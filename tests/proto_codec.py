#!/usr/bin/env python3
"""纯标准库的 chat_proto.Envelope 最小 protobuf 编解码器。

只为冒烟测试服务，覆盖 chat_proto.Envelope 的顶层标量字段：
  - 编码：type/content/target_UID/message/message_id/group_UID/... 等
  - 解码：顶层标量字段；嵌套 message（user/reply_to/meta/messages）只保留原始字节，
    冒烟测试不需要解析它们。

线上格式（WebSocket binary 帧）：
  | 4 字节 payload_len (网络序) | payload_len 字节 protobuf |
"""

# Envelope 字段号 -> 名称
NAME_BY_NUM = {
    1: 'type', 2: 'content', 3: 'target_UID', 4: 'message', 5: 'message_id',
    6: 'peer_id', 7: 'before_id', 8: 'has_more', 9: 'group_UID',
    10: 'target_user_UID', 11: 'requester_UID', 12: 'group_name', 13: 'new_name',
    14: 'promote', 15: 'friend_UID', 16: 'sender_UID', 17: 'email',
    18: 'apply_message', 19: 'remark', 20: 'accept', 21: 'UID', 22: 'username',
    23: 'password', 24: 'token', 25: 'user', 26: 'sender_name', 27: 'timestamp',
    28: 'reply_to_message_id', 29: 'reply_to', 30: 'file_name', 31: 'meta',
    32: 'messages',
}

NUM_BY_NAME = {v: k for k, v in NAME_BY_NUM.items()}

# length-delimited 字符串字段
STRING_FIELDS = {1, 2, 4, 12, 13, 17, 18, 19, 22, 23, 24, 26, 27, 30}


def encode_varint(value):
    value = int(value)
    if value < 0:
        value &= (1 << 64) - 1
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def _key(field_no, wire_type):
    return encode_varint((field_no << 3) | wire_type)


def encode_envelope(**fields):
    """按字段名构造 Envelope 的 protobuf 字节。"""
    body = bytearray()
    for name, value in fields.items():
        if value is None:
            continue
        field_no = NUM_BY_NAME[name]
        if field_no in STRING_FIELDS:
            data = str(value).encode('utf-8')
            body += _key(field_no, 2)
            body += encode_varint(len(data))
            body += data
        else:
            # varint：int 或 bool（bool 按 0/1 编码）
            if value is True:
                v = 1
            elif value is False:
                v = 0
            else:
                v = value
            body += _key(field_no, 0)
            body += encode_varint(v)
    return bytes(body)


def decode_varint(buf, pos):
    result = 0
    shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, pos
        shift += 7


def decode_envelope(data):
    """把 Envelope 的 protobuf 字节解析为 {字段名: 值}。"""
    out = {}
    pos = 0
    n = len(data)
    while pos < n:
        key, pos = decode_varint(data, pos)
        field_no = key >> 3
        wire_type = key & 0x07
        name = NAME_BY_NUM.get(field_no, 'f%d' % field_no)

        if wire_type == 0:  # varint
            value, pos = decode_varint(data, pos)
            out[name] = value
        elif wire_type == 2:  # length-delimited
            length, pos = decode_varint(data, pos)
            chunk = data[pos:pos + length]
            pos += length
            if field_no in STRING_FIELDS:
                out[name] = chunk.decode('utf-8', 'replace')
            else:
                # 嵌套 message：冒烟测试不解析，仅保留原始字节
                out[name] = chunk
        elif wire_type == 5:  # 32-bit
            pos += 4
        elif wire_type == 1:  # 64-bit
            pos += 8
        else:
            break
    return out
