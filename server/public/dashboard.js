const socket = io();
const statusPill = document.getElementById('connection-status');
const lastUpdatedEl = document.getElementById('last-updated');

socket.on('connect', () => {
  statusPill.classList.add('connected');
  statusPill.innerHTML = '<span class="status-dot"></span>live';
});
socket.on('disconnect', () => {
  statusPill.classList.remove('connected');
  statusPill.innerHTML = '<span class="status-dot"></span>disconnected';
});

let thresholds = { gasWarning: 1500, gasDanger: 2800 };
let chart;
let latestImageId = null;

const IMAGE_PAGE_SIZE = 5;
let currentImagePage = 1;
let totalImagePages = 1;

function setLastUpdated() {
  lastUpdatedEl.textContent = `Updated ${new Date().toLocaleTimeString()}`;
}

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
  if (verdict === 'Fresh' || verdict === 'Good') return 'ok';
  if (verdict === 'Caution') return 'warn';
  if (verdict === 'Spoiled' || verdict === 'Not Good') return 'danger';
  return '';
}
function formatTime(createdAt) {
  return new Date(createdAt + 'Z').toLocaleString();
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

  setLastUpdated();
}

function renderSnapshotChips(image) {
  const el = document.getElementById('capture-snapshot');
  if (image.temperature == null) {
    el.innerHTML = '';
    return;
  }
  el.innerHTML = `
    <span class="snapshot-chip">${Number(image.temperature).toFixed(1)} C</span>
    <span class="snapshot-chip">${Number(image.humidity).toFixed(0)} % humidity</span>
    <span class="snapshot-chip">Gas ${image.gas_raw} (${gasLabel(image.gas_raw)})</span>
    <span class="snapshot-chip">${Number(image.weight_g).toFixed(0)} g</span>
  `;
}

function renderImage(image) {
  if (!image) return;
  document.getElementById('latest-image').src = image.filepath;
  document.getElementById('image-timestamp').textContent = formatTime(image.created_at);
  renderSnapshotChips(image);

  const verdictBadge = document.getElementById('verdict-badge');
  const notes = document.getElementById('verdict-notes');

  if (!image.gemini_verdict) {
    verdictBadge.textContent = 'Analyzing…';
    verdictBadge.className = 'badge';
    notes.textContent = 'Waiting for Gemini feedback.';
  } else {
    verdictBadge.textContent = image.gemini_verdict;
    verdictBadge.className = `badge ${verdictBadgeClass(image.gemini_verdict)}`;
    notes.textContent = image.gemini_notes || '';
  }
}

function renderStatus(status) {
  if (!status) return;
  const badge = document.getElementById('status-badge');
  const notes = document.getElementById('status-notes');
  const source = document.getElementById('status-source');
  const timestamp = document.getElementById('status-timestamp');

  badge.textContent = status.verdict || 'Unknown';
  badge.className = `badge ${verdictBadgeClass(status.verdict)}`;
  notes.textContent = status.notes || '';
  source.textContent = status.source === 'image' ? 'from latest photo' : 'from sensor data (no recent photo)';
  timestamp.textContent = status.created_at ? formatTime(status.created_at) : '';
}

function galleryItemMarkup(image) {
  const verdict = image.gemini_verdict || 'Analyzing…';
  return `
    <img src="${image.filepath}" alt="capture" loading="lazy" />
    <div class="gallery-item-meta">
      <span class="badge ${verdictBadgeClass(image.gemini_verdict)}">${verdict}</span>
    </div>
    <div class="gallery-item-meta">
      <span class="gallery-item-time">${formatTime(image.created_at)}</span>
    </div>
  `;
}

function renderGallery(items) {
  const gallery = document.getElementById('gallery');
  if (!items.length) {
    gallery.innerHTML = '<p class="gallery-empty">No images captured yet.</p>';
    return;
  }
  gallery.innerHTML = items
    .map((image) => `<div class="gallery-item" id="gallery-item-${image.id}">${galleryItemMarkup(image)}</div>`)
    .join('');
}

function updatePaginationControls({ page, totalPages, total }) {
  currentImagePage = page;
  totalImagePages = totalPages;
  document.getElementById('page-indicator').textContent = `Page ${page} of ${totalPages}`;
  document.getElementById('gallery-count').textContent = `${total} total`;
  document.getElementById('prev-page').disabled = page <= 1;
  document.getElementById('next-page').disabled = page >= totalPages;
}

async function loadImagePage(page) {
  const res = await fetch(`/api/images?page=${page}&limit=${IMAGE_PAGE_SIZE}`);
  const data = await res.json();
  renderGallery(data.items);
  updatePaginationControls(data);
}

document.getElementById('prev-page').addEventListener('click', () => {
  if (currentImagePage > 1) loadImagePage(currentImagePage - 1);
});
document.getElementById('next-page').addEventListener('click', () => {
  if (currentImagePage < totalImagePages) loadImagePage(currentImagePage + 1);
});

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
      plugins: { legend: { labels: { boxWidth: 10, boxHeight: 10 } } },
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
  if (data.latestImage) {
    latestImageId = data.latestImage.id;
    renderImage(data.latestImage);
  }
  renderHistory(data.history);
  renderStatus(data.currentStatus);
}

socket.on('sensor:update', (reading) => {
  renderReading(reading);
});

socket.on('image:new', (image) => {
  latestImageId = image.id;
  renderImage(image);
  if (currentImagePage === 1) loadImagePage(1);
});

socket.on('image:analyzed', (image) => {
  // Only update the Latest Capture panel if a newer image hasn't already replaced it.
  if (image.id === latestImageId) renderImage(image);
  if (currentImagePage === 1) loadImagePage(1);
  renderStatus({ source: 'image', verdict: image.gemini_verdict, notes: image.gemini_notes, created_at: image.created_at });
});

socket.on('status:update', (status) => {
  renderStatus(status);
});

loadSummary();
loadImagePage(1);
