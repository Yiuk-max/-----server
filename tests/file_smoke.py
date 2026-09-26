#!/usr/bin/env python3
"""文件系统端到端冒烟测试（WebSocket + protobuf 最小编解码）。

覆盖：
  1. 分片上传（file_upload_init / file_chunk / file_upload_done）
  2. 拉取式分片下载（file_download_init / file_download_chunk）
  3. 下载中途暂停 / 继续（file_transfer_pause / file_transfer_resume）
  4. 上传中断线重连续传（file_upload_resume）

前置条件：
  - server 以 configure.json 的 "net_layer": "websocket" 启动，MySQL/Redis 可用。
  - 已执行 sql/file_system.sql、sql/alter_existing_tables.sql、
    sql/alter_file_transfer_upload_meta.sql（断线重连续传依赖后者）。

用法：
  python3 tests/file_smoke.py [host] [port] [path]
"""
import hashlib
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ws_chat_smoke as base          # noqa: E402  复用 WSClient / register / login
from proto_codec import decode_envelope, decode_varint  # noqa: E402


# ---------------- 最小 protobuf 编码（覆盖文件相关嵌套消息） ----------------

def varint(n):
    if n < 0:
        n &= (1 << 64) - 1
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def key(field_no, wire_type):
    return varint((field_no << 3) | wire_type)


def s(field_no, text):
    d = text.encode('utf-8')
    return key(field_no, 2) + varint(len(d)) + d


def vi(field_no, value):
    return key(field_no, 0) + varint(value)


def ld(field_no, data):
    return key(field_no, 2) + varint(len(data)) + data


# Envelope 字段号 -> 名称（只列本测试用到的）
ENV_NUM = {
    'type': 1, 'content': 2, 'target_UID': 3, 'message': 4, 'message_id': 5,
    'peer_id': 6, 'before_id': 7, 'file_name': 30, 'meta': 31,
    'file_meta': 44, 'transfer_info': 45, 'transfer_id': 46, 'file_id': 47,
    'is_file': 50, 'file_size': 51, 'chunk_index': 55,
}
ENV_BYTES = {31, 44, 45}  # 这些字段是嵌套 message，直接传原始字节


def encode_env(**fields):
    body = bytearray()
    for name, value in fields.items():
        if value is None:
            continue
        no = ENV_NUM[name]
        if no in ENV_BYTES:
            body += ld(no, value)
        elif isinstance(value, str):
            body += s(no, value)
        elif isinstance(value, bool):
            body += vi(no, 1 if value else 0)
        else:
            body += vi(no, value)
    return bytes(body)


# FileMeta：1 file_id,2 original_name,3 mime_type,4 size,5 hash,6 type,7 uploader_uid,8 created_at
def encode_file_meta(original_name, size, mime_type="", ftype="file", hash_hex="", file_id=""):
    b = bytearray()
    if file_id: b += s(1, file_id)
    if original_name: b += s(2, original_name)
    if mime_type: b += s(3, mime_type)
    if size: b += vi(4, size)
    if hash_hex: b += s(5, hash_hex)
    if ftype: b += s(6, ftype)
    return bytes(b)


# FileChunkMeta：1 type,2 file_id,3 filename,4 total_size,5 chunk_index,
#                 6 chunk_count,7 chunk_size,8 offset,9 transfer_id,10 hash
def encode_chunk_meta(transfer_id, chunk_index, chunk_count, chunk_size,
                      offset=0, filename="", total_size=0, file_id=""):
    b = bytearray()
    b += s(1, "file_chunk")
    if file_id: b += s(2, file_id)
    if filename: b += s(3, filename)
    if total_size: b += vi(4, total_size)
    b += vi(5, chunk_index)
    b += vi(6, chunk_count)
    b += vi(7, chunk_size)
    if offset: b += vi(8, offset)
    if transfer_id: b += s(9, transfer_id)
    return bytes(b)


