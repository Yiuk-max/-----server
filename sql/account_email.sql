-- ============================================================
-- 账户邮箱绑定表（用于"用邮箱登录"）
--
-- 背景：此前只能凭 UID 登录，UID 难记。邮箱与 UID 一对一绑定后，
--       登录既可用 UID 也可用邮箱；新用户注册必须填邮箱。
-- 说明：
--   - 不做邮箱验证/核对，仅要求唯一（邮箱做主键）。
--   - 一个账户一个邮箱（UID 唯一）；换绑由 repo 层 set_email 事务处理。
--   - 老账号无记录，可通过客户端的"设置邮箱"功能补绑。
-- 幂等：IF NOT EXISTS，可重复执行。
-- 执行： mysql -u<user> -p <db> < sql/account_email.sql
-- ============================================================

CREATE TABLE IF NOT EXISTS account_email (
    email        VARCHAR(190)    NOT NULL COMMENT '邮箱(登录用,唯一)',
    UID          BIGINT UNSIGNED NOT NULL COMMENT '绑定的账户UID',
    create_time  DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '绑定时间',
    update_time  DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                 ON UPDATE CURRENT_TIMESTAMP          COMMENT '更新时间',

    PRIMARY KEY (email),
    UNIQUE KEY uk_account_email_uid (UID),
    CONSTRAINT fk_email_uid
        FOREIGN KEY (UID) REFERENCES Account(UID) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='账户邮箱绑定表';
