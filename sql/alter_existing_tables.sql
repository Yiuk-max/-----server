-- ============================================================
-- 现有表改造脚本（依赖 file_system.sql 先建好 file 表）
--   1. Account 增加 avatar_id → file.id（头像走统一文件体系）
--   2. message 增加 is_file / file_id → file.id（文件消息只存引用）
-- 设计文档：docs/文件系统方案.md
-- ============================================================

USE chat_server;

-- ------------------------------------------------------------
-- 1. Account：用户头像指向 file.id
--    旧的 avatar_file_id(VARCHAR) 暂保留兼容，后续可下线。
--    加载头像时：原账号查询仍只返回文字信息；头像经
--    file_download_init(file_id = avatar_id) 单独传输。
-- ------------------------------------------------------------
ALTER TABLE Account
    ADD COLUMN avatar_id BIGINT UNSIGNED NULL COMMENT '用户头像文件ID(file.id)' AFTER avatar_file_id,
    ADD CONSTRAINT fk_account_avatar
        FOREIGN KEY (avatar_id) REFERENCES file(id) ON DELETE SET NULL;

-- ------------------------------------------------------------
-- 2. message：文件消息
--    is_file=1 表示这是一条文件消息；历史/在线消息按文字返回
--    (file_id + 文件名 + 大小等元数据)，用户点击后再下载本体。
-- ------------------------------------------------------------
ALTER TABLE message
    ADD COLUMN is_file TINYINT(1) NOT NULL DEFAULT 0 COMMENT '是否文件消息(0=否 1=是)' AFTER content,
    ADD COLUMN file_id BIGINT UNSIGNED NULL COMMENT '文件ID(file.id)' AFTER is_file,
    ADD CONSTRAINT fk_message_file
        FOREIGN KEY (file_id) REFERENCES file(id) ON DELETE SET NULL;

-- ------------------------------------------------------------
-- 可选后续（本脚本暂不执行）：
--   community：community.avatar(VARCHAR) → community.avatar_id BIGINT → file.id
--   CustomEmoji：新增 custom_emoji 表时用 file_id → file.id
-- 均遵循同一规则：DB 只存 file_id，二进制统一走 FileStorage。
-- ------------------------------------------------------------
