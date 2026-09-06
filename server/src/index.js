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

app.use('/api/sensors', require('./routes/sensors')(io));
app.use('/api/images', require('./routes/images')(io));
app.use('/api/dashboard', require('./routes/dashboard')());

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
