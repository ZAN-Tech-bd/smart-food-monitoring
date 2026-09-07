require('dotenv').config();

const path = require('path');
const express = require('express');
const cors = require('cors');
const http = require('http');
const { Server } = require('socket.io');

const app = express();
const server = http.createServer(app);
const io = new Server(server);

app.use(cors());
app.use(express.json());
app.use('/uploads', express.static(path.join(__dirname, '..', 'uploads')));
app.use('/static', express.static(path.join(__dirname, '..', 'public')));

app.set('view engine', 'ejs');
app.set('views', path.join(__dirname, '..', 'views'));

const statusRouter = require('./routes/status')(io);

app.use('/api/sensors', require('./routes/sensors')(io));
app.use('/api/images', require('./routes/images')(io));
app.use('/api/dashboard', require('./routes/dashboard')(statusRouter.getCombinedStatus));
app.use('/api/status', statusRouter);

// Fills the gap between camera captures: periodically asks Gemini for a
// simple Good/Not Good read on sensor data alone, but only when there's no
// recent photo-based verdict already covering that window (see status.js).
const AI_STATUS_INTERVAL_MS = Number(process.env.AI_STATUS_INTERVAL_MS) || 120000;
setInterval(() => statusRouter.runSensorOnlyCheck(), AI_STATUS_INTERVAL_MS);

app.get('/', (req, res) => {
  res.render('dashboard');
});

io.on('connection', (socket) => {
  console.log('Dashboard client connected:', socket.id);
});

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`Smart Food Monitoring server running at http://localhost:${PORT}`);
});