# ---------------- 解码 ----------------

FILE_META_NAMES = {1: 'file_id', 2: 'original_name', 3: 'mime_type', 4: 'size',
                   5: 'hash', 6: 'type', 7: 'uploader_uid', 8: 'created_at'}
FILE_META_STR = {1, 2, 3, 5, 6, 8}

TRANSFER_NAMES = {1: 'transfer_id', 2: 'file_id', 3: 'uploader_uid', 4: 'receiver_uid',
                  5: 'direction', 6: 'status', 7: 'total_size', 8: 'transferred_size',
                  9: 'chunk_size', 10: 'chunk_count', 11: 'next_chunk_index'}
TRANSFER_STR = {1, 2, 5, 6}

CHUNK_NAMES = {1: 'type', 2: 'file_id', 3: 'filename', 4: 'total_size', 5: 'chunk_index',
               6: 'chunk_count', 7: 'chunk_size', 8: 'offset', 9: 'transfer_id', 10: 'hash'}
CHUNK_STR = {1, 2, 3, 9, 10}


def decode_sub(data, names, str_fields):
    out = {}
    pos, n = 0, len(data)
    while pos < n:
        k, pos = decode_varint(data, pos)
        f, wt = k >> 3, k & 0x07
        if wt == 0:
            value, pos = decode_varint(data, pos)
            out[names.get(f, 'f%d' % f)] = value
        elif wt == 2:
            ln, pos = decode_varint(data, pos)
            chunk = data[pos:pos + ln]
            pos += ln
            out[names.get(f, 'f%d' % f)] = chunk.decode('utf-8', 'replace') if f in str_fields else chunk
        else:
            break
    return out


def decode_file_meta(data):
    return decode_sub(data, FILE_META_NAMES, FILE_META_STR)


def decode_transfer_info(data):
    return decode_sub(data, TRANSFER_NAMES, TRANSFER_STR)


def decode_chunk_meta(data):
    return decode_sub(data, CHUNK_NAMES, CHUNK_STR)


class FileClient(base.WSClient):
    """在 WSClient 基础上支持「protobuf + 帧尾文件数据」收发。"""

    def send_raw(self, body, file_data=b""):
        # WS 应用层：| 4 字节 payload_len (网络序) | protobuf | file_data |
        payload = int(len(body)).to_bytes(4, 'big') + body + file_data
        self._send_frame(0x2, payload)

    def recv_raw(self):
        payload = self._read_message()
        if len(payload) < 4:
            raise base.WSError("short binary frame")
        body_len = int.from_bytes(payload[:4], 'big')
        env = decode_envelope(payload[4:4 + body_len])
        file_data = payload[4 + body_len:]
        return env, file_data

    def recv_env(self):
        env, _ = self.recv_raw()
        return env


def upload(client, data, filename, ftype="file", hash_hex=""):
    if not hash_hex:
        hash_hex = hashlib.sha256(data).hexdigest()
    client.send_raw(encode_env(
        type="file_upload_init",
        file_meta=encode_file_meta(filename, len(data), "application/octet-stream", ftype, hash_hex)))
    ready = client.recv_until(lambda m: m.get("type") == "file_upload_ready")
    ti = decode_transfer_info(ready["f45"])
    transfer_id = int(ti["transfer_id"])
    chunk_count = int(ti["chunk_count"])
    chunk_size = int(ti["chunk_size"])

    offset = 0
    for i in range(chunk_count):
        n = min(chunk_size, len(data) - offset)
        chunk = data[offset:offset + n]
        client.send_raw(
            encode_env(type="file_chunk",
                       meta=encode_chunk_meta(str(transfer_id), i, chunk_count, n,
                                              offset=offset, filename=filename,
                                              total_size=len(data))),
            chunk)
        offset += n

    client.send_raw(encode_env(type="file_upload_done", transfer_id=transfer_id))
    done = client.recv_until(lambda m: m.get("type") == "file_upload_complete")
    fm = decode_file_meta(done["f44"])
    return int(fm["file_id"]), transfer_id, chunk_count, chunk_size


