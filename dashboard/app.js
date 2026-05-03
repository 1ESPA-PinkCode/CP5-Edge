/* ============================================================
   Vinheria Agnello — dashboard frontend
   Atualiza estado atual a cada 5s e histórico a cada 30s.
   ============================================================ */

// ───── Configurações ──────────────────────────────────────
const REFRESH_CURRENT_MS = 5000;   // 5s — estado atual
const REFRESH_HISTORY_MS = 30000;  // 30s — histórico (mais pesado)
const HISTORY_LAST_N     = 50;

const WINE_DARK   = '#7A1F23';
const WINE        = '#A12C32';
const WINE_LIGHT  = '#C84952';
const CREAM_DEEP  = '#F4EDC2';
const INK_SOFT    = '#6B4549';

// ───── Cache de gráficos ──────────────────────────────────
const charts = {};

// ───── Util: format ───────────────────────────────────────
const fmt = {
  num1:   v => (v == null || isNaN(v)) ? '—' : Number(v).toFixed(1),
  int:    v => (v == null || isNaN(v)) ? '—' : Math.round(Number(v)),
  time:   d => new Date(d).toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
  shortT: d => new Date(d).toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' }),
};

// ───── Pill de status (online/offline) ────────────────────
function setStatusPill(state, text) {
  const pill = document.getElementById('statusPill');
  pill.classList.remove('online', 'offline');
  if (state) pill.classList.add(state);
  pill.querySelector('.label').textContent = text;
}

// ───── Card meta refletindo limiares ──────────────────────
function applyAlarmFlags(current) {
  const t  = current.thresholds || {};
  const cT = document.getElementById('cardTemp');
  const cH = document.getElementById('cardHumid');
  const cL = document.getElementById('cardLux');

  // Atualiza meta com as faixas dinâmicas (vêm dos static_attributes)
  document.getElementById('metaTemp').textContent =
    `faixa ideal: ${fmt.num1(t.temperatureMin)}°C a ${fmt.num1(t.temperatureMax)}°C`;
  document.getElementById('metaHumid').textContent =
    `faixa ideal: ${fmt.num1(t.humidityMin)}% a ${fmt.num1(t.humidityMax)}%`;
  document.getElementById('metaLux').textContent =
    `máximo: ${fmt.int(t.luminosityMax)}%`;

  const trig = (current.triggerViolated || 'none').toString();
  cT.classList.toggle('alarm', trig.includes('t'));
  cH.classList.toggle('alarm', trig.includes('h'));
  cL.classList.toggle('alarm', trig.includes('l'));
}

// ───── Cor do card de conforto ────────────────────────────
function applyComfort(current) {
  const card = document.getElementById('cardStatus');
  const val  = document.getElementById('valStatus');
  const meta = document.getElementById('metaStatus');

  card.classList.remove('is-ideal', 'is-warning', 'is-critical');
  const c = current.comfortLevel || 'ideal';
  card.classList.add(`is-${c}`);

  const label = { ideal: 'Ideal', warning: 'Atenção', critical: 'Crítico' }[c] || c;
  val.textContent = label;

  const status = current.environmentStatus || 'ok';
  meta.textContent = (status === 'ok')
    ? 'todos os parâmetros dentro da faixa'
    : (current.alertType ? `alerta: ${current.alertType}` : 'em alerta');
}

// ───── Banner de alerta ───────────────────────────────────
function applyAlertBanner(current) {
  const banner = document.getElementById('alertBanner');
  const reason = (current.decisionReason || '').trim();
  const isAlert = current.alertStatus === 'active';

  if (isAlert) {
    banner.hidden = false;
    document.getElementById('alertTitle').textContent =
      current.comfortLevel === 'critical' ? 'Condição crítica' : 'Atenção';
    document.getElementById('alertReason').textContent =
      reason || 'parâmetros fora da faixa ideal';
  } else {
    banner.hidden = true;
  }
}

// ───── Fetch /api/current ─────────────────────────────────
async function loadCurrent() {
  try {
    const r = await fetch('/api/current');
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const c = await r.json();

    document.getElementById('valTemp').textContent  = fmt.num1(c.temperature);
    document.getElementById('valHumid').textContent = fmt.num1(c.humidity);
    document.getElementById('valLux').textContent   = fmt.int(c.luminosity);

    applyAlarmFlags(c);
    applyComfort(c);
    applyAlertBanner(c);

    document.getElementById('lastUpdate').textContent = fmt.time(new Date());
    setStatusPill('online', 'Online — dados em tempo real');
  } catch (e) {
    console.error('loadCurrent:', e);
    setStatusPill('offline', 'Desconectado — verifique o ESP32 e o FIWARE');
  }
}

