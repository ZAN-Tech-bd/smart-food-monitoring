const socket = io();
const statusPill = document.getElementById('connection-status');

socket.on('connect', () => {
  statusPill.textContent = 'live';
  statusPill.classList.add('connected');
});
socket.on('disconnect', () => {
  statusPill.textContent = 'disconnected';
  statusPill.classList.remove('connected');
});

let thresholds = { gasWarning: 1500, gasDanger: 2800 };
let chart;

function gasBadgeClass(raw) {
  if (raw >= thresholds.gasDanger) return 'danger';
  if (raw >= thresholds.gasWarning) return 'warn';
  return 'ok';
}
function gasLabel(raw) {
  if (raw >= thresholds.gasDanger) return 'Danger';
  if (raw >= thresholds.gasWarning) return 'Warning';
  return 'Normal';
}
function verdictBadgeClass(verdict) {
  if (verdict === 'Fresh') return 'ok';
  if (verdict === 'Caution') return 'warn';
  if (verdict === 'Spoiled') return 'danger';
  return '';
}

function renderReading(reading) {
  if (!reading) return;
  document.getElementById('card-temperature').textContent = `${reading.temperature.toFixed(1)} C`;
  document.getElementById('card-humidity').textContent = `${reading.humidity.toFixed(0)} %`;
  document.getElementById('card-weight').textContent = `${reading.weight_g.toFixed(0)} g`;

  const gasBadge = document.getElementById('card-gas-badge');
  document.getElementById('card-gas').textContent = reading.gas_raw;
  gasBadge.textContent = gasLabel(reading.gas_raw);
  gasBadge.className = `badge ${gasBadgeClass(reading.gas_raw)}`;
}

function renderImage(image) {
  if (!image) return;
  document.getElementById('latest-image').src = image.filepath;
  document.getElementById('image-timestamp').textContent = new Date(image.created_at + 'Z').toLocaleString();

  const verdictBadge = document.getElementById('verdict-badge');
  const notes = document.getElementById('verdict-notes');

  if (!image.gemini_verdict) {
    verdictBadge.textContent = 'Analyzing...';
    verdictBadge.className = 'badge';
    notes.textContent = 'Waiting for Gemini feedback.';
  } else {
    verdictBadge.textContent = image.gemini_verdict;
    verdictBadge.className = `badge ${verdictBadgeClass(image.gemini_verdict)}`;
    notes.textContent = image.gemini_notes || '';
  }
}

function prependGalleryItem(image) {
  const gallery = document.getElementById('gallery');
  const existing = document.getElementById(`gallery-item-${image.id}`);
  if (existing) {
    updateGalleryItem(existing, image);
    return;
  }

  const item = document.createElement('div');
  item.className = 'gallery-item';
  item.id = `gallery-item-${image.id}`;
  updateGalleryItem(item, image);
  gallery.prepend(item);
}

function updateGalleryItem(el, image) {
  el.innerHTML = `
    <img src="${image.filepath}" alt="capture" />
    <div class="gallery-item-meta">${image.gemini_verdict || 'Analyzing...'}</div>
  `;
}

function renderHistory(history) {
  const labels = history.map((r) => new Date(r.created_at + 'Z').toLocaleTimeString());
  const data = {
    labels,
    datasets: [
      { label: 'Temp (C)', data: history.map((r) => r.temperature), borderColor: '#2f9e44', tension: 0.3 },
      { label: 'Humidity (%)', data: history.map((r) => r.humidity), borderColor: '#3d8bfd', tension: 0.3 },
      { label: 'Gas (raw)', data: history.map((r) => r.gas_raw), borderColor: '#e8a33d', tension: 0.3, yAxisID: 'gas' },
    ],
  };

  if (chart) {
    chart.data = data;
    chart.update();
    return;
  }

  const ctx = document.getElementById('history-chart');
  chart = new Chart(ctx, {
    type: 'line',
    data,
    options: {
      responsive: true,
      interaction: { mode: 'index', intersect: false },
      scales: {
        y: { title: { display: true, text: 'Temp / Humidity' } },
        gas: { position: 'right', grid: { drawOnChartArea: false }, title: { display: true, text: 'Gas (raw)' } },
      },
    },
  });
}

async function loadSummary() {
  const res = await fetch('/api/dashboard/summary');
  const data = await res.json();
  thresholds = data.thresholds;
  renderReading(data.latestReading);
  renderImage(data.latestImage);
  renderHistory(data.history);
  data.recentImages.forEach(prependGalleryItem);
}

socket.on('sensor:update', (reading) => {
  renderReading(reading);
});

socket.on('image:new', (image) => {
  renderImage(image);
  prependGalleryItem(image);
});

socket.on('image:analyzed', (image) => {
  renderImage(image);
  prependGalleryItem(image);
});

loadSummary();
