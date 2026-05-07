const WARN = 70;
const FULL = 90;
const GAS_WARN = 400;
const GAS_ALARM = 800;

let lastNotificationId = Number(localStorage.getItem("lastNotificationId") || "0");

function cssVar(name) {
  return getComputedStyle(document.body).getPropertyValue(name).trim();
}

function getStoredTheme() {
  return localStorage.getItem("theme") || "dark";
}

function setTheme(theme) {
  document.body.setAttribute("data-theme", theme);
  localStorage.setItem("theme", theme);

  const themeToggle = document.getElementById("theme-toggle");
  themeToggle.textContent = theme === "dark" ? "Açık Tema" : "Koyu Tema";

  applyChartTheme();
}

function toggleTheme() {
  const current = document.body.getAttribute("data-theme") || "dark";
  const next = current === "dark" ? "light" : "dark";
  setTheme(next);
}

function applyChartTheme() {
  Chart.defaults.color = cssVar("--muted");
  Chart.defaults.borderColor = cssVar("--border");

  historyChart.data.datasets[0].borderColor = cssVar("--blue");
  historyChart.data.datasets[0].backgroundColor = cssVar("--chart-fill-1");

  historyChart.data.datasets[1].borderColor = cssVar("--yellow");
  historyChart.data.datasets[1].backgroundColor = cssVar("--chart-fill-2");

  historyChart.options.scales.y.grid.color =
    document.body.getAttribute("data-theme") === "dark"
      ? "rgba(255, 122, 26, 0.10)"
      : "rgba(88, 77, 62, 0.10)";

  historyChart.options.plugins.legend.labels.color = cssVar("--text");
  historyChart.update();
}

const historyChart = new Chart(document.getElementById("history-chart"), {
  type: "line",
  data: {
    labels: [],
    datasets: [
      {
        label: "Doluluk %",
        data: [],
        borderColor: "#8d9eff",
        backgroundColor: "rgba(141, 158, 255, 0.16)",
        fill: true,
        tension: 0.32,
        pointRadius: 1.8,
        borderWidth: 2
      },
      {
        label: "Gaz",
        data: [],
        borderColor: "#d6a653",
        backgroundColor: "rgba(214, 166, 83, 0.12)",
        fill: false,
        tension: 0.32,
        pointRadius: 1.8,
        borderWidth: 2
      }
    ]
  },
  options: {
    animation: false,
    responsive: true,
    maintainAspectRatio: false,
    scales: {
      y: {
        beginAtZero: true,
        ticks: {
          font: { size: 11 }
        },
        grid: {
          color: "rgba(255,255,255,0.08)"
        }
      },
      x: {
        ticks: {
          maxTicksLimit: 8,
          font: { size: 10 }
        },
        grid: {
          display: false
        }
      }
    },
    plugins: {
      legend: {
        position: "bottom",
        labels: {
          boxWidth: 10,
          boxHeight: 10,
          color: "#eceff4",
          font: { size: 11 }
        }
      }
    }
  }
});

function valueOrDash(value) {
  return value === undefined || value === null ? "—" : value;
}

function setConnection(online) {
  const dot = document.getElementById("status-dot");
  const text = document.getElementById("connection-text");

  const green = cssVar("--green");
  const red = cssVar("--red");

  if (online) {
    dot.style.background = green;
    dot.style.boxShadow = `0 0 0 4px color-mix(in srgb, ${green} 25%, transparent)`;
    text.textContent = "Bağlı";
    text.style.color = green;
  } else {
    dot.style.background = red;
    dot.style.boxShadow = `0 0 0 4px color-mix(in srgb, ${red} 25%, transparent)`;
    text.textContent = "Bağlantı Yok";
    text.style.color = red;
  }
}

function colorForStatus(status) {
  if (status === "ALARM") return cssVar("--red");
  if (status === "WARNING") return cssVar("--yellow");
  return cssVar("--green");
}

function updateLive(d) {
  if (!d || !d.id) return;

  const statusColor = colorForStatus(d.status);
  const banner = document.getElementById("banner");

  if (d.status === "ALARM") {
    banner.textContent = "Durum: Alarm";
  } else if (d.status === "WARNING") {
    banner.textContent = "Durum: Uyarı";
  } else {
    banner.textContent = "Durum: Normal";
  }

  banner.style.color = statusColor;
  banner.style.borderColor = statusColor;

  const fillColor =
    d.fill >= FULL ? cssVar("--red") :
    d.fill >= WARN ? cssVar("--yellow") :
    cssVar("--green");

  document.getElementById("fill-value").textContent = d.fill + "%";
  document.getElementById("fill-value").style.color = fillColor;

  document.getElementById("fill-bar").style.width =
    Math.max(0, Math.min(100, d.fill)) + "%";
  document.getElementById("fill-bar").style.background = fillColor;

  document.getElementById("distance-text").textContent =
    "Sensör okuması: " + Number(d.distance).toFixed(1) + " cm";

  document.getElementById("calibration-text").textContent =
    "Kalibrasyon: boş " + Number(d.calibration_empty).toFixed(1) +
    " cm / dolu " + Number(d.calibration_full).toFixed(1) + " cm";

  document.getElementById("fill-time").textContent =
    "Son kayıt: " + d.received_at;

  const gasColor =
    d.gas_do || d.gas >= GAS_ALARM ? cssVar("--red") :
    d.gas >= GAS_WARN ? cssVar("--yellow") :
    cssVar("--green");

  document.getElementById("gas-value").textContent = d.gas;
  document.getElementById("gas-value").style.color = gasColor;

  const gasPct = Math.min(100, Math.round((d.gas / GAS_ALARM) * 100));
  document.getElementById("gas-bar").style.width = gasPct + "%";
  document.getElementById("gas-bar").style.background = gasColor;

  document.getElementById("gas-voltage-text").textContent =
    "Voltaj: " + Number(d.gas_voltage).toFixed(2) + " V";

  document.getElementById("gas-do-text").textContent =
    "DO: " + (d.gas_do ? "ALARM" : "Normal");

  document.getElementById("device-text").textContent =
    "Cihaz: " + d.device_id +
    " · FW: " + (d.firmware || "—") +
    " · RSSI: " + valueOrDash(d.rssi) + " dBm";
}

