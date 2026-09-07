const express = require('express');
const db = require('../db');
const { analyzeSensorsOnly } = require('../services/groq');
const geminiBudget = require('../services/geminiBudget');

// Sensor-only checks run on Groq (generous free daily limit, text-only - no
// vision needed for this), completely separate from Gemini's tight image
// analysis quota, so this can run frequently.
const STATUS_CHECK_INTERVAL_MS = Number(process.env.AI_STATUS_INTERVAL_MS) || 60000;

module.exports = function statusRouter(io) {
  const router = express.Router();

  const getLatestReading = db.prepare('SELECT * FROM sensor_readings ORDER BY id DESC LIMIT 1');
  const getLatestImage = db.prepare('SELECT * FROM images ORDER BY id DESC LIMIT 1');
  const getLatestAiStatus = db.prepare('SELECT * FROM ai_status ORDER BY id DESC LIMIT 1');
  const insertAiStatus = db.prepare(`
    INSERT INTO ai_status (verdict, notes, temperature, humidity, gas_raw, weight_g)
    VALUES (@verdict, @notes, @temperature, @humidity, @gas_raw, @weight_g)
  `);
  const getAiStatusById = db.prepare('SELECT * FROM ai_status WHERE id = ?');

  function getCombinedStatus() {
    const latestImage = getLatestImage.get();
    const latestSensorStatus = getLatestAiStatus.get();

    const imageTime = latestImage && latestImage.gemini_verdict
      ? new Date(`${latestImage.created_at}Z`).getTime()
      : -Infinity;
    const sensorTime = latestSensorStatus
      ? new Date(`${latestSensorStatus.created_at}Z`).getTime()
      : -Infinity;

    if (imageTime === -Infinity && sensorTime === -Infinity) return null;

    if (imageTime >= sensorTime) {
      return {
        source: 'image',
        verdict: latestImage.gemini_verdict,
        notes: latestImage.gemini_notes,
        created_at: latestImage.created_at,
        imageId: latestImage.id,
      };
    }

    return {
      source: 'sensor',
      verdict: latestSensorStatus.verdict,
      notes: latestSensorStatus.notes,
      created_at: latestSensorStatus.created_at,
    };
  }

  // Runs on a timer from index.js. Uses Groq (see services/groq.js), so it
  // doesn't compete with Gemini's image-analysis budget - never throws, a
  // failed check just gets logged and skipped.
  async function runSensorOnlyCheck() {
    try {
      const latestReading = getLatestReading.get();
      if (!latestReading) return; // no sensor data yet, nothing to check

      const { verdict, notes } = await analyzeSensorsOnly(latestReading);
      const info = insertAiStatus.run({
        verdict,
        notes,
        temperature: latestReading.temperature,
        humidity: latestReading.humidity,
        gas_raw: latestReading.gas_raw,
        weight_g: latestReading.weight_g,
      });

      const row = getAiStatusById.get(info.lastInsertRowid);
      io.emit('status:update', { source: 'sensor', ...row, budget: geminiBudget.getStatus() });
    } catch (err) {
      console.error('Sensor-only AI status check failed:', err);
    }
  }

  router.get('/latest', (req, res) => {
    res.json(getCombinedStatus());
  });

  router.get('/budget', (req, res) => {
    res.json(geminiBudget.getStatus());
  });

  router.getCombinedStatus = getCombinedStatus;
  router.runSensorOnlyCheck = runSensorOnlyCheck;
  router.getBudgetStatus = geminiBudget.getStatus;

  return router;
};
