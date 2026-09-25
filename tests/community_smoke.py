#!/usr/bin/env python3
"""社区功能 WebSocket 冒烟测试（复用 ws_chat_smoke.py 的 WSClient）。

覆盖：
  1. 注册 / 登录两个用户
  2. A 建社区（create_community）
  3. A 建频道（create_channel）
  4. A 拉 B 进社区（community_add_member）
  5. A 频道发言 -> B 收到 Channel_Chat
  6. B 拉取 社区列表 / 频道列表 / 成员列表（结构化响应）
  7. 非社区成员不能发言（负向）
  8. B 退出社区后不再看到该社区
  9. A 删频道 / 删社区

前置条件：
  - server 以 configure.json 的 "net_layer": "websocket" 启动，MySQL 可用。

用法：
  python3 tests/community_smoke.py [host] [port] [path]
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ws_chat_smoke import (  # noqa: E402
    WSClient, connect, content, is_system, expect, ok, register, login,
)


def parse_id(msg, pattern):
    m = re.search(pattern, content(msg))
    if not m:
        raise AssertionError("cannot parse id from: " + content(msg))
    return int(m.group(1))


def run(host, port, path):
    suffix = str(int(time.time()))[-6:]
    pwd = "cpass123"

    print("[1] 注册 / 登录两个用户")
    a = connect(host, port, path)
    uid_a = register(a, f"CA{suffix}", pwd)
    b = connect(host, port, path)
    uid_b = register(b, f"CB{suffix}", pwd)
    login(a, uid_a, pwd)
    login(b, uid_b, pwd)
    ok(f"A={uid_a}, B={uid_b} 已登录")

    print("[2] A 建社区")
    a.send_envelope(type="create_community", group_name=f"Comm{suffix}",
                    description="hello community")
    community_id = parse_id(
        a.recv_until(lambda m: is_system(m, "Community created successfully")),
        r"Community ID: (\d+)")
    ok(f"community created: {community_id}")

    print("[3] A 建频道")
    a.send_envelope(type="create_channel", community_id=community_id,
                    group_name="general", category="闲聊")
    channel_id = parse_id(
        a.recv_until(lambda m: is_system(m, "Channel created successfully")),
        r"Channel ID: (\d+)")
    ok(f"channel created: {channel_id}")

    print("[4] A 拉 B 进社区")
    a.send_envelope(type="community_add_member", community_id=community_id,
                    target_user_UID=uid_b)
    b.recv_until(lambda m: is_system(m, "You have been added to community"))
    ok("B notified of being added to community")

    print("[5] A 频道发言 -> B 收到 Channel_Chat")
    text = f"chan-{suffix}"
    a.send_envelope(type="channel_chat", target_UID=channel_id, message=text)
    recv = b.recv_until(lambda m: m.get("type") == "Channel_Chat" and text in content(m))
    expect(recv.get("group_UID") == channel_id, "Channel_Chat carries group_UID=channel_id")
    ok("B received channel broadcast")

    print("[6] B 拉取 社区/频道/成员 列表")
    b.send_envelope(type="show_communities")
    comms = b.recv_until(lambda m: m.get("type") == "community_list_response")
    cids = [c.get("community_id") for c in comms.get("communities", [])]
    expect(community_id in cids, "community_list_response contains community_id")
    b_info = [c for c in comms.get("communities", [])
              if c.get("community_id") == community_id]
    expect(bool(b_info) and b_info[0].get("my_role") == "member", "B my_role == member")

    b.send_envelope(type="show_community_channels", community_id=community_id)
    chans = b.recv_until(lambda m: m.get("type") == "channel_list_response")
    chids = [c.get("channel_id") for c in chans.get("channels", [])]
    expect(channel_id in chids, "channel_list_response contains channel_id")

    b.send_envelope(type="show_community_members", community_id=community_id)
    mems = b.recv_until(lambda m: m.get("type") == "community_member_list_response")
    roles = {m.get("user_uid"): m.get("role") for m in mems.get("members", [])}
    expect(roles.get(uid_a) == "owner" and roles.get(uid_b) == "member",
           "members: A=owner, B=member")

    print("[7] 非社区成员不能发言（负向）")
    c = connect(host, port, path)
    uid_c = register(c, f"CC{suffix}", pwd)
    login(c, uid_c, pwd)
    c.send_envelope(type="channel_chat", target_UID=channel_id, message="intrude")
    c.recv_until(lambda m: is_system(m, "not a member of this community"))
    ok("non-member rejected by channel_chat")

    print("[8] B 退出社区后不再看到该社区")
    b.send_envelope(type="leave_community", community_id=community_id)
    b.recv_until(lambda m: is_system(m, "left community"))
    b.send_envelope(type="show_communities")
    comms2 = b.recv_until(lambda m: m.get("type") == "community_list_response")
    expect(community_id not in [x.get("community_id") for x in comms2.get("communities", [])],
           "community gone from B's list after leave")

    print("[9] A 删频道 / 删社区")
    a.send_envelope(type="delete_channel", community_id=community_id, channel_id=channel_id)
    a.recv_until(lambda m: is_system(m, "Channel deleted successfully"))
    ok("channel deleted")

    a.send_envelope(type="delete_community", community_id=community_id)
    a.recv_until(lambda m: is_system(m, "Community deleted successfully"))
    ok("community deleted")

    for c in (a, b, c):
        try:
            c.close()
        except Exception:
            pass

    print("\nall community smoke tests passed")


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
