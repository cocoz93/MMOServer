-- ==========================================================================
-- MMO Zone Server - character persistence (Stage 1: save pipeline)
--
-- Apply once before running the server with USE_DB_WORKER=1:
--   "C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe" -u root -p < schema.sql
--
-- Model: server memory is the source of truth, DB is a store.
--        The server flushes only "dirty" (changed) players periodically.
-- ==========================================================================

CREATE DATABASE IF NOT EXISTS mmo DEFAULT CHARACTER SET utf8mb4;
USE mmo;

CREATE TABLE IF NOT EXISTS characters (
    account_id BIGINT      NOT NULL PRIMARY KEY,
    x          FLOAT       NOT NULL DEFAULT 0,
    y          FLOAT       NOT NULL DEFAULT 0,
    map_id     INT         NOT NULL DEFAULT 0,
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
                           ON UPDATE CURRENT_TIMESTAMP(3)
) ENGINE=InnoDB;

-- Queries used by CDBWorker (numeric-only params -> no injection surface):
--   Save (UPSERT):
--     INSERT INTO characters (account_id,x,y,map_id) VALUES (?,?,?,?)
--     ON DUPLICATE KEY UPDATE x=?, y=?, map_id=?
--   Load (Stage 2):
--     SELECT x,y,map_id FROM characters WHERE account_id=?