// ───── Setup de um gráfico ────────────────────────────────
function makeChart(canvasId, label, color, fillColor) {
  const ctx = document.getElementById(canvasId).getContext('2d');
  return new Chart(ctx, {
    type: 'line',
    data: {
      labels: [],
      datasets: [{
        label,
        data: [],
        borderColor: color,
        backgroundColor: fillColor,
        borderWidth: 2,
        tension: 0.32,
        fill: true,
        pointRadius: 2.5,
        pointHoverRadius: 5,
        pointBackgroundColor: color,
      }],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 350 },
      plugins: {
        legend: { display: false },
        tooltip: {
          backgroundColor: WINE_DARK,
          titleFont: { family: 'Inter', weight: '600' },
          bodyFont:  { family: 'Inter' },
          padding: 10,
          cornerRadius: 6,
        },
      },
      scales: {
        x: {
          ticks: { color: INK_SOFT, font: { size: 11 }, maxRotation: 0, autoSkip: true, maxTicksLimit: 8 },
          grid:  { color: 'rgba(0,0,0,0.04)' },
        },
        y: {
          ticks: { color: INK_SOFT, font: { size: 11 } },
          grid:  { color: 'rgba(0,0,0,0.04)' },
          beginAtZero: false,
        },
      },
    },
  });
}

// ───── Fetch /api/history ─────────────────────────────────
async function loadHistory() {
  try {
    const r = await fetch(`/api/history?lastN=${HISTORY_LAST_N}`);
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const { data } = await r.json();

    updateChart('chartTemp',  data.temperature || [], 'Temperatura (°C)');
    updateChart('chartHumid', data.humidity    || [], 'Umidade (%)');
    updateChart('chartLux',   data.luminosity  || [], 'Luminosidade (%)');
  } catch (e) {
    console.error('loadHistory:', e);
  }
}

function updateChart(chartId, points, label) {
  const chart = charts[chartId];
  if (!chart) return;

  // Filtra entradas inválidas e ordena por timestamp
  const valid = points
    .filter(p => p.recvTime && p.value != null && !isNaN(Number(p.value)))
    .sort((a, b) => new Date(a.recvTime) - new Date(b.recvTime));

  chart.data.labels = valid.map(p => fmt.shortT(p.recvTime));
  chart.data.datasets[0].data  = valid.map(p => Number(p.value));
  chart.data.datasets[0].label = label;
  chart.update('none');
}

// ───── Comandos remotos ───────────────────────────────────
function showCmdResult(message, success) {
  const box = document.getElementById('cmdResult');
  box.hidden = false;
  box.textContent = message;
  box.classList.remove('success', 'error');
  box.classList.add(success ? 'success' : 'error');
  // Esconde depois de 4s
  clearTimeout(showCmdResult._t);
  showCmdResult._t = setTimeout(() => { box.hidden = true; }, 4000);
}

async function sendCommand(cmd, btn) {
  btn.disabled = true;
  try {
    const r = await fetch(`/api/cmd/${cmd}`, { method: 'POST' });
    const json = await r.json();
    if (!r.ok) throw new Error(json.detail || json.error || 'erro');

    const labels = {
      alert_on:        '🔔 Alerta acionado no ESP32',
      alert_off:       '✓ Alerta desligado no ESP32',
      silence_buzzer:  '🔇 Buzzer silenciado',
    };
    showCmdResult(labels[cmd] || `Comando ${cmd} enviado`, true);
    // Atualiza estado depois de 1.5s pra refletir no painel
    setTimeout(loadCurrent, 1500);
  } catch (e) {
    showCmdResult(`Falha ao enviar comando: ${e.message}`, false);
  } finally {
    btn.disabled = false;
  }
}

// ───── Bootstrap ───────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  // Cria os 3 gráficos uma vez
  charts.chartTemp  = makeChart('chartTemp',  'Temperatura (°C)', WINE,       'rgba(161, 44, 50, 0.10)');
  charts.chartHumid = makeChart('chartHumid', 'Umidade (%)',      WINE_LIGHT, 'rgba(200, 73, 82, 0.10)');
  charts.chartLux   = makeChart('chartLux',   'Luminosidade (%)', WINE_DARK,  'rgba(122, 31, 35, 0.10)');

  // Botões de comando
  document.querySelectorAll('button[data-cmd]').forEach(btn => {
    btn.addEventListener('click', () => sendCommand(btn.dataset.cmd, btn));
  });

  // Primeira carga + ciclos
  loadCurrent();
  loadHistory();
  setInterval(loadCurrent, REFRESH_CURRENT_MS);
  setInterval(loadHistory, REFRESH_HISTORY_MS);
});
