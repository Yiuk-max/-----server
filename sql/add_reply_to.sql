-- ============================================================
-- message 表新增 reply_to_message_id：回复某条消息
--   reply_to_message_id = 123 表示本条消息回复了 id=123 的消息；NULL 表示非回复。
--   只支持一层直接引用（回复的消息本身也可以被回复，但 UI 只展示一层引用）。
-- 说明：MySQL 8 不支持 ADD COLUMN IF NOT EXISTS，本脚本只需执行一次。
-- 执行： sudo mysql chat_server < sql/add_reply_to.sql
-- ============================================================
ALTER TABLE message
    ADD COLUMN reply_to_message_id BIGINT UNSIGNED NULL
        COMMENT '回复的原消息id(NULL=非回复)' AFTER content;
