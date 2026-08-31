-- ============================================================
-- 迁移脚本：允许昵称重复（登录/加好友/发消息均按 UID 定位，昵称无需唯一）
-- 适用：已按旧版 create_table.sql 建库的现有数据库（Account.nickname 上有 uk_nickname 唯一索引）
-- 执行：使用具有 ALTER 权限的账号（如 root）执行本文件
--       mysql -u root -p chat_server < allow_duplicate_nickname.sql
-- 幂等说明：若索引已不存在，MySQL 8.0 会报错；可忽略，或先执行下面的检查语句确认。
-- ============================================================

USE chat_server;

-- 删除昵称唯一索引，允许不同用户使用相同昵称（密码本就无唯一约束）
ALTER TABLE Account DROP INDEX uk_nickname;

-- 验证：应只剩 PRIMARY（UID）索引
SHOW INDEX FROM Account;
