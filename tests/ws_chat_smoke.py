#!/usr/bin/env python3
"""M2 WebSocket 聊天与通知端到端冒烟测试（纯标准库 + tests/proto_codec.py）。

协议说明：
  - 服务端 net_layer=websocket，连接 /ws。
  - 业务消息为 WebSocket binary 帧：
        | 4 字节 payload_len (网络序) | payload_len 字节 protobuf Envelope |
  - Envelope 定义见 src/proto/message.proto；本测试用 tests/proto_codec.py
    纯标准库编码/解码顶层标量字段。

覆盖：
  1. 注册 / 登录（两个客户端）
  2. 好友申请 + 接受
  3. 私聊实时投递
  4. 建群 / 拉人 / 群聊广播
  5. 离线消息（对方离线时落库，重连登录后补发）
  6. 顶号（第二处登录把第一处顶下线，旧连接收到通知）

前置条件：
  - server 以 configure.json 的 "net_layer": "websocket" 启动，MySQL 可用。
  - 默认连接 127.0.0.1:8080，路径 /ws。

用法：
  python3 tests/ws_chat_smoke.py [host] [port] [path]
  python3 tests/ws_chat_smoke.py --idle-seconds 3    # 额外验证空闲超时
      （需服务端 use_heartbeat=true 且 heartbeat_interval<=3）
"""
import base64
import hashlib
import os
import re
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from proto_codec import decode_envelope, encode_envelope  # noqa: E402

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class WSError(Exception):
    pass


