const path = require('path');
const fs = require('fs');
const Database = require('better-sqlite3');

const dataDir = path.join(__dirname, '..', 'data');
fs.mkdirSync(dataDir, { recursive: true });

const db = new Database(path.join(dataDir, 'food-monitoring.sqlite'));
db.pragma('journal_mode = WAL');

db.exec(`
  CREATE TABLE IF NOT EXISTS sensor_readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    temperature REAL,
    humidity REAL,
    gas_raw INTEGER,
    weight_g REAL,
    created_at TEXT DEFAULT (datetime('now'))
  );

  CREATE TABLE IF NOT EXISTS images (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    filepath TEXT NOT NULL,
    temperature REAL,
    humidity REAL,
    gas_raw INTEGER,
    weight_g REAL,
    gemini_verdict TEXT,
    gemini_notes TEXT,
    gemini_raw TEXT,
    created_at TEXT DEFAULT (datetime('now'))
  );
`);

module.exports = db;
