-- 给已有库的 Account 表补充 token 列（新库直接用 create_table.sql 已含该列）
ALTER TABLE Account ADD COLUMN token VARCHAR(64) NULL COMMENT '登录令牌(自动登录用,可空)' AFTER language;
