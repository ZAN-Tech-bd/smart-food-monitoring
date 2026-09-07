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

    const t = Number(temperature);
    const h = Number(humidity);
    const g = Number(gas_raw);
    const w = Number(weight_g);

    // Sane physical bounds — catches things like a DHT sensor misread (e.g. a DHT11
    // decoded with the DHT22 byte layout inflates every reading by ~25.6x) before
    // it ever reaches the database or the dashboard.
    if (t < -40 || t > 125) {
      return res.status(400).json({ error: 'temperature out of expected range (-40 to 125 C)' });
    }
    if (h < 0 || h > 100) {
      return res.status(400).json({ error: 'humidity out of expected range (0-100%)' });
    }
    if (g < 0 || g > 4095) {
      return res.status(400).json({ error: 'gas_raw out of expected ADC range (0-4095)' });
    }
    if (w < -1000 || w > 1000000) {
      return res.status(400).json({ error: 'weight_g out of expected range' });
    }

    const info = insertReading.run({ temperature: t, humidity: h, gas_raw: g, weight_g: w });

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
