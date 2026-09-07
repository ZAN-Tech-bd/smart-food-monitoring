const express = require('express');
const path = require('path');
const fs = require('fs');
const multer = require('multer');
const db = require('../db');
const { analyzeImage } = require('../services/gemini');

const uploadsDir = path.join(__dirname, '..', '..', 'uploads');
fs.mkdirSync(uploadsDir, { recursive: true });

const storage = multer.diskStorage({
  destination: (req, file, cb) => cb(null, uploadsDir),
  filename: (req, file, cb) => cb(null, `${Date.now()}.jpg`),
});
const upload = multer({ storage, limits: { fileSize: 8 * 1024 * 1024 } });

module.exports = function imagesRouter(io) {
  const router = express.Router();

  const getLatestReading = db.prepare('SELECT * FROM sensor_readings ORDER BY id DESC LIMIT 1');
  const insertImage = db.prepare(`
    INSERT INTO images (filepath, temperature, humidity, gas_raw, weight_g)
    VALUES (@filepath, @temperature, @humidity, @gas_raw, @weight_g)
  `);
  const updateVerdict = db.prepare(`
    UPDATE images SET gemini_verdict = ?, gemini_notes = ?, gemini_raw = ?, food_name = ? WHERE id = ?
  `);
  const getImage = db.prepare('SELECT * FROM images WHERE id = ?');
  const getImagesPage = db.prepare('SELECT * FROM images ORDER BY id DESC LIMIT ? OFFSET ?');
  const countImages = db.prepare('SELECT COUNT(*) AS count FROM images');
  const getLatestImage = db.prepare('SELECT * FROM images ORDER BY id DESC LIMIT 1');

  router.post('/', upload.single('image'), (req, res) => {
    if (!req.file) {
      return res.status(400).json({ error: 'multipart field "image" with a JPEG file is required' });
    }

    const snapshot = getLatestReading.get() || {};
    const relativePath = `/uploads/${req.file.filename}`;

    const info = insertImage.run({
      filepath: relativePath,
      temperature: snapshot.temperature ?? null,
      humidity: snapshot.humidity ?? null,
      gas_raw: snapshot.gas_raw ?? null,
      weight_g: snapshot.weight_g ?? null,
    });

    const imageRow = getImage.get(info.lastInsertRowid);
    io.emit('image:new', imageRow);
    res.status(201).json(imageRow);

    // Analyze asynchronously so the ESP32-CAM upload isn't held open waiting on Gemini.
    const absolutePath = path.join(uploadsDir, req.file.filename);
    analyzeImage(absolutePath, snapshot)
      .then(({ verdict, notes, raw, food }) => {
        updateVerdict.run(verdict, notes, raw, food, info.lastInsertRowid);
        const updated = getImage.get(info.lastInsertRowid);
        io.emit('image:analyzed', updated);
      })
      .catch((err) => {
        console.error('Unexpected error analyzing image:', err);
      });
  });

  // ?format=text returns a plain two-line body (verdict, then notes) instead of JSON -
  // used by the sensor node's LCD so it doesn't need a JSON parser on the device.
  router.get('/latest', (req, res) => {
    const image = getLatestImage.get();

    if (req.query.format === 'text') {
      res.type('text/plain');
      if (!image) {
        return res.send('No data\nNo images captured yet');
      }
      const verdict = image.gemini_verdict || 'Analyzing';
      const baseNotes = image.gemini_notes || 'Waiting for AI feedback';
      const notes = image.food_name ? `${image.food_name}: ${baseNotes}` : baseNotes;
      return res.send(`${verdict}\n${notes}`);
    }

    res.json(image || null);
  });

  router.get('/', (req, res) => {
    const limit = Math.min(Math.max(Number(req.query.limit) || 5, 1), 50);
    const page = Math.max(Number(req.query.page) || 1, 1);
    const offset = (page - 1) * limit;

    const total = countImages.get().count;
    const totalPages = Math.max(Math.ceil(total / limit), 1);
    const items = getImagesPage.all(limit, offset);

    res.json({ items, page, limit, total, totalPages });
  });

  return router;
};
