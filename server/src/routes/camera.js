const express = require('express');

// In-memory only - this is just "where did we last hear from the camera",
// not something that needs to survive a server restart.
let lastStatus = null;

module.exports = function cameraRouter() {
  const router = express.Router();

  router.post('/status', (req, res) => {
    const { ip } = req.body || {};
    if (typeof ip !== 'string' || !ip) {
      return res.status(400).json({ error: 'ip is required' });
    }
    lastStatus = { ip, updatedAt: new Date().toISOString() };
    res.status(204).end();
  });

  router.get('/status', (req, res) => {
    res.json(lastStatus);
  });

  return router;
};
