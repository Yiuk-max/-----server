-- ============================================================
-- Baka Community 数据库建表脚本（与当前 chat_server 库保持一致）
-- 数据库：MySQL 8.0（collate utf8mb4_0900_ai_ci）
-- 字符集：utf8mb4（必须，否则中文/emoji 会乱码或插入失败）
--
-- 包含 11 张表：
--   Account / account_email / friend_relation / relation_apply /
--   `Group` / Groupmember / message / community / community_member /
--   file / file_transfer
--
-- 每张表前有三行注释，描述表内部字段：
--   第一行：类型
--   第二行：名字
--   第三行：说明
-- ============================================================

CREATE DATABASE IF NOT EXISTS chat_server
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_0900_ai_ci;

USE chat_server;

-- ------------------------------------------------------------
-- 类型：UID bigint unsigned | password varchar(128) | nickname varchar(64) | settings json | language varchar(16) | token varchar(64) | avatar_file_id varchar(128) | avatar_id bigint unsigned | create_time datetime | birthday date
-- 名字：UID | password | nickname | settings | language | token | avatar_file_id | avatar_id | create_time | birthday
-- 说明：用户唯一ID(系统分配) | 密码(建议存储hash) | 昵称 | 账号设置(JSON) | 语言 | 登录令牌 | 用户头像文件ID(预留) | 用户头像文件ID(file.id) | 注册时间 | 生日
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS Account (
    UID            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '用户唯一ID(系统分配)',
    password       VARCHAR(128)    NOT NULL                COMMENT '密码(建议存储hash)',
    nickname       VARCHAR(64)     NOT NULL                COMMENT '昵称',
    settings       JSON            NULL                    COMMENT '账号设置(主题/语言/通知开关等,JSON序列化)',
    language       VARCHAR(16)     NOT NULL DEFAULT 'Chinese' COMMENT '语言',
    token          VARCHAR(64)     NULL                    COMMENT '登录令牌(自动登录用,可空)',
    avatar_file_id VARCHAR(128)    NULL                    COMMENT '用户头像文件ID(预留,二期文件传输)',
    avatar_id      BIGINT UNSIGNED NULL                    COMMENT '用户头像文件ID(file.id)',
    create_time    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '注册时间',
    birthday       DATE            NULL                    COMMENT '生日(可空)',

    PRIMARY KEY (UID),
    KEY fk_account_avatar (avatar_id),
    CONSTRAINT fk_account_avatar
        FOREIGN KEY (avatar_id) REFERENCES file(id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='用户账户表';

-- ------------------------------------------------------------
-- 类型：email varchar(190) | UID bigint unsigned | create_time datetime | update_time datetime
-- 名字：email | UID | create_time | update_time
-- 说明：邮箱(登录用,唯一) | 绑定的账户UID | 绑定时间 | 更新时间
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS account_email (
    email       VARCHAR(190)    NOT NULL COMMENT '邮箱(登录用,唯一)',
    UID         BIGINT UNSIGNED NOT NULL COMMENT '绑定的账户UID',
    create_time DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '绑定时间',
    update_time DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                ON UPDATE CURRENT_TIMESTAMP          COMMENT '更新时间',

    PRIMARY KEY (email),
    UNIQUE KEY uk_account_email_uid (UID),
    CONSTRAINT fk_email_uid
        FOREIGN KEY (UID) REFERENCES Account(UID) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='账户邮箱绑定表';

-- ------------------------------------------------------------
-- 类型：UID bigint unsigned | friend_UID bigint unsigned | remark_name varchar(64) | create_time datetime
-- 名字：UID | friend_UID | remark_name | create_time
-- 说明：用户A | 好友B | 备注名(可空) | 成为好友时间
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS friend_relation (
    UID         BIGINT UNSIGNED NOT NULL COMMENT '用户A',
    friend_UID  BIGINT UNSIGNED NOT NULL COMMENT '好友B',
    remark_name VARCHAR(64)     NULL     COMMENT '备注名(可空)',
    create_time DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '成为好友时间',

    PRIMARY KEY (UID, friend_UID),
    KEY idx_friend_uid (friend_UID),
    CONSTRAINT fk_fr_uid
        FOREIGN KEY (UID)        REFERENCES Account(UID) ON DELETE CASCADE,
    CONSTRAINT fk_fr_friend
        FOREIGN KEY (friend_UID) REFERENCES Account(UID) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='好友关系表(双向)';

-- ------------------------------------------------------------
-- 类型：id int unsigned | apply_type tinyint | sender_UID bigint unsigned | receiver_UID bigint unsigned | group_UID bigint unsigned | message varchar(255) | status tinyint | create_time datetime
-- 名字：id | apply_type | sender_UID | receiver_UID | group_UID | message | status | create_time
-- 说明：自增主键 | 申请类型(1好友 2群聊 3社区) | 发送方 | 接收方 | 目标群/社区ID | 申请附加留言 | 状态(0等待 1同意 2拒绝) | 申请时间
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS relation_apply (
    id           INT UNSIGNED    NOT NULL AUTO_INCREMENT COMMENT '自增主键',
    apply_type   TINYINT         NOT NULL DEFAULT 1 COMMENT '申请类型: 1=好友申请 2=群聊申请 3=社区申请',
    sender_UID   BIGINT UNSIGNED NOT NULL COMMENT '发送方',
    receiver_UID BIGINT UNSIGNED NOT NULL COMMENT '接收方',
    group_UID    BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '群聊/社区申请的目标ID(好友申请为0)',
    message      VARCHAR(255)    NULL     COMMENT '申请附加留言',
    status       TINYINT         NOT NULL DEFAULT 0 COMMENT '0等待 1同意 2拒绝',
    create_time  DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '申请时间',

    PRIMARY KEY (id),
    UNIQUE KEY uk_apply (apply_type, sender_UID, receiver_UID, group_UID, status),
    CONSTRAINT fk_ra_sender
        FOREIGN KEY (sender_UID)   REFERENCES Account(UID) ON DELETE CASCADE,
    CONSTRAINT fk_ra_receiver
        FOREIGN KEY (receiver_UID) REFERENCES Account(UID) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='关系申请表(好友/群聊/社区申请)';

-- ------------------------------------------------------------
-- 类型：group_UID bigint unsigned | name varchar(64) | owner_UID bigint unsigned | create_time datetime
-- 名字：group_UID | name | owner_UID | create_time
-- 说明：群ID(系统分配) | 群名称 | 创建者/群主 | 建群时间
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `Group` (
    group_UID   BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '群ID(系统分配)',
    name        VARCHAR(64)     NOT NULL                COMMENT '群名称',
    owner_UID   BIGINT UNSIGNED NOT NULL                COMMENT '创建者/群主(管理员)',
    create_time DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '建群时间',

    PRIMARY KEY (group_UID),
    KEY idx_owner (owner_UID),
    CONSTRAINT fk_group_owner
        FOREIGN KEY (owner_UID) REFERENCES Account(UID) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='群组表';

-- ------------------------------------------------------------
-- 类型：group_UID bigint unsigned | member_UID bigint unsigned | name varchar(64) | role enum('owner','member') | join_time datetime
-- 名字：group_UID | member_UID | name | role | join_time
-- 说明：所属群 | 成员 | 群内名字(默认取用户昵称) | 角色 | 入群时间
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS Groupmember (
    group_UID  BIGINT UNSIGNED NOT NULL COMMENT '所属群',
    member_UID BIGINT UNSIGNED NOT NULL COMMENT '成员',
    name       VARCHAR(64)     NOT NULL DEFAULT '' COMMENT '群内名字(默认取用户昵称,可修改)',
    role       ENUM('owner','member') NOT NULL DEFAULT 'member' COMMENT '角色',
    join_time  DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '入群时间',

    PRIMARY KEY (group_UID, member_UID),
    KEY idx_member (member_UID),
    CONSTRAINT fk_gm_group
        FOREIGN KEY (group_UID)  REFERENCES `Group`(group_UID) ON DELETE CASCADE,
    CONSTRAINT fk_gm_member
        FOREIGN KEY (member_UID) REFERENCES Account(UID)      ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='群成员表(联合主键)';

-- ------------------------------------------------------------
-- 类型：id bigint unsigned | type enum('private','group','channel') | sender_UID bigint unsigned | receiver_UID bigint unsigned | content text | is_file tinyint(1) | file_id bigint unsigned | reply_to_message_id bigint unsigned | send_time datetime
-- 名字：id | type | sender_UID | receiver_UID | content | is_file | file_id | reply_to_message_id | send_time
-- 说明：消息自增ID | 消息类型 | 发送者 | 接收方 | 消息内容 | 是否文件消息 | 文件ID(file.id) | 回复的原消息ID | 发送时间
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS message (
    id                  BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '消息自增ID(保持顺序)',
    type                ENUM('private','group','channel') NOT NULL DEFAULT 'private'
                                            COMMENT 'private=私聊 group=普通群聊 channel=社区频道',
    sender_UID          BIGINT UNSIGNED NOT NULL COMMENT '发送者',
    receiver_UID        BIGINT UNSIGNED NOT NULL COMMENT '接收方(私聊=对方UID;群聊=group_UID;频道=频道id)',
    content             TEXT            NOT NULL COMMENT '消息内容',
    is_file             TINYINT(1)      NOT NULL DEFAULT 0 COMMENT '是否文件消息(0=否 1=是)',
    file_id             BIGINT UNSIGNED NULL COMMENT '文件ID(file.id)',
    reply_to_message_id BIGINT UNSIGNED NULL COMMENT '回复的原消息id(NULL=非回复)',
    send_time           DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '发送时间',

    PRIMARY KEY (id),
    KEY idx_private (sender_UID, receiver_UID, id),
    KEY idx_group   (receiver_UID, id),
    KEY idx_sender  (sender_UID),
    KEY fk_message_file (file_id),
    CONSTRAINT fk_message_file
        FOREIGN KEY (file_id) REFERENCES file(id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='聊天记录表';

-- ------------------------------------------------------------
-- 类型：id bigint unsigned | name varchar(64) | core_channel_id bigint unsigned | is_core tinyint(1) | description varchar(255) | avatar varchar(128) | avatar_id bigint unsigned | banner_id bigint unsigned | owner_UID bigint unsigned | category varchar(64) | create_time datetime
-- 名字：id | name | core_channel_id | is_core | description | avatar | avatar_id | banner_id | owner_UID | category | create_time
-- 说明：社区/频道ID | 社区名/频道名 | 所属核心频道ID | 是否核心频道 | 社区简介 | 旧头像字段 | 社区头像文件ID(file.id) | 社区背景图文件ID(file.id) | 社区拥有者 | 频道分区 | 创建时间
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS community (
    id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '社区/频道ID',
    name            VARCHAR(64)     NOT NULL                COMMENT '社区名(core)/频道名(channel)',
    core_channel_id BIGINT UNSIGNED NULL                    COMMENT '普通频道→核心频道id；核心频道为NULL',
    is_core         TINYINT(1)      NOT NULL DEFAULT 0      COMMENT '1=核心频道(社区,隐藏不展示)',
    description     VARCHAR(255)    NULL                    COMMENT '社区简介(仅核心频道)',
    avatar          VARCHAR(128)    NULL                    COMMENT '社区头像文件ID字符串(旧字段保留)',
    avatar_id       BIGINT UNSIGNED NULL                    COMMENT '社区头像文件ID(file.id)',
    banner_id       BIGINT UNSIGNED NULL                    COMMENT '社区背景图文件ID(file.id)',
    owner_UID       BIGINT UNSIGNED NULL                    COMMENT '社区拥有者(仅核心频道)',
    category        VARCHAR(64)     NULL                    COMMENT '频道分区(普通频道,可空)',
    create_time     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',

    PRIMARY KEY (id),
    KEY idx_core_channel (core_channel_id),
    KEY fk_community_avatar (avatar_id),
    KEY fk_community_banner (banner_id),
    CONSTRAINT fk_community_core
        FOREIGN KEY (core_channel_id) REFERENCES community(id) ON DELETE CASCADE,
    CONSTRAINT fk_community_avatar
        FOREIGN KEY (avatar_id) REFERENCES file(id) ON DELETE SET NULL,
    CONSTRAINT fk_community_banner
        FOREIGN KEY (banner_id) REFERENCES file(id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='社区表(核心频道+普通频道)';

-- ------------------------------------------------------------
-- 类型：community_id bigint unsigned | user_UID bigint unsigned | nickname varchar(64) | role enum('owner','admin','member') | join_time datetime
-- 名字：community_id | user_UID | nickname | role | join_time
-- 说明：所属社区(核心频道id) | 成员UID | 社区内昵称 | 社区角色 | 加入时间
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS community_member (
    community_id BIGINT UNSIGNED NOT NULL COMMENT '所属社区(核心频道id)',
    user_UID     BIGINT UNSIGNED NOT NULL COMMENT '成员UID',
    nickname     VARCHAR(64)     NULL COMMENT '社区内昵称(NULL=用全局昵称)',
    role         ENUM('owner','admin','member') NOT NULL DEFAULT 'member' COMMENT '社区角色',
    join_time    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '加入时间',

    PRIMARY KEY (community_id, user_UID),
    KEY idx_user (user_UID),
    CONSTRAINT fk_cm_community
        FOREIGN KEY (community_id) REFERENCES community(id) ON DELETE CASCADE,
    CONSTRAINT fk_cm_user
        FOREIGN KEY (user_UID) REFERENCES Account(UID) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='社区成员表';

-- ------------------------------------------------------------
-- 类型：id bigint unsigned | uploader_uid bigint unsigned | storage_key varchar(191) | type enum('avatar','emoji','image','attachment','file') | original_name varchar(255) | mime_type varchar(128) | size bigint unsigned | hash char(64) | created_at datetime
-- 名字：id | uploader_uid | storage_key | type | original_name | mime_type | size | hash | created_at
-- 说明：文件唯一ID | 上传者UID | 磁盘文件唯一键 | 文件类别 | 原始文件名 | MIME类型 | 文件字节数 | 内容SHA256 | 上传时间
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
-- 类型：id bigint unsigned | file_id bigint unsigned | uploader_uid bigint unsigned | receiver_uid bigint unsigned | direction enum('upload','download') | status enum('uploading','downloading','paused','completed','cancelled','failed') | total_size bigint unsigned | transferred_size bigint unsigned | chunk_size int unsigned | chunk_count int unsigned | next_chunk_index int unsigned | received_bitmap varbinary(4096) | tmp_path varchar(512) | storage_key varchar(191) | file_type varchar(16) | original_name varchar(255) | mime_type varchar(128) | last_active_at datetime | created_at datetime | updated_at datetime
-- 名字：id | file_id | uploader_uid | receiver_uid | direction | status | total_size | transferred_size | chunk_size | chunk_count | next_chunk_index | received_bitmap | tmp_path | storage_key | file_type | original_name | mime_type | last_active_at | created_at | updated_at
-- 说明：传输会话ID | 目标文件 | 上传方 | 接收方 | 上传/下载 | 会话状态机 | 总字节数 | 已完成字节数 | 分片大小 | 总分片数 | 顺序断点块号 | 乱序重组位图 | 上传临时文件路径 | 上传目标storage_key | 文件类别 | 原始文件名 | MIME类型 | 最近活动时间 | 创建时间 | 更新时间
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
    storage_key       VARCHAR(191)    NULL                    COMMENT '上传目标 storage_key',
    file_type         VARCHAR(16)     NULL                    COMMENT '文件类别(avatar/emoji/image/attachment/file)',
    original_name     VARCHAR(255)    NULL                    COMMENT '原始文件名',
    mime_type         VARCHAR(128)    NULL                    COMMENT 'MIME类型',
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
