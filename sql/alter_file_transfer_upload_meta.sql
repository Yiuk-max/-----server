-- ============================================================
-- 上传会话元数据持久化迁移（支持断线重连续传）
--   给 file_transfer 补充上传所需元数据，重连后可按 next_chunk_index
--   重建 writer 继续上传，不再依赖客户端会话内存。
-- ============================================================

USE chat_server;

ALTER TABLE file_transfer
    ADD COLUMN storage_key   VARCHAR(191) NULL COMMENT '上传目标 storage_key' AFTER tmp_path,
    ADD COLUMN file_type     VARCHAR(16)  NULL COMMENT '文件类别(avatar/emoji/image/attachment/file)' AFTER storage_key,
    ADD COLUMN original_name VARCHAR(255) NULL COMMENT '原始文件名' AFTER file_type,
    ADD COLUMN mime_type     VARCHAR(128) NULL COMMENT 'MIME类型' AFTER original_name;
