const express = require('express');
const db = require('../db');

module.exports = function dashboardRouter(getCombinedStatus) {
  const router = express.Router();

  const getLatestReading = db.prepare('SELECT * FROM sensor_readings ORDER BY id DESC LIMIT 1');
  const getLatestImage = db.prepare('SELECT * FROM images ORDER BY id DESC LIMIT 1');
  const getHistory = db.prepare('SELECT * FROM sensor_readings ORDER BY id DESC LIMIT ?');

  router.get('/summary', (req, res) => {
    res.json({
      latestReading: getLatestReading.get() || null,
      latestImage: getLatestImage.get() || null,
      // With sensors posting every ~1s, 300 points covers roughly the last 5 minutes.
      history: getHistory.all(300).reverse(),
      currentStatus: getCombinedStatus(),
      thresholds: {
        gasWarning: Number(process.env.GAS_WARNING_THRESHOLD) || 1500,
        gasDanger: Number(process.env.GAS_DANGER_THRESHOLD) || 2800,
      },
    });
  });

  return router;
};
