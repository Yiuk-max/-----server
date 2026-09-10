-- ============================================================
-- 让每个账号成为自己的好友（friend_relation 写入 (UID, UID)），
-- 从而允许"给自己发消息"（自聊 / 收藏夹式用法）。
--
-- 背景：私聊前会校验 is_friend(sender, target)，因此此前无法给自己发消息。
-- 说明：新账号注册、每次登录都会调用 ensure_self_friend 幂等补齐；
--       本脚本用于给【已有】账号一次性回填历史数据。
-- 幂等：INSERT IGNORE，重复执行安全。
-- 执行： mysql -u<user> -p <db> < sql/add_self_friend.sql
-- ============================================================
INSERT IGNORE INTO friend_relation (UID, friend_UID, remark_name)
SELECT UID, UID, NULL FROM Account;
