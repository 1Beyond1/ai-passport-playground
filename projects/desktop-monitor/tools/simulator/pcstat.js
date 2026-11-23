(function (root, factory) {
  const api = factory();
  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
  if (root) {
    root.PCStat = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : this, () => {
  "use strict";

  const MOCK_MEM_TOTAL_GB = 16;
  const MOCK_DISK_TOTAL_GB = 512;
  const MAX_RATE_TENTHS = 65535;
  const MAX_UINT16 = 65535;

  function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
  }

  function parseFiniteNumber(raw) {
    if (raw === undefined || raw === null) return null;
    const text = String(raw).trim();
    if (!text) return null;
    const num = Number(text);
    return Number.isFinite(num) ? num : null;
  }

  function formatGb(value) {
    if (typeof value !== "number" || !Number.isFinite(value) || value < 0) {
      return "--";
    }
    if (value >= 100 || Math.abs(value - Math.round(value)) < 0.05) {
      return String(Math.round(value));
    }
    return value.toFixed(1);
  }

  function applyMockCapacities(values) {
    const next = { ...values };
    next.memTotalGb = MOCK_MEM_TOTAL_GB;
    next.memUsedGb = Number(((clamp(next.mem, 0, 100) / 100) * MOCK_MEM_TOTAL_GB).toFixed(1));
    next.diskTotalGb = MOCK_DISK_TOTAL_GB;
    next.diskUsedGb = Math.round((clamp(next.disk, 0, 100) / 100) * MOCK_DISK_TOTAL_GB);
    return next;
  }

  function parsePcstatLine(line, baseValues) {
    const trimmed = String(line ?? "").trim();
    if (!trimmed.startsWith("PCSTAT|")) return null;

    const fields = {};
    trimmed.slice(7).split("|").forEach((part) => {
      const split = part.indexOf("=");
      if (split > 0) {
        fields[part.slice(0, split)] = part.slice(split + 1);
      }
    });

    const knownKeys = [
      "cpu", "mem", "disk", "gpu", "rx", "tx",
      "cpu_mhz", "gpu_mhz", "cpu_c", "gpu_c", "hz",
      "mem_used_gb", "mem_total_gb", "disk_used_gb", "disk_total_gb",
      "time", "uptime", "host"
    ];
    if (!knownKeys.some((key) => fields[key] !== undefined)) {
      return null;
    }

    const next = { ...baseValues };
    ["cpu", "mem", "disk"].forEach((key) => {
      const parsed = parseFiniteNumber(fields[key]);
      if (parsed !== null) next[key] = clamp(Math.round(parsed), 0, 100);
    });

    // Optional hardware telemetry is a snapshot, not a sticky patch.
    // Clear optional Gaming fields before parsing so missing sensors never
    // retain stale mock defaults or previous-host readings.
    next.gpu = null;
    next.cpuMhz = null;
    next.gpuMhz = null;
    next.cpuTemp = null;
    next.gpuTemp = null;
    next.hz = null;

    const gpu = parseFiniteNumber(fields.gpu);
    if (gpu !== null) next.gpu = clamp(Math.round(gpu), 0, 100);

    const rx = parseFiniteNumber(fields.rx);
    if (rx !== null) next.net = clamp(Math.round(rx * 10), 0, MAX_RATE_TENTHS);
    const tx = parseFiniteNumber(fields.tx);
    if (tx !== null) next.tx = clamp(Math.round(tx * 10), 0, MAX_RATE_TENTHS);

    const cpuMhz = parseFiniteNumber(fields.cpu_mhz);
    if (cpuMhz !== null) next.cpuMhz = clamp(Math.round(cpuMhz), 0, MAX_UINT16);
    const gpuMhz = parseFiniteNumber(fields.gpu_mhz);
    if (gpuMhz !== null) next.gpuMhz = clamp(Math.round(gpuMhz), 0, MAX_UINT16);

    const cpuTemp = parseFiniteNumber(fields.cpu_c);
    if (cpuTemp !== null) next.cpuTemp = clamp(Math.round(cpuTemp), -40, 150);
    const gpuTemp = parseFiniteNumber(fields.gpu_c);
    if (gpuTemp !== null) next.gpuTemp = clamp(Math.round(gpuTemp), -40, 150);

    const hz = parseFiniteNumber(fields.hz);
    if (hz !== null) next.hz = clamp(Math.round(hz), 0, MAX_UINT16);

    // Optional capacity telemetry is treated as a snapshot, not a sticky patch.
    // Legacy PCSTAT lines without *_gb keys clear capacities to null so the UI
    // degrades gracefully instead of showing stale mock or previous-host sizes.
    next.memUsedGb = null;
    next.memTotalGb = null;
    const memUsed = parseFiniteNumber(fields.mem_used_gb);
    const memTotal = parseFiniteNumber(fields.mem_total_gb);
    if (memUsed !== null && memTotal !== null && memTotal > 0 && memUsed >= 0) {
      next.memTotalGb = memTotal;
      next.memUsedGb = Math.min(memUsed, memTotal);
    }

    next.diskUsedGb = null;
    next.diskTotalGb = null;
    const diskUsed = parseFiniteNumber(fields.disk_used_gb);
    const diskTotal = parseFiniteNumber(fields.disk_total_gb);
    if (diskUsed !== null && diskTotal !== null && diskTotal > 0 && diskUsed >= 0) {
      next.diskTotalGb = diskTotal;
      next.diskUsedGb = Math.min(diskUsed, diskTotal);
    }

    if (fields.time) next.time = fields.time;
    if (fields.uptime) next.uptime = fields.uptime.replace(/^0+(?=\d)/, "") || "0m";
    if (fields.host) next.host = fields.host;

    return next;
  }

  function getMemoryDisplay(values, isMock = false) {
    const hasCapacity = (
      typeof values.memUsedGb === "number"
      && typeof values.memTotalGb === "number"
      && Number.isFinite(values.memUsedGb)
      && Number.isFinite(values.memTotalGb)
      && values.memTotalGb > 0
      && values.memUsedGb >= 0
    );
    if (hasCapacity) {
      const usedText = formatGb(values.memUsedGb);
      const totalText = formatGb(values.memTotalGb);
      const freeText = formatGb(Math.max(0, values.memTotalGb - values.memUsedGb));
      return {
        hasCapacity: true,
        overviewSub: `${usedText} / ${totalText} GB used`,
        pageSub: `used · ${usedText} / ${totalText} GB`,
        availableValue: `${freeText} GB`,
        availableSub: "available to apps",
        swapValue: isMock ? "0.0 GB" : "--",
        swapSub: isMock ? "under control" : "not in PCSTAT"
      };
    }
    return {
      hasCapacity: false,
      overviewSub: "memory used",
      pageSub: "memory used",
      availableValue: "--",
      availableSub: "capacity not in PCSTAT",
      swapValue: isMock ? "0.0 GB" : "--",
      swapSub: isMock ? "under control" : "not in PCSTAT"
    };
  }

  function getStorageDisplay(values) {
    const hasCapacity = (
      typeof values.diskUsedGb === "number"
      && typeof values.diskTotalGb === "number"
      && Number.isFinite(values.diskUsedGb)
      && Number.isFinite(values.diskTotalGb)
      && values.diskTotalGb > 0
      && values.diskUsedGb >= 0
    );
    if (hasCapacity) {
      const usedText = formatGb(values.diskUsedGb);
      const totalText = formatGb(values.diskTotalGb);
      const freeText = formatGb(Math.max(0, values.diskTotalGb - values.diskUsedGb));
      return {
        hasCapacity: true,
        overviewSub: `${usedText} / ${totalText} GB used`,
        pageSub: `used · ${usedText} / ${totalText} GB`,
        freeValue: `${freeText} GB`,
        freeSub: "available"
      };
    }
    return {
      hasCapacity: false,
      overviewSub: "system disk used",
      pageSub: "system disk used",
      freeValue: "--",
      freeSub: "capacity not in PCSTAT"
    };
  }

  function isFiniteMetric(value) {
    return typeof value === "number" && Number.isFinite(value);
  }

  function getGamingDisplay(values) {
    const hasCpuMhz = isFiniteMetric(values.cpuMhz);
    const hasCpuTemp = isFiniteMetric(values.cpuTemp);
    const hasGpu = isFiniteMetric(values.gpu);
    const hasGpuMhz = isFiniteMetric(values.gpuMhz);
    const hasGpuTemp = isFiniteMetric(values.gpuTemp);
    const hasAnyGpu = hasGpu || hasGpuMhz || hasGpuTemp;
    const hasHz = isFiniteMetric(values.hz);

    const cpuValue = hasCpuMhz ? `${values.cpu}% · ${values.cpuMhz} MHz` : `${values.cpu}% · --`;
    const cpuSub = hasCpuTemp ? `${values.cpuTemp} C temperature` : "CPU temp unavailable";

    let gpuValue = "N/A";
    let gpuSub = "GPU unavailable";
    let gpuPercent = null;
    if (hasAnyGpu) {
      if (hasGpu && hasGpuMhz) {
        gpuValue = `${values.gpu}% · ${values.gpuMhz} MHz`;
      } else if (hasGpu) {
        gpuValue = `${values.gpu}% · --`;
      } else if (hasGpuMhz) {
        gpuValue = `-- · ${values.gpuMhz} MHz`;
      } else {
        gpuValue = "--";
      }
      gpuSub = hasGpuTemp ? `${values.gpuTemp} C temperature` : "GPU temp unavailable";
      gpuPercent = hasGpu ? values.gpu : null;
    }

    return {
      cpuValue,
      cpuSub,
      cpuPercent: values.cpu,
      gpuValue,
      gpuSub,
      gpuPercent,
      displayValue: hasHz ? `${values.hz} Hz` : "-- Hz",
      displaySub: "current refresh rate"
    };
  }

  return {
    MOCK_MEM_TOTAL_GB,
    MOCK_DISK_TOTAL_GB,
    MAX_RATE_TENTHS,
    clamp,
    parseFiniteNumber,
    formatGb,
    applyMockCapacities,
    parsePcstatLine,
    getMemoryDisplay,
    getStorageDisplay,
    getGamingDisplay
  };
});
