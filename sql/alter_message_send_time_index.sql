-- ============================================================
-- message 表补充 send_time 索引
--   背景：登录后 send_offline_messages → get_offline_messages
--        每次登录必跑，查询条件为
--          (type='private' AND receiver_UID=?) OR (group...) OR (channel...)
--          AND send_time > since_time
--        三个 OR 分支无法利用现有索引，优化器退化为 PRIMARY 全表扫描。
--   本脚本给 send_time 加单列索引：
--     since_time = 上次登录时间，高频登录用户的时间窗口很小，
--     该索引可直接把全扫变成按 send_time 的范围扫，避免登录被拖慢。
--   与 sql/create_table.sql 中 message 表的定义保持一致。
-- ============================================================

USE chat_server;

ALTER TABLE message
    ADD INDEX idx_send_time (send_time);
