-- ============================================================
-- 方案 B：全链路统一 UTC 存储
-- ------------------------------------------------------------
-- 原理：
--   后端 C++ 用 gmtime 生成 last_login_time（UTC），
--   parse_datetime 按 UTC 解析（无时区依赖）；
--   MySQL 的 NOW()/CURRENT_TIMESTAMP 固定为 UTC；
--   前端拿到 "YYYY-MM-DD HH:MM:SS"(UTC) 后转浏览器本地时区显示。
--
-- 步骤：
--   1) 在 /etc/mysql/mysql.conf.d/mysqld.cnf 的 [mysqld] 段加一行：
--        default-time-zone = '+00:00'
--      然后重启：sudo systemctl restart mysql
--      （该配置必须写 my.cnf 才能持久化，SET GLOBAL 重启即失效）
--   2) 执行本脚本做历史数据迁移（见下方「迁移前必须先确认」）。
-- ============================================================

-- ------------------------------------------------------------
-- 迁移前必须先确认（只读，不执行）：
--   SELECT NOW(), UTC_TIMESTAMP();
--   SELECT id, send_time FROM message ORDER BY id DESC LIMIT 1;
--
-- 判断：
--   - 若 send_time 与 UTC_TIMESTAMP() 一致   => 历史数据已是 UTC，跳过本脚本。
--   - 若 send_time 比 UTC_TIMESTAMP() 快 8 小时（北京时间）=> 执行下面的 UPDATE。
--
-- 依赖：CONVERT_TZ 需要 MySQL 时区表。若报 "Unknown or incorrect time zone"，
--       先加载一次时区表：
--         mysql_tzinfo_to_sql /usr/share/zoneinfo | mysql -uroot -p mysql
--
-- 强烈建议执行前备份：
--   mysqldump -uroot -p chat_server > chat_server_backup_$(date +%F).sql
-- ============================================================

USE chat_server;

-- 消息时间（前端直接展示的核心字段）
UPDATE message
   SET send_time = CONVERT_TZ(send_time, '+08:00', '+00:00')
 WHERE send_time IS NOT NULL;

-- 文件元数据
UPDATE file
   SET created_at = CONVERT_TZ(created_at, '+08:00', '+00:00')
 WHERE created_at IS NOT NULL;

-- 文件传输会话
UPDATE file_transfer
   SET created_at     = CONVERT_TZ(created_at,     '+08:00', '+00:00'),
       updated_at     = CONVERT_TZ(updated_at,     '+08:00', '+00:00'),
       last_active_at = CONVERT_TZ(last_active_at, '+08:00', '+00:00')
 WHERE created_at IS NOT NULL;

-- 账号表 Account.settings JSON 内的 last_login_time（跳过空串，避免 CONVERT_TZ 对空串返回 NULL）
UPDATE Account
   SET settings = JSON_SET(settings, '$.last_login_time',
           DATE_FORMAT(CONVERT_TZ(JSON_UNQUOTE(JSON_EXTRACT(settings, '$.last_login_time')), '+08:00', '+00:00'), '%Y-%m-%d %H:%i:%s'))
 WHERE JSON_UNQUOTE(JSON_EXTRACT(settings, '$.last_login_time')) <> '';

-- 其余表的 create_time / update_time / join_time（注册时间、好友时间、入群时间等）
-- 如仅做内部逻辑比较、不精确展示给用户，可暂不迁移；如需迁移，按同样的 CONVERT_TZ 模式逐表处理。
