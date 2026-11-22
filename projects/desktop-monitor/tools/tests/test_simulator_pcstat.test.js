"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

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