def download(client, file_id, expected, with_pause=False):
    client.send_raw(encode_env(type="file_download_init", file_id=str(file_id)))
    ready = client.recv_until(lambda m: m.get("type") == "file_download_ready")
    ti = decode_transfer_info(ready["f45"])
    transfer_id = int(ti["transfer_id"])
    chunk_count = int(ti["chunk_count"])

    received = bytearray()

    def pull(i):
        client.send_raw(encode_env(type="file_download_chunk", transfer_id=transfer_id, chunk_index=i))
        env, fd = client.recv_raw()
        if env.get("type") != "file_chunk":
            raise AssertionError(f"expected file_chunk, got {env.get('type')}")
        cm = decode_chunk_meta(env["meta"])
        # proto3 会省略默认值 0，因此 chunk_index=0 时字段可能不存在。
        if int(cm.get("chunk_index", 0)) != i:
            raise AssertionError(f"chunk_index mismatch: {cm.get('chunk_index')} != {i}")
        received.extend(fd)

    if with_pause and chunk_count > 1:
        pull(0)
        client.send_raw(encode_env(type="file_transfer_pause", transfer_id=transfer_id))
        client.recv_until(lambda m: m.get("type") == "file_transfer_paused")
        client.send_raw(encode_env(type="file_transfer_resume", transfer_id=transfer_id))
        client.recv_until(lambda m: m.get("type") == "file_transfer_progress")
        for i in range(1, chunk_count):
            pull(i)
    else:
        for i in range(chunk_count):
            pull(i)

    if bytes(received) != expected:
        raise AssertionError("downloaded bytes mismatch")
    return received


