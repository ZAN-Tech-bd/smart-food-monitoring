const express = require('express');
const db = require('../db');

module.exports = function sensorsRouter(io) {
  const router = express.Router();

  const insertReading = db.prepare(`
    INSERT INTO sensor_readings (temperature, humidity, gas_raw, weight_g)
    VALUES (@temperature, @humidity, @gas_raw, @weight_g)
  `);
  const getLatest = db.prepare('SELECT * FROM sensor_readings ORDER BY id DESC LIMIT 1');
  const getHistory = db.prepare('SELECT * FROM sensor_readings ORDER BY id DESC LIMIT ?');

  router.post('/', (req, res) => {
    const { temperature, humidity, gas_raw, weight_g } = req.body || {};

    if ([temperature, humidity, gas_raw, weight_g].some((v) => v === undefined || v === null || Number.isNaN(Number(v)))) {
      return res.status(400).json({ error: 'temperature, humidity, gas_raw, and weight_g are all required numbers' });
    }

    const info = insertReading.run({
      temperature: Number(temperature),
      humidity: Number(humidity),
      gas_raw: Number(gas_raw),
      weight_g: Number(weight_g),
    });

    const reading = db.prepare('SELECT * FROM sensor_readings WHERE id = ?').get(info.lastInsertRowid);
    io.emit('sensor:update', reading);
    res.status(201).json(reading);
  });

  router.get('/latest', (req, res) => {
    res.json(getLatest.get() || null);
  });

  router.get('/history', (req, res) => {
    const limit = Math.min(Number(req.query.limit) || 50, 500);
    const rows = getHistory.all(limit);
    res.json(rows.reverse());
  });

  return router;
};