class WSClient:
    """最小 WebSocket 客户端：握手、掩码帧、binary 收发。"""

    def __init__(self, host, port, path, timeout=5.0):
        self.host = host
        self.port = port
        self.path = path
        self.timeout = timeout
        self.sock = None
        self.buf = b""
        self.closed = False

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        request = (
            f"GET {self.path} HTTP/1.1\r\n"
            f"Host: {self.host}:{self.port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        )
        self.sock.sendall(request.encode())

        header = b""
        while b"\r\n\r\n" not in header:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise WSError("handshake: connection closed")
            header += chunk
        head, _, rest = header.partition(b"\r\n\r\n")
        status_line = head.split(b"\r\n", 1)[0]
        if b" 101 " not in status_line:
            raise WSError("handshake failed: " + status_line.decode(errors="replace"))
        expected = base64.b64encode(
            hashlib.sha1((key + GUID).encode()).digest()).decode()
        if expected.encode() not in head:
            raise WSError("handshake: bad Sec-WebSocket-Accept")
        self.buf = rest
        return self

    # ---------- 底层帧 ----------

    def _recv_exact(self, n):
        while len(self.buf) < n:
            try:
                chunk = self.sock.recv(65536)
            except (socket.timeout, TimeoutError):
                raise WSError("recv timeout")
            except OSError as e:
                raise WSError(f"recv error: {e}")
            if not chunk:
                self.closed = True
                raise WSError("connection closed by peer")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def _read_frame(self):
        b1, b2 = self._recv_exact(2)
        opcode = b1 & 0x0F
        fin = bool(b1 & 0x80)
        masked = bool(b2 & 0x80)
        length = b2 & 0x7F
        if length == 126:
            length = struct.unpack("!H", self._recv_exact(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self._recv_exact(8))[0]
        mask = self._recv_exact(4) if masked else b""
        payload = self._recv_exact(length) if length else b""
        if masked:
            payload = bytes(c ^ mask[i % 4] for i, c in enumerate(payload))
        return opcode, fin, payload

    def _read_message(self):
        chunks = []
        while True:
            opcode, fin, payload = self._read_frame()
            if opcode == 0x9:  # ping -> pong
                self._send_frame(0xA, payload)
                continue
            if opcode == 0xA:  # pong
                continue
            if opcode == 0x8:  # close
                self.closed = True
                raise WSError("server closed connection")
            if opcode in (0x1, 0x2):
                chunks = [payload]
            elif opcode == 0x0:
                chunks.append(payload)
            if fin:
                return b"".join(chunks)

    def _send_frame(self, opcode, payload):
        if self.closed:
            raise WSError("send on closed connection")
        mask = os.urandom(4)
        n = len(payload)
        header = bytes([0x80 | opcode])
        if n < 126:
            header += bytes([0x80 | n])
        elif n < (1 << 16):
            header += bytes([0x80 | 126]) + struct.pack("!H", n)
        else:
            header += bytes([0x80 | 127]) + struct.pack("!Q", n)
        masked = bytes(c ^ mask[i % 4] for i, c in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    # ---------- 业务层 ----------

    def send_envelope(self, **fields):
        body = encode_envelope(**fields)
        payload = struct.pack("!I", len(body)) + body
        self._send_frame(0x2, payload)

    def recv_envelope(self, timeout=None):
        self.sock.settimeout(self.timeout if timeout is None else timeout)
        payload = self._read_message()
        if len(payload) < 4:
            raise WSError("short binary frame")
        (body_len,) = struct.unpack("!I", payload[:4])
        return decode_envelope(payload[4:4 + body_len])

    def recv_until(self, pred, timeout=None):
        deadline = time.time() + (self.timeout if timeout is None else timeout)
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise WSError("timeout waiting for matching message")
            msg = self.recv_envelope(timeout=remaining)
            if pred(msg):
                return msg

    def expect_closed(self, timeout=5.0):
        """等待对端关闭（或收到 close 帧）。"""
        deadline = time.time() + timeout
        try:
            while time.time() < deadline:
                self.recv_envelope(timeout=deadline - time.time())
        except WSError:
            return True
        raise WSError("expected connection close")

    def close(self):
        if self.sock and not self.closed:
            try:
                self._send_frame(0x8, struct.pack("!H", 1000))
            except Exception:
                pass
        self.closed = True
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None


# ---------------- 断言与业务辅助 ----------------

def content(msg):
    return msg.get("content", "")


def is_system(msg, needle=None):
    return msg.get("type") == "system" and (needle is None or needle in content(msg))


def ok(label):
    print(f"  [ok] {label}")


def expect(cond, label):
    if not cond:
        raise AssertionError(f"[FAIL] {label}")
    ok(label)


def register(client, name, password):
    # 新用户注册必须带邮箱（不校验格式，仅要求唯一）
    client.send_envelope(type="register", username=name,
                         password=password, email=f"{name}@example.com")
    msg = client.recv_until(lambda m: is_system(m, "Registration successful"))
    m = re.search(r"UID (\d+)", content(msg))
    if not m:
        raise AssertionError(f"cannot parse UID from: {content(msg)}")
    return int(m.group(1))


def login(client, uid, password):
    client.send_envelope(type="login", UID=uid, password=password)
    client.recv_until(lambda m: m.get("type") == "login_success")


def connect(host, port, path, timeout=5.0):
    return WSClient(host, port, path, timeout).connect()


def run(host, port, path, idle_seconds=None):
    suffix = str(int(time.time()))[-6:]
    pwd = "m2pass123"

    print("[1] 注册 / 登录")
    a = connect(host, port, path)
    name_a = f"M2A{suffix}"
    uid_a = register(a, name_a, pwd)
    b = connect(host, port, path)
    name_b = f"M2B{suffix}"
    uid_b = register(b, name_b, pwd)
    expect(uid_a > 0 and uid_b > 0 and uid_a != uid_b,
           f"two accounts registered: {uid_a}, {uid_b}")
    login(a, uid_a, pwd)
    login(b, uid_b, pwd)
    ok(f"login over WebSocket ok (uid {uid_a}, {uid_b})")

    print("[2] 好友申请 / 接受")
    a.send_envelope(type="add_friend", email=f"{name_b}@example.com", apply_message="hi")
    b.recv_until(lambda m: is_system(m, "wants to be friend with you"))
    ok("friend request notification delivered to B")
    b.send_envelope(type="accept_friend", sender_UID=uid_a)
    a.recv_until(lambda m: is_system(m, "You are now friends with"))
    ok("friend accepted; A notified")

    print("[3] 私聊实时投递")
    chat_text = f"hello-m2-{suffix}"
    a.send_envelope(type="private_chat", target_UID=uid_b, message=chat_text)
    recv = b.recv_until(lambda m: m.get("type") == "private_chat" and chat_text in content(m))
    expect("message_id" in recv, "private chat carries message_id")
    ok("B received private chat")

    print("[4] 建群 / 拉人 / 群聊广播")
    a.send_envelope(type="create_group", group_name=f"M2G{suffix}")
    created = a.recv_until(lambda m: is_system(m, "Group created successfully"))
    g = re.search(r"Group UID: (\d+)", content(created))
    if not g:
        raise AssertionError(f"cannot parse group UID: {content(created)}")
    group_uid = int(g.group(1))
    ok(f"group created: {group_uid}")

    a.send_envelope(type="group_add_client", group_UID=group_uid,
                    target_user_UID=uid_b)
    b.recv_until(lambda m: is_system(m, "You have been added to group"))
    ok("B notified of being added to group")

    gchat = f"group-m2-{suffix}"
    a.send_envelope(type="group_chat", target_UID=group_uid, message=gchat)
    grecv = b.recv_until(lambda m: m.get("type") == "Group_Chat" and gchat in content(m))
    expect(grecv.get("group_UID") == group_uid, "group chat carries group_UID")
    ok("B received group broadcast")

    print("[5] 离线消息")
    b.close()
    # Account.last_login_time 与 message.timestamp 都是秒级；确保离线消息晚于登录时间，
    # 避免两者落在同一秒时被 since_time 查询边界排除。
    time.sleep(1.1)
    off_text = f"offline-m2-{suffix}"
    a.send_envelope(type="private_chat", target_UID=uid_b, message=off_text)
    a.recv_until(lambda m: is_system(m, "Message sent."))
    ok("offline message stored while B offline")

    b2 = connect(host, port, path)
    login(b2, uid_b, pwd)
    b2.recv_until(lambda m: m.get("type") == "private_chat" and off_text in content(m),
                  timeout=5.0)
    ok("offline message pushed after B re-login")

    print("[6] 顶号")
    a2 = connect(host, port, path)
    login(a2, uid_a, pwd)
    a.recv_until(lambda m: is_system(m, "kicked offline"), timeout=5.0)
    ok("first connection received kick-offline notice")
    # 新连接仍可用
    a2.send_envelope(type="heartbeat")
    a2.recv_until(lambda m: m.get("type") == "heartbeat_ack")
    ok("new connection remains usable after kick")

    if idle_seconds is not None:
        print(f"[7] 空闲超时（服务端 use_heartbeat=true, interval<={idle_seconds}s）")
        idle = connect(host, port, path)
        idle.expect_closed(timeout=idle_seconds + 5)
        ok("idle connection closed by server")

    for c in (a, b2, a2):
        try:
            c.close()
        except Exception:
            pass

    print("\nall M2 websocket chat smoke tests passed")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    idle_seconds = None
    for a in sys.argv[1:]:
        if a.startswith("--idle-seconds"):
            idle_seconds = int(a.split("=", 1)[1]) if "=" in a else 5
    host = args[0] if len(args) > 0 else "127.0.0.1"
    port = int(args[1]) if len(args) > 1 else 8080
    path = args[2] if len(args) > 2 else "/ws"
    try:
        run(host, port, path, idle_seconds)
    except Exception as e:
        print(f"\n[FAIL] {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
