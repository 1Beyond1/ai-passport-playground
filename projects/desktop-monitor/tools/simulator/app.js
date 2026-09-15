(() => {
  "use strict";

  const screen = document.querySelector("#deviceScreen");
  const transportBadge = document.querySelector("#transportBadge");
  const sourceBadge = document.querySelector("#sourceBadge");
  const transportHelp = document.querySelector("#transportHelp");
  const mockControls = document.querySelector("#mockControls");
  const bridgeConnect = document.querySelector("#bridgeConnect");
  const serialConnect = document.querySelector("#serialConnect");
  const logLines = document.querySelector("#logLines");
  const BRIDGE_STREAM_URL = "http://127.0.0.1:8765/stream";
  const state = {
    page: 0,
    detail: false,
    source: "mock",
    live: true,
    bridgeConnected: false,
    bridgeErrorLogged: false,
    serialConnected: false,
    bridgeStream: null,
    serialPort: null,
    serialReader: null,
    serialWriter: null,
    serialBuffer: "",
    values: {
      cpu: 37,
      gpu: 61,
      mem: 58,
      disk: 68,
      net: 32,
      tx: 4,
      cpuMhz: 4200,
      gpuMhz: 2400,
      cpuTemp: 72,
      gpuTemp: 68,
      hz: 165,
      time: "14:28",
      uptime: "3h 18m",
      host: "Windows"
    }
  };

  const pages = ["Overview", "CPU", "Memory", "Storage", "Network", "Gaming"];
  const pageCopy = {
    Overview: ["Desktop Monitor", "A quiet window into your desktop"],
    CPU: ["CPU", "Processor load, right now"],
    Memory: ["Memory", "How much room your apps have"],
    Storage: ["Storage", "The system disk at a glance"],
    Network: ["Network", "Live traffic from your desktop"],
    Gaming: ["Gaming", "CPU, GPU and display while you play"]
  };

  function clamp(value, min, max) { return Math.max(min, Math.min(max, value)); }
  function nowStamp() { return new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" }); }
  function formatNet(value) { return `${(value / 10).toFixed(1)} MB/s`; }
  function formatTime() { return new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }); }
  function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, (character) => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#39;"
    }[character]));
  }

  function addLog(message, tone = "info") {
    const row = document.createElement("div");
    row.className = "log-line";
    row.innerHTML = `<time>${escapeHtml(nowStamp())}</time><span class="${escapeHtml(tone)}">${escapeHtml(message)}</span>`;
    logLines.prepend(row);
    while (logLines.children.length > 4) logLines.lastElementChild.remove();
  }

  function card(label, value, sub, percent, colorClass = "") {
    return `<div class="passport-card ${escapeHtml(colorClass)}">
      <span class="label">${escapeHtml(label)}</span>
      <span class="value">${escapeHtml(value)}</span>
      <span class="sub">${escapeHtml(sub)}</span>
      ${percent === null ? "" : `<div class="passport-meter"><i style="width:${clamp(percent, 0, 100)}%"></i></div>`}
    </div>`;
  }

  function pageCards(page) {
    const v = state.values;
    if (state.source !== "mock" && !state.live) {
      const source = state.source === "bridge" ? "BRIDGE" : "SERIAL";
      const detail = state.source === "bridge" ? "local PCSTAT stream" : "waiting for PCSTAT";
      return [
        card("STATUS", "WAIT", "no live snapshot yet", null, "blue"),
        card("SOURCE", source, detail, null, "purple"),
        card("PROTOCOL", "PCSTAT", "one record per line", null, "green")
      ];
    }
    if (page === "CPU") {
      return [card("CPU LOAD", `${v.cpu}%`, "all cores", v.cpu), card("UPTIME", v.uptime, "since last start", null), card("HOST", v.host, "connected over USB", null)];
    }
    if (page === "Memory") {
      return [card("MEMORY", `${v.mem}%`, "used · 9.3 / 16 GB", v.mem), card("APP SPACE", "6.7 GB", "available to apps", null), card("SWAP", "0.0 GB", "under control", null)];
    }
    if (page === "Storage") {
      return [card("SYSTEM DISK", `${v.disk}%`, "used · 341 / 512 GB", v.disk), card("FREE", "171 GB", "available", null), card("HEALTH", "GOOD", "last checked now", null)];
    }
    if (page === "Network") {
      return [card("DOWNLOAD", formatNet(v.net), "from desktop", null), card("UPLOAD", formatNet(v.tx), "to desktop", null), card("LINK", "USB", "stable connection", null)];
    }
    if (page === "Gaming") {
      return [
        card("CPU", `${v.cpu}% · ${v.cpuMhz} MHz`, `${v.cpuTemp} C temperature`, v.cpu, "blue"),
        card("GPU", `${v.gpu}% · ${v.gpuMhz} MHz`, `${v.gpuTemp} C temperature`, v.gpu, "purple"),
        card("DISPLAY", `${v.hz} Hz`, "current refresh rate", null, "green")
      ];
    }
    return [
      card("CPU", `${v.cpu}%`, "processor load", v.cpu),
      card("MEMORY", `${v.mem}%`, "9.3 / 16 GB used", v.mem),
      card("DISK", `${v.disk}%`, "system disk used", v.disk),
      card("NETWORK", formatNet(v.net), `upload ${formatNet(v.tx)}`, null)
    ];
  }

  // Avoid any dependency on a browser font or a large bitmap: the product UI is
  // intentionally made from the same small primitives that can be ported to LVGL.
  function renderScreen() {
    const page = pages[state.page];
    const [title, subtitle] = pageCopy[page];
    const v = state.values;
    const cards = pageCards(page).join("");
    const connection = state.source === "mock" ? "MOCK" : (state.live ? state.source.toUpperCase() : "WAIT");
    const time = state.source === "mock" || state.live ? v.time : "--:--";
    const note = state.source !== "mock" && !state.live
      ? `Waiting for PCSTAT · ${state.source === "bridge" ? "local bridge" : "USB serial"}`
      : state.detail ? "OK: summary · long OK: overview" : `Last sync ${escapeHtml(v.time)} · ${escapeHtml(v.host)}`;
    screen.innerHTML = `<div class="passport-ui ${state.detail ? "detail-view" : ""}">
      <div class="passport-topbar"><strong>AI Passport</strong><span>${escapeHtml(time)}</span><span class="passport-connection"><i></i>${connection}</span></div>
      <div class="passport-hero"><div class="kicker">${page.toUpperCase()} / DESKTOP</div><h3>${title}</h3><p>${subtitle}</p></div>
      <div class="passport-cards">${state.detail ? pageCards(page).slice(0, 3).join("") : cards}</div>
      <div class="passport-note">${note}</div>
      <div class="passport-footer"><span><b>↑↓</b> page</span><span><b>OK</b> ${state.detail ? "normal" : "details"}</span></div>
    </div>`;
  }

  function syncInputs() {
    const map = [["cpu", "cpuRange", "cpuOutput"], ["gpu", "gpuRange", "gpuOutput"], ["mem", "memRange", "memOutput"], ["disk", "diskRange", "diskOutput"]];
    map.forEach(([key, inputId, outputId]) => {
      document.querySelector(`#${inputId}`).value = state.values[key];
      document.querySelector(`#${outputId}`).textContent = `${state.values[key]}%`;
    });
    document.querySelector("#netRange").value = state.values.net;
    document.querySelector("#netOutput").textContent = formatNet(state.values.net);
  }

  function changePage(delta) {
    state.page = (state.page + delta + pages.length) % pages.length;
    state.detail = false;
    renderScreen();
    addLog(`page → ${pages[state.page]}`);
  }

  function press(key, longPress = false) {
    if (longPress && key === "ok") {
      state.page = 0;
      state.detail = false;
      renderScreen();
      addLog("long OK → overview", "ok");
      return;
    }
    if (key === "up") changePage(-1);
    if (key === "down") changePage(1);
    if (key === "ok") {
      state.detail = !state.detail;
      renderScreen();
      addLog(state.detail ? "OK → details" : "OK → summary", "ok");
    }
  }

  function bindPress(button) {
    let timer = null;
    let held = false;
    const key = button.dataset.key;
    const start = (event) => {
      event.preventDefault();
      held = false;
      timer = window.setTimeout(() => { held = true; press(key, true); }, 720);
    };
    const end = (event) => {
      event.preventDefault();
      if (timer) window.clearTimeout(timer);
      if (!held) press(key);
    };
    button.addEventListener("pointerdown", start);
    button.addEventListener("pointerup", end);
    button.addEventListener("pointerleave", () => { if (timer) window.clearTimeout(timer); });
  }

  async function readSerialLoop() {
    if (!state.serialPort) return;
    const decoder = new TextDecoderStream();
    state.serialPort.readable.pipeTo(decoder.writable).catch(() => {});
    state.serialReader = decoder.readable.getReader();
    try {
      while (true) {
        const { value, done } = await state.serialReader.read();
        if (done) break;
        state.serialBuffer += value;
        const lines = state.serialBuffer.split(/\r?\n/);
        state.serialBuffer = lines.pop();
        lines.forEach(parseSerialLine);
      }
    } catch (error) {
      addLog(`serial read: ${error.message}`, "info");
      state.serialConnected = false;
      updateTransportUi();
    }
  }

  function parseSerialLine(line) {
    line = String(line).trim();
    if (!line.startsWith("PCSTAT|")) return;
    const values = {};
    line.slice(7).split("|").forEach((part) => {
      const split = part.indexOf("=");
      if (split > 0) values[part.slice(0, split)] = part.slice(split + 1);
    });
    ["cpu", "mem", "disk"].forEach((key) => { if (values[key] !== undefined) state.values[key] = clamp(Number(values[key]) || 0, 0, 100); });
    if (values.gpu !== undefined) state.values.gpu = clamp(Number(values.gpu) || 0, 0, 100);
    if (values.rx !== undefined) state.values.net = clamp(Number(values.rx) * 10 || 0, 0, 100);
    if (values.tx !== undefined) state.values.tx = clamp(Number(values.tx) * 10 || 0, 0, 100);
    if (values.cpu_mhz !== undefined) state.values.cpuMhz = clamp(Number(values.cpu_mhz) || 0, 0, 10000);
    if (values.gpu_mhz !== undefined) state.values.gpuMhz = clamp(Number(values.gpu_mhz) || 0, 0, 10000);
    if (values.cpu_c !== undefined) state.values.cpuTemp = Number(values.cpu_c);
    if (values.gpu_c !== undefined) state.values.gpuTemp = Number(values.gpu_c);
    if (values.hz !== undefined) state.values.hz = clamp(Number(values.hz) || 0, 0, 1000);
    if (values.time) state.values.time = values.time;
    if (values.uptime) state.values.uptime = values.uptime.replace(/^0+/, "") || "0m";
    if (values.host) state.values.host = values.host;
    state.live = true;
    updateTransportUi();
    syncInputs();
    renderScreen();
    addLog(`PCSTAT received · ${state.values.host}`, "ok");
  }

  async function connectSerial() {
    if (!("serial" in navigator)) {
      addLog("Web Serial requires Chrome/Edge on localhost", "info");
      return;
    }
    try {
      state.serialPort = await navigator.serial.requestPort();
      await state.serialPort.open({ baudRate: 115200 });
      state.serialConnected = true;
      state.live = false;
      updateTransportUi();
      transportHelp.textContent = "已连接串口，等待电脑端或设备发送 PCSTAT 行。";
      addLog("serial port opened", "ok");
      readSerialLoop();
    } catch (error) {
      addLog(`serial open failed · ${error.message}`, "info");
    }
  }

  function closeBridgeStream() {
    if (state.bridgeStream) {
      state.bridgeStream.close();
      state.bridgeStream = null;
    }
    state.bridgeConnected = false;
  }

  function sourceLabel(source = state.source) {
    return source === "bridge" ? "BRIDGE" : source === "serial" ? "SERIAL" : "MOCK";
  }

  function updateTransportUi() {
    const source = sourceLabel();
    const status = state.source === "mock"
      ? "MOCK STREAM"
      : state.source === "bridge"
        ? (state.bridgeConnected ? (state.live ? "BRIDGE LIVE" : "BRIDGE WAITING") : "BRIDGE OFFLINE")
        : (state.serialConnected ? (state.live ? "SERIAL LIVE" : "SERIAL WAITING") : "SERIAL SELECT");
    sourceBadge.textContent = source;
    transportBadge.querySelector("span:last-child").textContent = status;
    ["mockMode", "bridgeMode", "serialMode"].forEach((id) => {
      const active = (id === "mockMode" && state.source === "mock")
        || (id === "bridgeMode" && state.source === "bridge")
        || (id === "serialMode" && state.source === "serial");
      const element = document.querySelector(`#${id}`);
      element.classList.toggle("active", active);
      element.setAttribute("aria-selected", String(active));
    });
    mockControls.hidden = state.source !== "mock";
    bridgeConnect.hidden = state.source !== "bridge";
    serialConnect.hidden = state.source !== "serial";
    serialConnect.disabled = state.serialConnected;
    serialConnect.textContent = state.serialConnected ? "USB 串口已连接" : "选择 USB 串口";
    bridgeConnect.textContent = state.bridgeConnected ? "Bridge 已连接" : "连接本机 Bridge";
  }

  function setMockMode() {
    closeBridgeStream();
    state.source = "mock";
    state.live = true;
    transportHelp.textContent = "自动生成 CPU、内存、磁盘和网络变化。用于先验 UI 和按键逻辑。";
    updateTransportUi();
    addLog("switched to mock stream", "ok");
    renderScreen();
  }

  function setSerialMode() {
    closeBridgeStream();
    state.source = "serial";
    state.live = false;
    transportHelp.textContent = "仅当串口本身会输出 PCSTAT 时使用；如果桥接程序正在占用设备串口，请改用‘电脑桥接’。";
    updateTransportUi();
    renderScreen();
    addLog("serial mode selected", "ok");
  }

  function connectBridge() {
    closeBridgeStream();
    state.source = "bridge";
    state.live = false;
    state.bridgeErrorLogged = false;
    transportHelp.textContent = `连接 ${BRIDGE_STREAM_URL}，桥接程序会同时更新设备和浏览器。`;
    updateTransportUi();
    renderScreen();
    addLog("connecting local bridge", "info");

    const stream = new EventSource(BRIDGE_STREAM_URL);
    state.bridgeStream = stream;
    stream.onopen = () => {
      if (state.bridgeStream !== stream) return;
      state.bridgeConnected = true;
      state.bridgeErrorLogged = false;
      updateTransportUi();
      addLog("bridge connected · waiting for PCSTAT", "ok");
    };
    stream.onmessage = (event) => {
      if (state.bridgeStream === stream) parseSerialLine(event.data);
    };
    stream.onerror = () => {
      if (state.bridgeStream !== stream) return;
      state.bridgeConnected = false;
      updateTransportUi();
      if (!state.bridgeErrorLogged) {
        state.bridgeErrorLogged = true;
        addLog("bridge offline · start desktop_bridge.py", "info");
      }
    };
  }

  function randomize() {
    state.values.cpu = Math.round(12 + Math.random() * 76);
    state.values.gpu = Math.round(8 + Math.random() * 88);
    state.values.mem = Math.round(30 + Math.random() * 57);
    state.values.disk = Math.round(48 + Math.random() * 35);
    state.values.net = Math.round(4 + Math.random() * 85);
    state.values.tx = Math.round(state.values.net * (0.18 + Math.random() * 0.25));
    state.values.cpuMhz = Math.round(3600 + Math.random() * 1200);
    state.values.gpuMhz = Math.round(1800 + Math.random() * 900);
    state.values.cpuTemp = Math.round(55 + Math.random() * 35);
    state.values.gpuTemp = Math.round(50 + Math.random() * 32);
    state.values.hz = [60, 120, 144, 165, 240][Math.floor(Math.random() * 5)];
    state.values.time = formatTime();
    syncInputs();
    renderScreen();
    addLog("mock snapshot regenerated", "ok");
  }

  function tickMock() {
    if (state.source !== "mock" || !document.querySelector("#autoMock").checked) return;
    const drift = (value, amount) => clamp(Math.round(value + (Math.random() - .5) * amount), 0, 100);
    state.values.cpu = drift(state.values.cpu, 14);
    state.values.gpu = drift(state.values.gpu, 20);
    state.values.mem = drift(state.values.mem, 4);
    state.values.net = drift(state.values.net, 18);
    state.values.tx = drift(state.values.tx, 8);
    state.values.cpuMhz = clamp(Math.round(state.values.cpuMhz + (Math.random() - .5) * 180), 800, 5200);
    state.values.gpuMhz = clamp(Math.round(state.values.gpuMhz + (Math.random() - .5) * 220), 300, 3000);
    state.values.cpuTemp = clamp(Math.round(state.values.cpuTemp + (Math.random() - .5) * 4), 35, 100);
    state.values.gpuTemp = clamp(Math.round(state.values.gpuTemp + (Math.random() - .5) * 4), 35, 95);
    state.values.time = formatTime();
    syncInputs();
    renderScreen();
  }

  document.querySelectorAll("[data-key]").forEach(bindPress);
  document.addEventListener("keydown", (event) => {
    if (event.target?.closest?.("input, button, select, textarea")) return;
    if (event.key === "ArrowUp") press("up");
    if (event.key === "ArrowDown") press("down");
    if (event.key === "Enter") press("ok");
    if (["ArrowUp", "ArrowDown", "Enter"].includes(event.key)) event.preventDefault();
  });
  document.querySelector("#mockMode").addEventListener("click", setMockMode);
  document.querySelector("#bridgeMode").addEventListener("click", connectBridge);
  document.querySelector("#serialMode").addEventListener("click", setSerialMode);
  bridgeConnect.addEventListener("click", connectBridge);
  serialConnect.addEventListener("click", connectSerial);
  document.querySelector("#randomize").addEventListener("click", randomize);
  document.querySelector("#clearLog").addEventListener("click", () => { logLines.replaceChildren(); });
  [["cpuRange", "cpu", "cpuOutput"], ["gpuRange", "gpu", "gpuOutput"], ["memRange", "mem", "memOutput"], ["diskRange", "disk", "diskOutput"], ["netRange", "net", "netOutput"]].forEach(([inputId, key, outputId]) => {
    document.querySelector(`#${inputId}`).addEventListener("input", (event) => {
      state.values[key] = Number(event.target.value);
      document.querySelector(`#${outputId}`).textContent = key === "net" ? formatNet(state.values[key]) : `${state.values[key]}%`;
      renderScreen();
    });
  });
  window.setInterval(tickMock, 1500);
  window.PassportSimulator = { state, press, parseSerialLine, setMockMode, renderScreen };

  syncInputs();
  updateTransportUi();
  renderScreen();
  addLog("simulator ready", "ok");
})();