function updateStats(s) {
  if (!s || typeof s.count === "undefined") return;

  document.getElementById("stat-count").textContent = valueOrDash(s.count);
  document.getElementById("stat-max").textContent = valueOrDash(s.max_fill) + "%";
  document.getElementById("stat-min").textContent = valueOrDash(s.min_fill) + "%";
  document.getElementById("stat-avg").textContent = valueOrDash(s.avg_fill) + "%";
  document.getElementById("stat-empties").textContent = valueOrDash(s.empties);
  document.getElementById("stat-warnings").textContent = valueOrDash(s.warnings);
  document.getElementById("stat-alarms").textContent = valueOrDash(s.alarms);
}

function updateHistory(data) {
  if (!Array.isArray(data)) return;

  const ordered = data.slice().reverse();

  historyChart.data.labels = ordered.map(x => x.received_at);
  historyChart.data.datasets[0].data = ordered.map(x => x.fill);
  historyChart.data.datasets[1].data = ordered.map(x => x.gas);
  historyChart.update();

  const tbody = document.getElementById("history-table");
  tbody.innerHTML = "";

  data.slice(0, 20).forEach(row => {
    const tr = document.createElement("tr");
    const statusColor = colorForStatus(row.status);

    tr.innerHTML = `
      <td>${row.received_at}</td>
      <td>${row.fill}%</td>
      <td>${row.gas}</td>
      <td class="mini-status" style="color:${statusColor}">${row.status}</td>
      <td>${row.rssi ?? "—"}</td>
    `;

    tbody.appendChild(tr);
  });

  if (data.length === 0) {
    tbody.innerHTML = `<tr><td colspan="5">Henüz kayıt yok.</td></tr>`;
  }
}

function addNotificationToPanel(item) {
  const list = document.getElementById("notification-list");

  if (list.children.length === 1 && list.children[0].textContent.includes("Henüz bildirim yok")) {
    list.innerHTML = "";
  }

  const div = document.createElement("div");
  div.className = "notification-item " + item.level.toLowerCase();

  div.innerHTML = `
    <div class="notification-title">${item.level} · ${item.title}</div>
    <div class="notification-message">${item.message}</div>
    <div class="notification-message">${item.created_at} · ${item.channel}</div>
  `;

  list.prepend(div);

  while (list.children.length > 8) {
    list.removeChild(list.lastChild);
  }
}

function showBrowserNotification(item) {
  if (!("Notification" in window)) return;
  if (Notification.permission !== "granted") return;

  if (item.level === "ALARM" || item.level === "WARNING" || item.level === "INFO") {
    new Notification(item.title, {
      body: item.message,
      tag: "smart-trash-" + item.id
    });
  }
}

async function updateNotifications() {
  const events = await fetchJson("/api/notifications?after_id=" + lastNotificationId);

  if (!Array.isArray(events)) return;

  for (const item of events) {
    lastNotificationId = Math.max(lastNotificationId, Number(item.id));
    localStorage.setItem("lastNotificationId", String(lastNotificationId));

    addNotificationToPanel(item);
    showBrowserNotification(item);
  }
}

async function fetchJson(url, options = undefined) {
  const response = await fetch(url, options);

  if (!response.ok) {
    throw new Error("HTTP " + response.status);
  }

  return await response.json();
}

async function refresh() {
  try {
    const [live, history, stats] = await Promise.all([
      fetchJson("/api/live"),
      fetchJson("/api/history?limit=100"),
      fetchJson("/api/stats")
    ]);

    updateLive(live);
    updateHistory(history);
    updateStats(stats);
    await updateNotifications();

    setConnection(true);
  } catch (err) {
    console.error(err);
    setConnection(false);
  }
}

document.getElementById("theme-toggle").addEventListener("click", toggleTheme);

document.getElementById("notify-btn").addEventListener("click", async () => {
  if (!("Notification" in window)) {
    alert("Bu tarayıcı bildirimleri desteklemiyor.");
    return;
  }

  const permission = await Notification.requestPermission();
  alert(permission === "granted" ? "Bildirim izni verildi." : "Bildirim izni verilmedi.");
});

document.getElementById("test-btn").addEventListener("click", async () => {
  try {
    await fetchJson("/api/test-notification", { method: "POST" });
    await updateNotifications();
  } catch (err) {
    alert("Test bildirimi oluşturulamadı: " + err.message);
  }
});

setTheme(getStoredTheme());
refresh();
setInterval(refresh, 5000);