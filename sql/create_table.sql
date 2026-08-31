-- ============================================================
-- 聊天服务器 数据库建表脚本（以当前数据库实际结构为准）
-- 数据库：MySQL 8.0（当前库 collate 为 utf8mb4_0900_ai_ci）
-- 字符集：utf8mb4（必须，否则中文/emoji 会乱码或插入失败）
-- 本脚本与线上 chat_server 库的结构一致，包含 6 张表：
--   Account / friend_relation / relation_apply / `Group` / Groupmember / message
-- 说明：
--   1. friend_relation 采用"双向同步"（应用层在事务内双写两行）
--   2. friend_request 已更名扩展为 relation_apply（apply_type 区分好友/群聊申请），
--      唯一约束 uk_apply 防重复"等待中"记录
--   3. Groupmember 含 name 列（群内名字，默认取用户昵称，可修改）
--   4. UID 采用 AUTO_INCREMENT 跟随系统分配，
--      启动时应用按 MAX(UID)/MAX(group_UID) 重新定位内存分配器（见底部说明）
-- ============================================================

CREATE DATABASE IF NOT EXISTS chat_server
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_0900_ai_ci;

USE chat_server;

-- ------------------------------------------------------------
-- 1. 账户表 Account
--    nickname 不设唯一约束（登录/加好友/发消息均按 UID 定位，昵称允许重复；
--    密码也允许重复）
--    settings 用 JSON 字符串承载（应用层序列化），避免拆表
--    UID：AUTO_INCREMENT 由系统分配，启动时按 MAX(UID) 重定位内存分配器
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS Account (
    UID         BIGINT UNSIGNED    NOT NULL AUTO_INCREMENT COMMENT '用户唯一ID(系统分配)',
    password    VARCHAR(128)       NOT NULL                COMMENT '密码(建议存储hash)',
    nickname    VARCHAR(64)        NOT NULL                COMMENT '昵称',
    settings    JSON               NULL                    COMMENT '账号设置(主题/语言/通知开关等,JSON序列化)',
    language    VARCHAR(16)        NOT NULL DEFAULT 'Chinese' COMMENT '语言',
    create_time DATETIME           NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '注册时间',
    birthday    DATE               NULL                    COMMENT '生日(可空)',

    PRIMARY KEY (UID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='用户账户表';

-- ------------------------------------------------------------
-- 2. 好友关系表 friend_relation
--    双向同步：逻辑上 AB 互为好友后需存 (A,B) 与 (B,A) 两行（由 repo 应用层
--          在事务内双写），查询任一方好友列表只需 WHERE UID=? 即可，无需双向判断。
--    联合主键 (UID, friend_UID)
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS friend_relation (
    UID          BIGINT UNSIGNED NOT NULL COMMENT '用户A',
    friend_UID   BIGINT UNSIGNED NOT NULL COMMENT '好友B',
    remark_name  VARCHAR(64)     NULL     COMMENT '备注名(可空)',
    create_time  DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '成为好友时间',

    PRIMARY KEY (UID, friend_UID),
    KEY idx_friend_uid (friend_UID),
    CONSTRAINT fk_fr_uid
        FOREIGN KEY (UID)        REFERENCES Account(UID) ON DELETE CASCADE,
    CONSTRAINT fk_fr_friend
        FOREIGN KEY (friend_UID) REFERENCES Account(UID) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='好友关系表(双向)';

-- 关于"双向同步"的落地（详见本文件底部说明）：
--   这里【不】使用 DB 触发器（DELIMITER 语法只能通过 mysql 命令行 source 执行，
--   在 JDBC/Connector 逐条执行时会导致语法错误）。
--   改为在应用层 repo 实现里"双写两行"：建立好友时同时插入 (A,B) 与 (B,A)，
--   删除时同时删除两行，并用事务保证原子性。详细写法见底部【应用层双向同步实现】。
-- ------------------------------------------------------------
-- 3. 关系申请表 relation_apply（原 friend_request 更名扩展）
--    一张表承载两类申请：
--      apply_type=1 好友申请：sender_UID 向 receiver_UID 申请（group_UID=0）
--      apply_type=2 群聊申请：sender_UID 申请加入 group_UID 群，receiver_UID=群主/管理员
--    唯一约束 uk_apply(apply_type, sender_UID, receiver_UID, group_UID, status)：
--          好友申请按 (1,A,B,0,0)、群聊申请按 (2,A,群主,群,0) 各自防重复"等待中"记录。
--    status: 0=等待  1=同意(处理完删除)  2=拒绝(处理完删除)
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS relation_apply (
    id           INT UNSIGNED    NOT NULL AUTO_INCREMENT COMMENT '自增主键',
    apply_type   TINYINT         NOT NULL DEFAULT 1 COMMENT '申请类型: 1=好友申请 2=群聊申请',
    sender_UID   BIGINT UNSIGNED NOT NULL COMMENT '申请人',
    receiver_UID BIGINT UNSIGNED NOT NULL COMMENT '接收方(好友申请=对方用户;群聊申请=群主/管理员)',
    group_UID    BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '群聊申请的目标群(好友申请为0)',
    message      VARCHAR(255)    NULL     COMMENT '申请附加留言',
    status       TINYINT         NOT NULL DEFAULT 0 COMMENT '0等待 1同意 2拒绝',
    create_time  DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '申请时间',

    PRIMARY KEY (id),
    UNIQUE KEY uk_apply (apply_type, sender_UID, receiver_UID, group_UID, status),
    CONSTRAINT fk_ra_sender
        FOREIGN KEY (sender_UID)   REFERENCES Account(UID) ON DELETE CASCADE,
    CONSTRAINT fk_ra_receiver
        FOREIGN KEY (receiver_UID) REFERENCES Account(UID) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='关系申请表(好友/群聊申请)';

-- ------------------------------------------------------------
-- 4. 群组表 `Group`
--    group_UID 与 Account.UID 可能同数值但语义不同，群组 ID 使用独立自增计数器。
--    owner_UID 外键到 Account。
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
-- 5. 群成员表 Groupmember
--    主键为联合主键 (group_UID, member_UID)（一个用户可加入多个群）
--    role: 'owner'=群主(创建者) / 'member'=普通成员（支持后续扩展 'admin'）
--    name: 成员在群内的名字（群名片）。默认取用户昵称(Account.nickname)，
--          由 repo 层在插入成员时 JOIN Account 填充（见 repo/group_repo.cpp），
--          之后可单独修改，不影响全局昵称。
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS Groupmember (
    group_UID   BIGINT UNSIGNED NOT NULL COMMENT '所属群',
    member_UID  BIGINT UNSIGNED NOT NULL COMMENT '成员',
    name        VARCHAR(64)     NOT NULL DEFAULT '' COMMENT '群内名字(默认取用户昵称,可修改)',
    role        ENUM('owner','member') NOT NULL DEFAULT 'member' COMMENT '角色',
    join_time   DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '入群时间',

    PRIMARY KEY (group_UID, member_UID),
    KEY idx_member (member_UID),
    CONSTRAINT fk_gm_group
        FOREIGN KEY (group_UID)  REFERENCES `Group`(group_UID) ON DELETE CASCADE,
    CONSTRAINT fk_gm_member
        FOREIGN KEY (member_UID) REFERENCES Account(UID)      ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='群成员表(联合主键)';

-- ------------------------------------------------------------
-- 6. 聊天记录表 message
--    需求：持久化私聊/群聊消息，用于历史记录/离线补发。
--    设计：type=1 私聊 / type=2 群聊
--          私聊时只写一条 (sender, receiver)，查询两人会话用
--              WHERE ((sender=:me AND receiver=:other) OR (sender=:other AND receiver=:me))
--          群聊时写一条 (sender, receiver=group_UID)
--    id：自增主键保持发送顺序，方便按 id 分页拉取历史。
--    说明：不做文件表（本次需求明确"补消息不补文件"）。
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS message (
    id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '消息自增ID(保持顺序)',
    type         TINYINT         NOT NULL                COMMENT '1=私聊 2=群聊',
    sender_UID   BIGINT UNSIGNED NOT NULL                COMMENT '发送者',
    receiver_UID BIGINT UNSIGNED NOT NULL                COMMENT '接收方(私聊=对方UID;群聊=group_UID)',
    content      TEXT            NOT NULL                COMMENT '消息内容',
    send_time    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '发送时间',

    PRIMARY KEY (id),
    -- 私聊会话查询索引
    KEY idx_private (sender_UID, receiver_UID, id),
    -- 群聊历史索引
    KEY idx_group (receiver_UID, id),
    KEY idx_sender (sender_UID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='聊天记录表';

-- ============================================================
-- 【应用层双向同步实现】参考（friend_relation 双写）
-- ============================================================
-- 不建议用 DB 触发器（DELIMITER 语法无法在 JDBC/Connector 逐条执行）。
-- 应在 repo 实现里用"事务双写"保证 (A,B) 与 (B,A) 一致。
--
--   // 建立好友 A<->B（事务内）
--   START TRANSACTION;
--   INSERT INTO friend_relation (UID, friend_UID, remark_name) VALUES (:A, :B, :remark);
--   INSERT INTO friend_relation (UID, friend_UID, remark_name) VALUES (:B, :A, :remark);
--   COMMIT;
--
--   // 删除好友（事务内，两条一起删）
--   START TRANSACTION;
--   DELETE FROM friend_relation WHERE UID=:A AND friend_UID=:B;
--   DELETE FROM friend_relation WHERE UID=:B AND friend_UID=:A;
--   COMMIT;
--
--   查询 A 的好友列表：SELECT friend_UID FROM friend_relation WHERE UID = :A;
--   （同理删除账户的外键 ON DELETE CASCADE 会自动清理两端关系行）
-- ============================================================

-- ============================================================
-- 关于 UID 分配与"初始值读取数据库后重新定位"的落地说明
-- ============================================================
-- 现网 C++ 侧使用内存单例 UID_allocator 自增分配 UID/group_id。
-- 引入 MySQL 后必须保证重启不撞号，两种方案：
--
-- 【方案A - 推荐，改造成本低】
--   Account.UID 与 Group.group_UID 已设为 AUTO_INCREMENT，
--   应用在启动时(连接DB后、开始 accept 前)执行：
--     SELECT COALESCE(MAX(UID),0)+1       FROM Account;  -- 回填 uid 分配起点
--     SELECT COALESCE(MAX(group_UID),0)+1 FROM `Group`;  -- 回填 group 分配起点
--   用结果重定位 UID_allocator::current_uid / current_group_id，
--   之后仍用内存分配器快速分配，避免每次落库取 MAX 的性能开销。
--
-- 【方案B - 彻底弃用内存分配器】
--   插入前直接让 DB 生成 AUTO_INCREMENT 主键，再通过
--   LAST_INSERT_ID() 取回。省去内存分配器，但每注册/建群都要一次 DB round trip。
--
-- 建议：登录/注册/建群是低频操作，方案B最简单可靠；
--       若想保持现有分配器设计，则用方案A在启动时重定位。
--
-- 关于"用户UID与群UID共用同一套编号空间"的说明：
--   这是【有意为之、无歧义】的设计。C++ 侧 request_uid() 与 request_group_id()
--   是两个独立计数器，确实可能让某个用户UID与某个群group_UID 数值相同(撞号)。
--   但消息协议每条都带 type 字段(如 private_chat / group_chat)，分发器
--   Chat_handler 先按 type 路由到 session.private_chat() 或 session.group_chat()，
--   两者内部各自只查 account_manager 或 Group_manager，两个查找空间天然隔离，
--   因此撞号不会造成转发歧义。持久化时同样靠 message.type 区分，安全。
-- ============================================================
