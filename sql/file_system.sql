-- ============================================================
-- 文件系统建表脚本（新增 2 张表，不修改现有表）
-- 数据库：MySQL 8.0，字符集 utf8mb4
--   file          文件元数据（只描述文件，不存二进制）
--   file_transfer 传输会话（分片/断点/暂停续传/watchdog）
-- 现有表改造见同目录 alter_existing_tables.sql
-- 设计文档：docs/文件系统方案.md
-- ============================================================

USE chat_server;

-- ------------------------------------------------------------
-- 1. 文件元数据表 file
--    数据库只负责描述文件：
--      谁传的(uploader_uid)、叫什么(original_name)、多大(size)、
--      什么类型(type)、存哪(storage_key)、何时传(created_at)、哈希(hash)。
--    二进制本体放磁盘（路径由 FileStorage 根据 type + storage_key 生成）。
--    storage_key：服务器生成的随机唯一键，绝不用原始文件名。
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS file (
    id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '文件唯一ID',
    uploader_uid  BIGINT UNSIGNED NOT NULL                COMMENT '上传者UID',
    storage_key   VARCHAR(191)    NOT NULL                COMMENT '磁盘文件唯一键(服务器生成,非原始名)',
    type          ENUM('avatar','emoji','image','attachment','file')
                  NOT NULL DEFAULT 'file'                 COMMENT '文件类别',
    original_name VARCHAR(255)    NOT NULL                COMMENT '原始文件名(仅展示/下载回显)',
    mime_type     VARCHAR(128)    NULL                    COMMENT 'MIME类型',
    size          BIGINT UNSIGNED NOT NULL DEFAULT 0      COMMENT '文件字节数',
    hash          CHAR(64)        NOT NULL                COMMENT '内容SHA256(完整性校验)',
    created_at    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '上传时间',

    PRIMARY KEY (id),
    UNIQUE KEY uk_storage_key (storage_key),
    KEY idx_uploader (uploader_uid),
    CONSTRAINT fk_file_uploader
        FOREIGN KEY (uploader_uid) REFERENCES Account(UID) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='文件元数据表(只描述文件,不存二进制)';

-- ------------------------------------------------------------
-- 2. 文件传输会话表 file_transfer
--    分片上传/下载的会话状态机 + 断点信息。
--    上传：file_id 完成前为 NULL，完成后回填；写 tmp/<id>.part。
--    下载：file_id 指向已完成的 file 行。
--    received_bitmap：1 bit / chunk，支持乱序重组与精确续传。
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS file_transfer (
    id                BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '传输会话ID',
    file_id           BIGINT UNSIGNED NULL                    COMMENT '目标文件(上传完成前为NULL,完成后回填)',
    uploader_uid      BIGINT UNSIGNED NOT NULL                COMMENT '上传方/发送方',
    receiver_uid      BIGINT UNSIGNED NULL                    COMMENT '接收方(头像/emoji等无接收方可空)',
    direction         ENUM('upload','download') NOT NULL      COMMENT '上传/下载',
    status            ENUM('uploading','downloading','paused','completed','cancelled','failed')
                      NOT NULL DEFAULT 'uploading'            COMMENT '会话状态机',
    total_size        BIGINT UNSIGNED NOT NULL DEFAULT 0      COMMENT '总字节数',
    transferred_size  BIGINT UNSIGNED NOT NULL DEFAULT 0      COMMENT '已完成字节数(进度冗余)',
    chunk_size        INT UNSIGNED    NOT NULL DEFAULT 4194304 COMMENT '分片大小(默认4MiB)',
    chunk_count       INT UNSIGNED    NOT NULL DEFAULT 0      COMMENT '总分片数',
    next_chunk_index  INT UNSIGNED    NOT NULL DEFAULT 0      COMMENT '顺序传输断点块号',
    received_bitmap   VARBINARY(4096) NULL                    COMMENT '乱序重组位图(1bit/chunk)',
    tmp_path          VARCHAR(512)    NULL                    COMMENT '上传中临时文件相对路径',
    last_active_at    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                       ON UPDATE CURRENT_TIMESTAMP COMMENT '最近活动时间(watchdog依据)',
    created_at        DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    updated_at        DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                       ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',

    PRIMARY KEY (id),
    KEY idx_file (file_id),
    KEY idx_uploader (uploader_uid),
    KEY idx_receiver (receiver_uid, status),
    KEY idx_watchdog (status, last_active_at),
    CONSTRAINT fk_ft_file
        FOREIGN KEY (file_id)      REFERENCES file(id)       ON DELETE SET NULL,
    CONSTRAINT fk_ft_uploader
        FOREIGN KEY (uploader_uid) REFERENCES Account(UID)   ON DELETE CASCADE,
    CONSTRAINT fk_ft_receiver
        FOREIGN KEY (receiver_uid) REFERENCES Account(UID)   ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='文件传输会话表(分片/断点/暂停续传)';
