"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const PCStat = require("../simulator/pcstat.js");

const BASE_VALUES = {
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

test("parses extended PCSTAT frame with real memory and disk GB capacities", () => {
  const line =
    "PCSTAT|cpu=45|mem=62|disk=74|rx=2.5|tx=0.8|cpu_mhz=4500|gpu=50|gpu_mhz=2100|gpu_c=64|cpu_c=69|hz=144|mem_used_gb=19.8|mem_total_gb=32.0|disk_used_gb=740.4|disk_total_gb=1000.0|time=18:05|uptime=5h12m|host=RIG-01\r\n";
  const parsed = PCStat.parsePcstatLine(line, BASE_VALUES);
  assert.ok(parsed);
  assert.equal(parsed.cpu, 45);
  assert.equal(parsed.mem, 62);
  assert.equal(parsed.disk, 74);
  assert.equal(parsed.net, 25);
  assert.equal(parsed.tx, 8);
  assert.equal(parsed.cpuMhz, 4500);
  assert.equal(parsed.gpu, 50);
  assert.equal(parsed.gpuMhz, 2100);
  assert.equal(parsed.gpuTemp, 64);
  assert.equal(parsed.cpuTemp, 69);
  assert.equal(parsed.hz, 144);
  assert.equal(parsed.memUsedGb, 19.8);
  assert.equal(parsed.memTotalGb, 32);
  assert.equal(parsed.diskUsedGb, 740.4);
  assert.equal(parsed.diskTotalGb, 1000);
  assert.equal(parsed.time, "18:05");
  assert.equal(parsed.uptime, "5h12m");
  assert.equal(parsed.host, "RIG-01");

  const memDisplay = PCStat.getMemoryDisplay(parsed, false);
  assert.equal(memDisplay.hasCapacity, true);
  assert.equal(memDisplay.overviewSub, "19.8 / 32 GB used");
  assert.equal(memDisplay.pageSub, "used · 19.8 / 32 GB");
  assert.equal(memDisplay.availableValue, "12.2 GB");
  assert.equal(memDisplay.swapValue, "--");

  const diskDisplay = PCStat.getStorageDisplay(parsed);
  assert.equal(diskDisplay.hasCapacity, true);
  assert.equal(diskDisplay.overviewSub, "740 / 1000 GB used");
  assert.equal(diskDisplay.pageSub, "used · 740 / 1000 GB");
  assert.equal(diskDisplay.freeValue, "260 GB");
});

test("legacy PCSTAT frame without GB fields clears stale mock capacities and degrades gracefully", () => {
  const legacyLine =
    "PCSTAT|cpu=22|mem=44|disk=55|rx=1.1|tx=0.2|time=09:15|uptime=00m|host=LegacyPC";
  const parsed = PCStat.parsePcstatLine(legacyLine, BASE_VALUES);
  assert.ok(parsed);
  assert.equal(parsed.cpu, 22);
  assert.equal(parsed.mem, 44);
  assert.equal(parsed.disk, 55);
  assert.equal(parsed.uptime, "0m");
  assert.equal(parsed.memUsedGb, null);
  assert.equal(parsed.memTotalGb, null);
  assert.equal(parsed.diskUsedGb, null);
  assert.equal(parsed.diskTotalGb, null);

  const memDisplay = PCStat.getMemoryDisplay(parsed, false);
  assert.equal(memDisplay.hasCapacity, false);
  assert.equal(memDisplay.overviewSub, "memory used");
  assert.equal(memDisplay.availableValue, "--");

  const diskDisplay = PCStat.getStorageDisplay(parsed);
  assert.equal(diskDisplay.hasCapacity, false);
  assert.equal(diskDisplay.overviewSub, "system disk used");
  assert.equal(diskDisplay.freeValue, "--");
});

test("handles invalid lines and malformed capacity fields safely", () => {
  assert.equal(PCStat.parsePcstatLine("", BASE_VALUES), null);
  assert.equal(PCStat.parsePcstatLine("HELLO|cpu=10", BASE_VALUES), null);
  assert.equal(PCStat.parsePcstatLine("PCSTAT|unknown=123", BASE_VALUES), null);

  const badCap = PCStat.parsePcstatLine(
    "PCSTAT|cpu=15|mem=50|mem_used_gb=NaN|mem_total_gb=16|disk_used_gb=600|disk_total_gb=500|host=Edge",
    BASE_VALUES
  );
  assert.ok(badCap);
  assert.equal(badCap.memUsedGb, null);
  assert.equal(badCap.memTotalGb, null);
  // Clamps used <= total when both are valid positive numbers
  assert.equal(badCap.diskUsedGb, 500);
  assert.equal(badCap.diskTotalGb, 500);
});

test("formatGb and mock capacities behave predictably", () => {
  assert.equal(PCStat.formatGb(16.0), "16");
  assert.equal(PCStat.formatGb(9.34), "9.3");
  assert.equal(PCStat.formatGb(341.2), "341");
  assert.equal(PCStat.formatGb(NaN), "--");
  assert.equal(PCStat.formatGb(-1), "--");

  const mockValues = PCStat.applyMockCapacities({ ...BASE_VALUES, mem: 50, disk: 25 });
  assert.equal(mockValues.memUsedGb, 8);
  assert.equal(mockValues.memTotalGb, 16);
  assert.equal(mockValues.diskUsedGb, 128);
  assert.equal(mockValues.diskTotalGb, 512);
});

test("simulator static assets use relative paths safe for GitHub Pages subpaths", () => {
  const simDir = path.resolve(__dirname, "../simulator");
  const html = fs.readFileSync(path.join(simDir, "index.html"), "utf8");
  assert.match(html, /href="favicon\.svg"/);
  assert.match(html, /href="styles\.css"/);
  assert.match(html, /src="pcstat\.js"/);
  assert.match(html, /src="app\.js"/);
  assert.doesNotMatch(html, /(?:href|src)="\/[^/]/);
  assert.ok(fs.existsSync(path.join(simDir, "favicon.svg")));
  assert.ok(fs.existsSync(path.join(simDir, "styles.css")));
  assert.ok(fs.existsSync(path.join(simDir, "pcstat.js")));
  assert.ok(fs.existsSync(path.join(simDir, "app.js")));
});

test("P1-1: rx/tx rates support up to 6553.5 MB/s (65535 tenths) without truncating at 10.0 MB/s", () => {
  const highSpeed = PCStat.parsePcstatLine(
    "PCSTAT|cpu=18|mem=40|disk=50|rx=125.4|tx=88.9|host=GigabitPC",
    BASE_VALUES
  );
  assert.ok(highSpeed);
  assert.equal(highSpeed.net, 1254);
  assert.equal(highSpeed.tx, 889);

  const maxSpeed = PCStat.parsePcstatLine(
    "PCSTAT|cpu=18|rx=6553.5|tx=9999.9|host=TenGigPC",
    BASE_VALUES
  );
  assert.ok(maxSpeed);
  assert.equal(maxSpeed.net, 65535);
  assert.equal(maxSpeed.tx, 65535);
});

test("P1-2: parsePcstatLine clears absent optional Gaming fields to null and getGamingDisplay degrades gracefully", () => {
  const minimal = PCStat.parsePcstatLine(
    "PCSTAT|cpu=24|mem=51|disk=60|rx=1.5|tx=0.3|time=10:00|uptime=1h5m|host=NoGpuLaptop",
    BASE_VALUES
  );
  assert.ok(minimal);
  assert.equal(minimal.gpu, null);
  assert.equal(minimal.cpuMhz, null);
  assert.equal(minimal.gpuMhz, null);
  assert.equal(minimal.cpuTemp, null);
  assert.equal(minimal.gpuTemp, null);
  assert.equal(minimal.hz, null);

  const display = PCStat.getGamingDisplay(minimal);
  assert.equal(display.cpuValue, "24% · --");
  assert.equal(display.cpuSub, "CPU temp unavailable");
  assert.equal(display.gpuValue, "N/A");
  assert.equal(display.gpuSub, "GPU unavailable");
  assert.equal(display.gpuPercent, null);
  assert.equal(display.displayValue, "-- Hz");

  // Only gpu_c present must not forge a 0% GPU utilization readout
  const tempOnlyGpu = PCStat.parsePcstatLine(
    "PCSTAT|cpu=30|gpu_c=-10|host=SubZeroRig",
    BASE_VALUES
  );
  assert.ok(tempOnlyGpu);
  assert.equal(tempOnlyGpu.gpu, null);
  assert.equal(tempOnlyGpu.gpuTemp, -10);
  const subZeroDisplay = PCStat.getGamingDisplay(tempOnlyGpu);
  assert.equal(subZeroDisplay.gpuValue, "--");
  assert.equal(subZeroDisplay.gpuSub, "-10 C temperature");
  assert.equal(subZeroDisplay.gpuPercent, null);
});

test("P1-3 & P2 (vm): app.js closes serial on mode switch & EOF, bounds serialBuffer, fixes bindPress, and clears live on bridge error", async () => {
  const simDir = path.resolve(__dirname, "../simulator");
  const appSrc = fs.readFileSync(path.join(simDir, "app.js"), "utf8");

  function makeElement(id = "", dataset = {}) {
    const listeners = new Map();
    const attrs = new Map();
    const classes = new Set();
    const children = [];
    const el = {
      id,
      dataset,
      value: "",
      checked: false,
      hidden: false,
      disabled: false,
      textContent: "",
      innerHTML: "",
      children,
      classList: {
        toggle: (cls, force) => {
          if (force) classes.add(cls);
          else classes.delete(cls);
        },
        contains: (cls) => classes.has(cls)
      },
      setAttribute: (k, v) => attrs.set(k, v),
      getAttribute: (k) => attrs.get(k) ?? null,
      addEventListener: (ev, fn) => {
        if (!listeners.has(ev)) listeners.set(ev, []);
        listeners.get(ev).push(fn);
      },
      dispatch: (ev, extra = {}) => {
        for (const fn of listeners.get(ev) || []) {
          fn({ preventDefault() {}, target: el, ...extra });
        }
      },
      querySelector: () => el._child || makeElement(),
      prepend: (child) => {
        child.parent = el;
        children.unshift(child);
        el.lastElementChild = children[children.length - 1] || null;
      },
      remove: () => {
        if (el.parent) {
          const idx = el.parent.children.indexOf(el);
          if (idx >= 0) el.parent.children.splice(idx, 1);
          el.parent.lastElementChild = el.parent.children[el.parent.children.length - 1] || null;
          el.parent = null;
        }
      },
      replaceChildren: () => {
        children.length = 0;
      }
    };
    return el;
  }

  const elements = new Map();
  [
    "deviceScreen", "transportBadge", "sourceBadge", "transportHelp", "mockControls",
    "bridgeConnect", "serialConnect", "logLines", "mockMode", "bridgeMode", "serialMode",
    "randomize", "clearLog", "autoMock", "cpuRange", "cpuOutput", "gpuRange", "gpuOutput",
    "memRange", "memOutput", "diskRange", "diskOutput", "netRange", "netOutput"
  ].forEach((id) => elements.set(id, makeElement(id)));
  elements.get("transportBadge")._child = makeElement("transportBadgeText");

  const keyButtons = [
    makeElement("btnUp", { key: "up" }),
    makeElement("btnOk", { key: "ok" }),
    makeElement("btnDown", { key: "down" })
  ];

  const eventSources = [];
  class MockEventSource {
    constructor(url) {
      this.url = url;
      this.closed = false;
      eventSources.push(this);
    }
    close() {
      this.closed = true;
    }
  }

  let portCounter = 0;
  let nextPortBehavior = null;
  const ports = [];
  function createMockPort(chunks) {
    const portInfo = {
      id: ++portCounter,
      opened: false,
      closed: false,
      cancelled: false,
      lockReleased: false,
      async open() {
        this.opened = true;
      },
      async close() {
        this.closed = true;
      },
      readable: {
        pipeTo() {
          return Promise.resolve();
        }
      },
      _chunks: [...chunks]
    };
    ports.push(portInfo);
    return portInfo;
  }

  let activeChunksQueue = [];
  let activePortRef = null;
  class MockTextDecoderStream {
    constructor() {
      this.writable = {};
      const portRef = activePortRef;
      let idx = 0;
      let pendingResolve = null;
      this.readable = {
        getReader: () => ({
          read: () => {
            if (idx < activeChunksQueue.length) {
              const item = activeChunksQueue[idx++];
              if (item === "__WAIT__") {
                return new Promise((resolve) => {
                  pendingResolve = resolve;
                });
              }
              return Promise.resolve({ value: item, done: false });
            }
            return Promise.resolve({ value: undefined, done: true });
          },
          cancel: async () => {
            if (portRef) portRef.cancelled = true;
            if (pendingResolve) {
              const res = pendingResolve;
              pendingResolve = null;
              res({ value: undefined, done: true });
            }
          },
          releaseLock: () => {
            if (portRef) portRef.lockReleased = true;
          }
        })
      };
    }
  }

  const timeouts = new Set();
  const intervals = new Set();
  const sandbox = {
    PCStat,
    EventSource: MockEventSource,
    TextDecoderStream: MockTextDecoderStream,
    navigator: {
      serial: {
        requestPort: async () => {
          const port = createMockPort(nextPortBehavior());
          activePortRef = port;
          activeChunksQueue = port._chunks;
          return port;
        }
      }
    },
    document: {
      querySelector: (sel) => {
        if (sel.startsWith("#")) return elements.get(sel.slice(1)) || null;
        return null;
      },
      querySelectorAll: (sel) => (sel === "[data-key]" ? keyButtons : []),
      createElement: () => makeElement(),
      addEventListener: () => {}
    },
    setTimeout: (fn, ms) => {
      const id = setTimeout(() => {
        timeouts.delete(id);
        fn();
      }, ms);
      timeouts.add(id);
      return id;
    },
    clearTimeout: (id) => {
      timeouts.delete(id);
      clearTimeout(id);
    },
    setInterval: (fn, ms) => {
      const id = setInterval(fn, ms);
      intervals.add(id);
      return id;
    },
    clearInterval: (id) => {
      intervals.delete(id);
      clearInterval(id);
    },
    Date,
    Math
  };
  sandbox.window = sandbox;

  try {
    vm.runInNewContext(appSrc, sandbox);
    const sim = sandbox.PassportSimulator;
    assert.ok(sim);

    // 1. bindPress ignores orphan pointerup (without pointerdown) and cleans up on pointercancel
    const initialPage = sim.state.page;
    keyButtons[2].dispatch("pointerup");
    assert.equal(sim.state.page, initialPage);

    keyButtons[2].dispatch("pointerdown");
    keyButtons[2].dispatch("pointercancel");
    keyButtons[2].dispatch("pointerup");
    assert.equal(sim.state.page, initialPage);

    // Valid click advances page
    keyButtons[2].dispatch("pointerdown");
    keyButtons[2].dispatch("pointerup");
    assert.equal(sim.state.page, 1);

    // 2. Gaming page renders graceful -- / N/A when optional fields are missing in live snapshot
    sim.state.page = 5; // Gaming page
    sim.parseSerialLine("PCSTAT|cpu=42|mem=50|disk=60|rx=125.4|tx=12.3|host=NoGpuHost");
    const screenHtml = elements.get("deviceScreen").innerHTML;
    assert.match(screenHtml, /42% · --/);
    assert.match(screenHtml, /CPU temp unavailable/);
    assert.match(screenHtml, />N\/A</);
    assert.match(screenHtml, /GPU unavailable/);
    assert.match(screenHtml, /-- Hz/);
    assert.equal(elements.get("gpuOutput").textContent, "--");
    assert.equal(elements.get("netOutput").textContent, "125.4 MB/s");

    // 3. Serial EOF cleans up port, resets serialConnected=false, and re-enables connect button,
    //    while dropping an overlong unterminated chunk (> 4096 chars) without unbounded buffer growth
    sim.setSerialMode();
    nextPortBehavior = () => [
      "X".repeat(5000),
      "TAIL_DROPPED\nPCSTAT|cpu=77|mem=33|disk=22|rx=25.0|tx=1.0|host=SerialBox\n"
    ];
    await sim.connectSerial();
    await new Promise((r) => setTimeout(r, 30));

    assert.equal(sim.state.values.cpu, 77);
    assert.equal(sim.state.values.host, "SerialBox");
    assert.equal(sim.state.values.net, 250);
    assert.equal(sim.state.serialBuffer.length, 0);
    assert.equal(sim.state.serialConnected, false);
    assert.equal(elements.get("serialConnect").disabled, false);
    assert.equal(ports[0].closed, true);
    assert.equal(ports[0].lockReleased, true);

    // 4. Switching from active Serial connection to Mock or Bridge calls closeSerialPort()
    sim.setSerialMode();
    nextPortBehavior = () => ["__WAIT__"];
    await sim.connectSerial();
    await new Promise((r) => setTimeout(r, 15));
    assert.equal(sim.state.serialConnected, true);

    sim.connectBridge();
    await new Promise((r) => setTimeout(r, 20));
    assert.equal(sim.state.serialConnected, false);
    assert.equal(ports[1].cancelled, true);
    assert.equal(ports[1].lockReleased, true);
    assert.equal(ports[1].closed, true);

    // 5. Bridge stream.onerror clears state.live and re-renders offline WAIT screen
    const stream = eventSources[eventSources.length - 1];
    stream.onopen();
    stream.onmessage({ data: "PCSTAT|cpu=19|mem=30|disk=40|rx=5.0|tx=1.0|host=LivePC" });
    assert.equal(sim.state.live, true);
    assert.match(elements.get("deviceScreen").innerHTML, /LivePC/);

    stream.onerror();
    assert.equal(sim.state.bridgeConnected, false);
    assert.equal(sim.state.live, false);
    assert.match(elements.get("deviceScreen").innerHTML, /WAIT/);
    assert.match(elements.get("deviceScreen").innerHTML, /no live snapshot yet/);

    // 6. Returning to Mock mode closes bridge stream and restores non-null mock Gaming values
    sim.setMockMode();
    assert.equal(stream.closed, true);
    assert.equal(typeof sim.state.values.gpu, "number");
    assert.equal(typeof sim.state.values.hz, "number");
  } finally {
    for (const id of timeouts) clearTimeout(id);
    for (const id of intervals) clearInterval(id);
  }
});
