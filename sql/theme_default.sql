-- ============================================================
-- Account.settings JSON 的 theme 默认值迁移：white → default
--
-- 背景：
--   老版本把主题默认写成 "white"，现已改为 "default"
--   （见 src/logic/account.h / src/db/repo/account_repo.cpp）。
--   本脚本把存量数据里的旧默认值统一改成 "default"，并补齐
--   NULL / 缺失 theme 键的 settings。
--
-- 幂等：JSON_SET 只改 $.theme 一个键，重复执行结果不变。
-- 说明：只改 "white" / 缺失 / NULL 三种情况，用户自定义主题（如 dark）不动。
--
-- 执行： mysql -u<user> -p chat_server < sql/theme_default.sql
-- ============================================================

USE chat_server;

UPDATE Account
SET settings = JSON_SET(COALESCE(settings, '{}'), '$.theme', 'default')
WHERE settings IS NULL
   OR JSON_UNQUOTE(JSON_EXTRACT(settings, '$.theme')) IS NULL
   OR JSON_UNQUOTE(JSON_EXTRACT(settings, '$.theme')) = 'white';