def run(host, port, path):
    suffix = str(int(time.time()))[-6:]
    pwd = "fspass123"

    print("[1] 注册 / 登录")
    a = FileClient(host, port, path).connect()
    name_a = f"FS{suffix}"
    uid_a = base.register(a, name_a, pwd)
    base.login(a, uid_a, pwd)
    base.ok(f"logged in uid {uid_a}")

    print("[2] 分片上传")
    data = os.urandom(4 * 1024 * 1024 + 4096)  # 2 个分片（最后一片不满）
    hash_hex = hashlib.sha256(data).hexdigest()
    file_id, _, _, _ = upload(a, data, "hello.bin", "file", hash_hex)
    base.expect(file_id > 0, f"file_upload_complete file_id={file_id}")

    print("[3] 拉取式分片下载（含暂停/继续）")
    got = download(a, file_id, data, with_pause=True)
    base.expect(hashlib.sha256(got).hexdigest() == hash_hex, "downloaded sha256 matches")

    print("[4] 上传中断线重连续传")
    data2 = os.urandom(4 * 1024 * 1024 + 7777)
    hash2 = hashlib.sha256(data2).hexdigest()
    # 开一个上传会话，只发第一块后断开
    a.send_raw(encode_env(
        type="file_upload_init",
        file_meta=encode_file_meta("resume.bin", len(data2), "application/octet-stream", "file", hash2)))
    ready2 = a.recv_until(lambda m: m.get("type") == "file_upload_ready")
    ti2 = decode_transfer_info(ready2["f45"])
    transfer2 = int(ti2["transfer_id"])
    count2 = int(ti2["chunk_count"])
    cs2 = int(ti2["chunk_size"])

    n = min(cs2, len(data2))
    a.send_raw(encode_env(type="file_chunk",
                          meta=encode_chunk_meta(str(transfer2), 0, count2, n,
                                                 offset=0, filename="resume.bin",
                                                 total_size=len(data2))),
               data2[:n])
    time.sleep(0.2)  # 等服务端把第一块落库，再模拟断线
    a.close()

    a2 = FileClient(host, port, path).connect()
    base.login(a2, uid_a, pwd)
    a2.send_raw(encode_env(type="file_upload_resume", transfer_id=transfer2))
    resumed = a2.recv_until(lambda m: m.get("type") == "file_upload_ready")
    rti = decode_transfer_info(resumed["f45"])
    next_idx = int(rti["next_chunk_index"])
    base.expect(next_idx == 1, f"resume next_chunk_index={next_idx}")

    offset = n
    for i in range(next_idx, count2):
        m = min(cs2, len(data2) - offset)
        a2.send_raw(encode_env(type="file_chunk",
                               meta=encode_chunk_meta(str(transfer2), i, count2, m,
                                                      offset=offset, filename="resume.bin",
                                                      total_size=len(data2))),
                    data2[offset:offset + m])
        offset += m
    a2.send_raw(encode_env(type="file_upload_done", transfer_id=transfer2))
    done2 = a2.recv_until(lambda m: m.get("type") == "file_upload_complete")
    fm2 = decode_file_meta(done2["f44"])
    file_id2 = int(fm2["file_id"])
    base.expect(file_id2 > 0, f"resumed upload completed file_id={file_id2}")

    print("[5] 下载续传文件校验")
    got2 = download(a2, file_id2, data2)
    base.expect(hashlib.sha256(got2).hexdigest() == hash2, "resumed file sha256 matches")

    print("[6] 乱序上传（重组）")
    data3 = os.urandom(4 * 1024 * 1024 + 5123)
    hash3 = hashlib.sha256(data3).hexdigest()
    a2.send_raw(encode_env(
        type="file_upload_init",
        file_meta=encode_file_meta("ooo.bin", len(data3), "application/octet-stream", "file", hash3)))
    r3 = a2.recv_until(lambda m: m.get("type") == "file_upload_ready")
    ti3 = decode_transfer_info(r3["f45"])
    t3 = int(ti3["transfer_id"]); c3 = int(ti3["chunk_count"]); cs3 = int(ti3["chunk_size"])
    for i in list(range(c3))[::-1]:  # 逆序发块
        off = i * cs3
        m = min(cs3, len(data3) - off)
        a2.send_raw(encode_env(type="file_chunk",
                               meta=encode_chunk_meta(str(t3), i, c3, m, offset=off,
                                                      filename="ooo.bin", total_size=len(data3))),
                    data3[off:off + m])
    a2.send_raw(encode_env(type="file_upload_done", transfer_id=t3))
    d3 = a2.recv_until(lambda m: m.get("type") == "file_upload_complete")
    fm3 = decode_file_meta(d3["f44"]); file_id3 = int(fm3["file_id"])
    got3 = download(a2, file_id3, data3)
    base.expect(hashlib.sha256(got3).hexdigest() == hash3, "out-of-order upload reassembled correctly")

    print("[7] 文件列表 / 删除")
    a2.send_raw(encode_env(type="file_list_request"))
    a2.recv_until(lambda m: m.get("type") == "file_list_response")
    base.ok("file_list_response received")
    a2.send_raw(encode_env(type="file_delete", file_id=str(file_id3)))
    a2.recv_until(lambda m: base.is_system(m, "File deleted"))
    a2.send_raw(encode_env(type="file_download_init", file_id=str(file_id3)))
    a2.recv_until(lambda m: base.is_system(m, "File not found"))
    base.ok("file_delete removed file and download rejected")

    a.close()
    a2.close()
    print("\nall file system smoke tests passed")


def main():
    args = [x for x in sys.argv[1:] if not x.startswith("--")]
    host = args[0] if len(args) > 0 else "127.0.0.1"
    port = int(args[1]) if len(args) > 1 else 8080
    path = args[2] if len(args) > 2 else "/ws"
    try:
        run(host, port, path)
    except Exception as e:
        print(f"\n[FAIL] {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
