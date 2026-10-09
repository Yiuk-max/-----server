-- ============================================================
-- message 表补充 (receiver_UID, type, send_time) 复合索引
--   背景：get_offline_messages 由「OR 三分支」改写为「UNION ALL 三分支」，
--        每个分支都是 receiver_UID=?/IN(...) AND type=? AND send_time>?，
--        本复合索引正好覆盖三个分支的最左前缀（receiver_UID 等值/IN → type 等值 → send_time 范围）：
--          private : receiver_UID=?  AND type='private' AND send_time>?
--          group   : receiver_UID IN(组列表) AND type='group'   AND send_time>?
--          channel : receiver_UID IN(频道列表) AND type='channel' AND send_time>?
--   注意：
--     1) 与 idx_send_time(send_time) 不冲突——delete_expired_messages 的
--        send_time < NOW()-7DAY 仍依赖单列 idx_send_time。
--     2) 与 idx_group(receiver_UID, id) 不冲突——get_history_page 的
--        type=? AND receiver_UID=? AND id<? 仍依赖 idx_group 做 id 范围 + 倒序。
--   与 sql/create_table.sql 中 message 表的定义保持一致。
-- ============================================================

USE chat_server;

ALTER TABLE message
    ADD INDEX idx_receiver_type_send_time (receiver_UID, type, send_time);
