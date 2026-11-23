(() => {
  "use strict";

  const PCStat = window.PCStat;
  const screen = document.querySelector("#deviceScreen");
  const transportBadge = document.querySelector("#transportBadge");
  const sourceBadge = document.querySelector("#sourceBadge");
  const transportHelp = document.querySelector("#transportHelp");
  const mockControls = document.querySelector("#mockControls");
  const bridgeConnect = document.querySelector("#bridgeConnect");
  const serialConnect = document.querySelector("#serialConnect");
  const logLines = document.querySelector("#logLines");
  const BRIDGE_STREAM_URL = "http://127.0.0.1:8765/stream";
  const MAX_SERIAL_BUFFER_CHARS = 4096;
  const DEFAULT_MOCK_VALUES = {
    cpu: 37,
    gpu: 61,
    mem: 58,
    memUsedGb: 9.3,
    memTotalGb: 16,
    disk: 68,
    diskUsedGb: 341,
    diskTotalGb: 512,
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
  };
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
    serialPipeClosed: null,
    serialSessionId: 0,
    serialBuffer: "",
    values: { ...DEFAULT_MOCK_VALUES }
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
      const memInfo = PCStat.getMemoryDisplay(v, state.source === "mock");
      return [
        card("MEMORY", `${v.mem}%`, memInfo.pageSub, v.mem),
        card("APP SPACE", memInfo.availableValue, memInfo.availableSub, null),
        card("SWAP", memInfo.swapValue, memInfo.swapSub, null)
      ];
    }
    if (page === "Storage") {
      const diskInfo = PCStat.getStorageDisplay(v);
      return [
        card("SYSTEM DISK", `${v.disk}%`, diskInfo.pageSub, v.disk),
        card("FREE", diskInfo.freeValue, diskInfo.freeSub, null),
        card("HEALTH", "GOOD", "last checked now", null)
      ];
    }
    if (page === "Network") {
      return [card("DOWNLOAD", formatNet(v.net), "from desktop", null), card("UPLOAD", formatNet(v.tx), "to desktop", null), card("LINK", "USB", "stable connection", null)];
    }
    if (page === "Gaming") {
      const gaming = PCStat.getGamingDisplay(v);
      return [
        card("CPU", gaming.cpuValue, gaming.cpuSub, gaming.cpuPercent, "blue"),
        card("GPU", gaming.gpuValue, gaming.gpuSub, gaming.gpuPercent, "purple"),
        card("DISPLAY", gaming.displayValue, gaming.displaySub, null, "green")
      ];
    }
    const memInfo = PCStat.getMemoryDisplay(v, state.source === "mock");
    const diskInfo = PCStat.getStorageDisplay(v);
    return [
      card("CPU", `${v.cpu}%`, "processor load", v.cpu),
      card("MEMORY", `${v.mem}%`, memInfo.overviewSub, v.mem),
      card("DISK", `${v.disk}%`, diskInfo.overviewSub, v.disk),
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
      const val = state.values[key];
      const hasVal = typeof val === "number" && Number.isFinite(val);
      document.querySelector(`#${inputId}`).value = hasVal ? clamp(val, 0, 100) : 0;
      document.querySelector(`#${outputId}`).textContent = hasVal ? `${val}%` : "--";
    });
    const netVal = typeof state.values.net === "number" && Number.isFinite(state.values.net) ? state.values.net : 0;
    document.querySelector("#netRange").value = clamp(netVal, 0, 100);
    document.querySelector("#netOutput").textContent = formatNet(netVal);
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
    let pressed = false;
    let held = false;
    const key = button.dataset.key;
    const cancel = () => {
      if (timer !== null) {
        window.clearTimeout(timer);
        timer = null;
      }
      pressed = false;
      held = false;
    };
    const start = (event) => {
      event.preventDefault();
      cancel();
      pressed = true;
      timer = window.setTimeout(() => {
        timer = null;
        if (!pressed) return;
        held = true;
        press(key, true);
      }, 720);
    };
    const end = (event) => {
      event.preventDefault();
      if (!pressed) return;
      const wasHeld = held;
      cancel();
      if (!wasHeld) press(key);
    };
    button.addEventListener("pointerdown", start);
    button.addEventListener("pointerup", end);
    button.addEventListener("pointerleave", cancel);
    button.addEventListener("pointercancel", cancel);
  }

  async function closeSerialPort(expectedSessionId = null) {
    if (expectedSessionId !== null && expectedSessionId !== state.serialSessionId) {
      return;
    }
    const hadSession = Boolean(state.serialPort || state.serialReader || state.serialConnected);
    const reader = state.serialReader;
    const pipeClosed = state.serialPipeClosed;
    const port = state.serialPort;

    state.serialSessionId += 1;
    state.serialReader = null;
    state.serialPipeClosed = null;
    state.serialPort = null;
    state.serialBuffer = "";
    state.serialConnected = false;
    if (state.source === "serial") {
      state.live = false;
    }

    if (hadSession) {
      updateTransportUi();
      renderScreen();
    }

    if (reader) {
      try {
        await reader.cancel();
      } catch {
        // Ignore cancel errors on already-closed streams.
      }
      try {
        reader.releaseLock();
      } catch {
        // Ignore lock release errors if already detached.
      }
    }
    if (pipeClosed) {
      try {
        await pipeClosed;
      } catch {
        // Expected when pipeTo aborts due to reader cancellation.
      }
    }
    if (port) {
      try {
        await port.close();
      } catch {
        // Ignore close errors when device was unplugged.
      }
    }
    if (hadSession) {
      updateTransportUi();
      renderScreen();
    }
  }

  async function readSerialLoop(sessionId, port) {
    if (!port || sessionId !== state.serialSessionId) return;
    let droppingOverlong = false;

    try {
      if (!port.readable) return;
      const decoder = new TextDecoderStream();
      const pipeClosed = port.readable.pipeTo(decoder.writable).catch(() => {});
      const reader = decoder.readable.getReader();
      state.serialPipeClosed = pipeClosed;
      state.serialReader = reader;

      while (sessionId === state.serialSessionId) {
        const { value, done } = await reader.read();
        if (done || sessionId !== state.serialSessionId) break;
        if (!value) continue;

        const combined = state.serialBuffer + value;
        if (!combined.includes("\n") && !combined.includes("\r")) {
          if (droppingOverlong || combined.length > MAX_SERIAL_BUFFER_CHARS) {
            droppingOverlong = true;
            state.serialBuffer = "";
          } else {
            state.serialBuffer = combined;
          }
          continue;
        }

        const lines = combined.split(/\r\n|\n|\r/);
        const tail = lines.pop() ?? "";
        for (const line of lines) {
          if (droppingOverlong) {
            droppingOverlong = false;
            continue;
          }
          if (line.length <= MAX_SERIAL_BUFFER_CHARS) {
            parseSerialLine(line);
          }
        }
        if (tail.length > MAX_SERIAL_BUFFER_CHARS) {
          droppingOverlong = true;
          state.serialBuffer = "";
        } else {
          state.serialBuffer = tail;
        }
      }
    } catch (error) {
      if (sessionId === state.serialSessionId) {
        addLog(`serial read: ${error.message}`, "info");
      }
    } finally {
      await closeSerialPort(sessionId);
    }
  }

  function parseSerialLine(line) {
    const parsed = PCStat.parsePcstatLine(line, state.values);
    if (!parsed) return;
    state.values = parsed;
    state.live = true;
    updateTransportUi();
    syncInputs();
    renderScreen();
    addLog(`PCSTAT received · ${state.values.host}`, "ok");
  }

  async function connectSerial() {
    if (!("serial" in navigator)) {
      addLog("Web Serial requires Chrome/Edge on HTTPS or localhost", "info");
      return;
    }
    await closeSerialPort();
    if (state.source !== "serial") {
      return;
    }
    const sessionId = state.serialSessionId;
    let port = null;
    try {
      port = await navigator.serial.requestPort();
      if (state.source !== "serial" || sessionId !== state.serialSessionId) {
        return;
      }
      await port.open({ baudRate: 115200 });
      if (state.source !== "serial" || sessionId !== state.serialSessionId) {
        await port.close().catch(() => {});
        return;
      }
      state.serialPort = port;
      state.serialBuffer = "";
      state.serialConnected = true;
      state.live = false;
      updateTransportUi();
      renderScreen();
      transportHelp.textContent = "已连接串口，等待电脑端或设备发送 PCSTAT 行。";
      addLog("serial port opened", "ok");
      readSerialLoop(sessionId, port);
    } catch (error) {
      if (port) {
        await port.close().catch(() => {});
      }
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

  function restoreMockValues(values) {
    const next = { ...values };
    ["gpu", "cpuMhz", "gpuMhz", "cpuTemp", "gpuTemp", "hz"].forEach((key) => {
      if (typeof next[key] !== "number" || !Number.isFinite(next[key])) {
        next[key] = DEFAULT_MOCK_VALUES[key];
      }
    });
    next.net = clamp(typeof next.net === "number" && Number.isFinite(next.net) ? next.net : DEFAULT_MOCK_VALUES.net, 0, 100);
    next.tx = clamp(typeof next.tx === "number" && Number.isFinite(next.tx) ? next.tx : DEFAULT_MOCK_VALUES.tx, 0, 100);
    return PCStat.applyMockCapacities(next);
  }

  function setMockMode() {
    closeBridgeStream();
    void closeSerialPort();
    state.source = "mock";
    state.live = true;
    state.values = restoreMockValues(state.values);
    transportHelp.textContent = "自动生成 CPU、内存、磁盘和网络变化。用于先验 UI 和按键逻辑。";
    syncInputs();
    updateTransportUi();
    addLog("switched to mock stream", "ok");
    renderScreen();
  }

  function setSerialMode() {
    closeBridgeStream();
    state.source = "serial";
    if (!state.serialConnected) {
      state.live = false;
    }
    transportHelp.textContent = "仅当串口本身会输出 PCSTAT 时使用；如果桥接程序正在占用设备串口，请改用‘电脑桥接’。";
    updateTransportUi();
    renderScreen();
    addLog("serial mode selected", "ok");
  }

  function connectBridge() {
    closeBridgeStream();
    void closeSerialPort();
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
      state.live = false;
      updateTransportUi();
      renderScreen();
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
    state.values = PCStat.applyMockCapacities(state.values);
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
    state.values = PCStat.applyMockCapacities(state.values);
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
      if (state.source === "mock" && (key === "mem" || key === "disk")) {
        state.values = PCStat.applyMockCapacities(state.values);
      }
      document.querySelector(`#${outputId}`).textContent = key === "net" ? formatNet(state.values[key]) : `${state.values[key]}%`;
      renderScreen();
    });
  });
  window.setInterval(tickMock, 1500);
  window.PassportSimulator = {
    state,
    press,
    parseSerialLine,
    setMockMode,
    setSerialMode,
    connectBridge,
    connectSerial,
    closeSerialPort,
    renderScreen
  };

  syncInputs();
  updateTransportUi();
  renderScreen();
  addLog("simulator ready", "ok");
})();
