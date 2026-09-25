-- ============================================================
-- 社区功能落地 —— 数据库迁移脚本
--
-- 设计原则：
--   1. 社区【独立一张表】community，与普通群聊的 Group 彻底分开。
--   2. community 表同时装两类行：
--        - 核心频道（is_core=1）：社区本身，隐藏不展示，
--          承载社区描述(description)、社区头像(avatar)、社区拥有者(owner_UID)。
--        - 普通频道（is_core=0）：社区下的聊天频道，
--          description/avatar/owner_UID 为 NULL，
--          用 core_channel_id 指向所属社区的核心频道。
--   3. 社区成员用【独立一张表】community_member。
--        - 社区成员挂在核心频道上（community_id = 核心频道id），一份共享。
--        - 普通频道不维护自己的成员表，成员继承其 core_channel_id 指向的社区。
--   4. 普通群聊的 Group / Groupmember 完全不动。
--   5. message.type 改为英文枚举：private=私聊 group=普通群聊 channel=社区频道。
--
-- 实体关系：
--   社区      = community(is_core=1)                    -- 隐藏核心频道
--   频道      = community(is_core=0, core_channel_id=社区id)
--   普通群    = Group（原表，不动）
--
--   社区成员  = community_member(community_id = 核心频道id)  <-- 一份，所有频道共享
--   频道成员  = 不单独存，查其 core_channel_id 指向的社区成员
--   普通群成员= Groupmember（原表，不动）
--
-- 字段说明：
--   Account.avatar_file_id        : 用户头像（预留，二期文件传输）
--   community.name                : 社区名(core) / 频道名(channel)
--   community.core_channel_id     : 普通频道→核心频道id；核心频道为 NULL
--   community.is_core             : 1=核心频道(社区，隐藏不展示)
--   community.description         : 社区简介（仅核心频道）
--   community.avatar              : 社区头像 file_id（仅核心频道，预留）
--   community.owner_UID           : 社区拥有者（仅核心频道）
--   community.category            : 频道分区（普通频道，可空）
--   community_member.community_id : 所属社区（=核心频道id）
--   community_member.nickname     : 社区内昵称（NULL=全局昵称）
--   community_member.role         : 社区角色 owner/admin/member
--
-- 注意事项：
--   1. MySQL 8 不支持 ADD COLUMN IF NOT EXISTS，本脚本【只需执行一次】。
--   2. 删除社区（核心频道）时，其普通频道会因 fk_community_core 的
--      ON DELETE CASCADE 一并删除；community_member 也会级联删除。
--      但 message 表无外键级联，删社区前 repo 层需先删这些频道的聊天记录
--      （message.type='channel' 且 receiver_UID=频道id）。
--   3. 老库执行前建议备份：  mysqldump -u<user> -p chat_server > backup.sql
--
-- 执行命令（在仓库根目录 server/ 下）：
--   sudo mysql chat_server < sql/community.sql
-- ============================================================

USE chat_server;

-- ------------------------------------------------------------
-- 1. Account 增加用户头像字段（预留，二期文件传输）
-- ------------------------------------------------------------
ALTER TABLE Account
    ADD COLUMN avatar_file_id VARCHAR(128) NULL
        COMMENT '用户头像文件ID(预留,二期文件传输)' AFTER token;

-- ------------------------------------------------------------
-- 2. 社区表 community（独立表：核心频道 + 普通频道）
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS community (
    id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '社区/频道ID',
    name            VARCHAR(64)     NOT NULL                COMMENT '社区名(core)/频道名(channel)',
    core_channel_id BIGINT UNSIGNED NULL                    COMMENT '普通频道→核心频道id；核心频道为NULL',
    is_core         TINYINT(1)      NOT NULL DEFAULT 0      COMMENT '1=核心频道(社区,隐藏不展示)',
    description     VARCHAR(255)    NULL                    COMMENT '社区简介(仅核心频道)',
    avatar          VARCHAR(128)    NULL                    COMMENT '社区头像文件ID(仅核心频道,预留,二期文件传输)',
    owner_UID       BIGINT UNSIGNED NULL                    COMMENT '社区拥有者(仅核心频道)',
    category        VARCHAR(64)     NULL                    COMMENT '频道分区(普通频道,可空)',
    create_time     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',

    PRIMARY KEY (id),
    KEY idx_core_channel (core_channel_id),
    CONSTRAINT fk_community_core
        FOREIGN KEY (core_channel_id) REFERENCES community(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='社区表(核心频道+普通频道)';

-- ------------------------------------------------------------
-- 3. 社区成员表 community_member（独立表）
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
-- 4. message.type 由数字改为英文枚举（保留老数据：1→private, 2→group）
--    private=私聊 group=普通群聊 channel=社区频道
-- ------------------------------------------------------------
-- 先转成 VARCHAR，把老数字值映射成英文，再收紧为 ENUM
ALTER TABLE message
    MODIFY COLUMN type VARCHAR(16) NOT NULL DEFAULT 'private'
        COMMENT 'private=私聊 group=普通群聊 channel=社区频道';

UPDATE message SET type = CASE type
    WHEN '1' THEN 'private'
    WHEN '2' THEN 'group'
    ELSE 'private' END;

ALTER TABLE message
    MODIFY COLUMN type ENUM('private','group','channel') NOT NULL DEFAULT 'private'
        COMMENT 'private=私聊 group=普通群聊 channel=社区频道';
