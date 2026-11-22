English | [简体中文](README.zh_CN.md)

# Desktop Monitor (preserved snapshot)

The previous USB telemetry application, preserved before Orbital Post. Build with ESP-IDF 5.5.3 using `idf.py build` in this directory. Do not erase flash or change the protected `cardid` partition.

Run the bridge on Windows:

```sh
python -m pip install psutil pyserial
python tools/desktop_bridge.py --port COM3
```

Use the actual device port. The bridge serves live simulator data at `http://127.0.0.1:8765/stream`. See [simulator instructions](tools/simulator/README.md). CPU temperature requires a supported sensor source such as LibreHardwareMonitor; NVIDIA telemetry uses `nvidia-smi`. Display Hz is not game FPS.

This is an archived prototype, not a polished release. Some secondary memory/storage fields are hard-coded placeholders; only fields supplied by PCSTAT are live. The simulator and firmware can differ. Preserving this snapshot does not certify those placeholders as real telemetry. Local device firmware backups are intentionally not published.

## Simulator Preview

![Desktop Monitor 240×320 Web Simulator](../../docs/screenshots/desktop-monitor-simulator.png)

*Browser-based `240 × 320` display simulation with three-key (`↑` / `OK` / `↓`) input testing and mock telemetry controls (see [simulator instructions](tools/simulator/README.md)).*
