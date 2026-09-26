-- ============================================================
-- 社区头像 / 背景图迁移脚本
--   给 community 表新增：
--     avatar_id  -> file.id（社区头像）
--     banner_id  -> file.id（社区背景图）
--   依赖 file_system.sql 已建好 file 表。
-- ============================================================

USE chat_server;

ALTER TABLE community
    ADD COLUMN avatar_id BIGINT UNSIGNED NULL COMMENT '社区头像文件ID(file.id)' AFTER avatar,
    ADD COLUMN banner_id BIGINT UNSIGNED NULL COMMENT '社区背景图文件ID(file.id)' AFTER avatar_id,
    ADD CONSTRAINT fk_community_avatar
        FOREIGN KEY (avatar_id) REFERENCES file(id) ON DELETE SET NULL,
    ADD CONSTRAINT fk_community_banner
        FOREIGN KEY (banner_id) REFERENCES file(id) ON DELETE SET NULL;
