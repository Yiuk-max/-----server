#!/usr/bin/env python3
"""群聊广播回归测试：验证 A4 优化后「构造 + 序列化一次 + 批量查表」的广播正确性。

覆盖：
  1. 群主建群并拉 3 人入群（N>1 成员，触发 NoticeService::send_to_users_with_id 广播路径）
  2. 群聊广播：所有成员（含发送者自己）都收到，且 group_UID / sender_UID /
     sender_name / content / message_id / timestamp 一致
  3. 发送者收到 "Message sent. Message ID: X" 回执，且 message_id 与广播一致
  4. 回复广播：reply_to_message_id 正确带出

前置：
  - server 以 configure.json 的 "net_layer": "websocket" 启动，MySQL 可用。
  - 默认连接 127.0.0.1:8080，路径 /ws。

用法：
  python3 tests/test_group_broadcast.py [host] [port] [path]
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ws_chat_smoke import connect, content, expect, is_system, login, ok, register  # noqa: E402


def run(host, port, path):
    suffix = str(int(time.time()))[-6:]
    pwd = "gbpass123"
    name_a = f"GBA{suffix}"
    name_b = f"GBB{suffix}"
    name_c = f"GBC{suffix}"
    name_d = f"GBD{suffix}"

    print("[1] 注册 / 登录 4 个客户端（群主 + 3 成员）")
    a = connect(host, port, path)
    uid_a = register(a, name_a, pwd)
    login(a, uid_a, pwd)

    b = connect(host, port, path)
    uid_b = register(b, name_b, pwd)
    login(b, uid_b, pwd)

    c = connect(host, port, path)
    uid_c = register(c, name_c, pwd)
    login(c, uid_c, pwd)

    d = connect(host, port, path)
    uid_d = register(d, name_d, pwd)
    login(d, uid_d, pwd)
    ok(f"4 个账号登录: {uid_a}, {uid_b}, {uid_c}, {uid_d}")

    print("[2] 建群 + 拉 3 人入群")
    a.send_envelope(type="create_group", group_name=f"GBG{suffix}")
    created = a.recv_until(lambda m: is_system(m, "Group created successfully"))
    m = re.search(r"Group UID: (\d+)", content(created))
    if not m:
        raise AssertionError(f"cannot parse group UID from: {content(created)}")
    group_uid = int(m.group(1))
    ok(f"群已创建: {group_uid}")

    for uid in (uid_b, uid_c, uid_d):
        a.send_envelope(type="group_add_client", group_UID=group_uid,
                        target_user_UID=uid)
    for cli in (b, c, d):
        cli.recv_until(lambda m: is_system(m, "You have been added to group"))
    ok("B/C/D 均已入群")

    print("[3] 群聊广播：A 发，B/C/D 收到且字段一致")
    gchat = f"hello-broadcast-{suffix}"
    a.send_envelope(type="group_chat", target_UID=group_uid, message=gchat)

    recvs = {}
    for cli, uid in ((b, uid_b), (c, uid_c), (d, uid_d)):
        recv = cli.recv_until(
            lambda m: m.get("type") == "Group_Chat" and gchat in m.get("content", ""))
        recvs[uid] = recv
        expect(recv.get("group_UID") == group_uid,
               f"成员 {uid} 收到 group_UID={group_uid}")
        expect(recv.get("sender_UID") == uid_a,
               f"成员 {uid} 收到 sender_UID={uid_a}")
        expect(recv.get("sender_name") == name_a,
               f"成员 {uid} 收到 sender_name={name_a}")
        expect(bool(recv.get("timestamp")),
               f"成员 {uid} 收到 timestamp")

    # 发送者也通过同一条广播路径收到自己的回声 + 回执
    echo = a.recv_until(
        lambda m: m.get("type") == "Group_Chat" and gchat in m.get("content", ""))
    expect(echo.get("group_UID") == group_uid and echo.get("sender_UID") == uid_a,
           "发送者收到自己的广播回声")

    ack = a.recv_until(lambda m: is_system(m, "Message sent."))
    mid = re.search(r"Message ID: (\d+)", content(ack))
    if not mid:
        raise AssertionError(f"cannot parse message id from: {content(ack)}")
    msg_id = int(mid.group(1))
    ok(f"发送者收到回执 message_id={msg_id}")

    # A4 优化关键校验：广播是同一份 payload，各接收方拿到的 message_id 必须完全一致
    ids = {recvs[u].get("message_id") for u in recvs}
    tss = {recvs[u].get("timestamp") for u in recvs}
    expect(len(ids) == 1 and next(iter(ids), 0) == msg_id,
           "三个接收方与回执的 message_id 一致")
    expect(len(tss) == 1, "三个接收方的 timestamp 一致")
    expect(echo.get("message_id") == msg_id, "发送者回声 message_id 与回执一致")

    print("[4] 回复广播：B 回复，其余成员收到 reply_to_message_id")
    reply_text = f"reply-broadcast-{suffix}"
    b.send_envelope(type="group_chat", target_UID=group_uid,
                    message=reply_text, reply_to_message_id=msg_id)

    for cli, uid in ((a, uid_a), (c, uid_c), (d, uid_d)):
        recv = cli.recv_until(
            lambda m: m.get("type") == "Group_Chat" and reply_text in m.get("content", ""))
        expect(recv.get("reply_to_message_id") == msg_id,
               f"成员 {uid} 收到 reply_to_message_id={msg_id}")

    # 回复者自己也收到回声 + 回执
    b_echo = b.recv_until(
        lambda m: m.get("type") == "Group_Chat" and reply_text in m.get("content", ""))
    expect(b_echo.get("reply_to_message_id") == msg_id, "回复者收到自己的回声")
    b.recv_until(lambda m: is_system(m, "Message sent."))

    for cli in (a, b, c, d):
        try:
            cli.close()
        except Exception:
            pass

    print("\nall group broadcast tests passed")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
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
