-- 与代码硬编码的 SQL 对齐：
--   登录/注册: ChatLoginHandler / ChatRegisterHandler -> users(id, username, password)
--   对话历史:  AIHelper::pushMessageToMysql / ChatServer::readDataFromMySQL -> chat_message
CREATE DATABASE IF NOT EXISTS ChatHttpServer
  DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE ChatHttpServer;

CREATE TABLE IF NOT EXISTS users (
  id       INT AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(255) NOT NULL UNIQUE,
  password VARCHAR(255) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 注意: INSERT 不写 pk，id 存的是 userId(可重复)，session_id 是纯数字字符串(无引号插入)
CREATE TABLE IF NOT EXISTS chat_message (
  pk         BIGINT AUTO_INCREMENT PRIMARY KEY,
  id         BIGINT NOT NULL,          -- userId
  username   VARCHAR(255) NOT NULL,
  session_id BIGINT NOT NULL,
  is_user    TINYINT NOT NULL,         -- 1=用户, 0=AI
  content    MEDIUMTEXT,
  ts         BIGINT NOT NULL,          -- 毫秒时间戳
  INDEX idx_user (id),
  INDEX idx_session (session_id),
  INDEX idx_ts (ts)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
