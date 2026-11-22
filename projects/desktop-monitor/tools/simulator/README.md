English | [简体中文](README.zh_CN.md)

# AI Passport Web Simulator

This is a browser-first test bench for the **Desktop Monitor** application. It is deliberately independent of ESP-IDF so visual layout, three-button navigation, long-press behavior, and the PC status protocol can be tested before flashing hardware.

## Online Demo (GitHub Pages)

Try the simulator directly in your browser without local setup:

- **Live Demo**: [https://1beyond1.github.io/ai-passport-playground/](https://1beyond1.github.io/ai-passport-playground/)
- **Deployment**: Enable **Settings → Pages → Build and deployment → Source: GitHub Actions** in the repository. `.github/workflows/pages.yml` publishes this simulator directory automatically on pushes to `main`.
- **Browser Loopback & Web Serial Notes**:
  - In Chrome/Edge, the HTTPS Pages site can connect to the local bridge at `http://127.0.0.1:8765/stream` (loopback is treated as a trustworthy origin and `desktop_bridge.py` sets CORS + `Access-Control-Allow-Private-Network: true`). Firefox or Safari may block HTTPS-to-HTTP loopback requests; use the local server below if that happens.
  - Web Serial requires Chrome/Edge over **HTTPS** or `localhost` and only works with serial ports that emit `PCSTAT` directly.

## Run locally

From `projects/desktop-monitor`:

```powershell
python -m http.server 4173 --directory tools/simulator
```

Open [http://127.0.0.1:4173](http://127.0.0.1:4173) in Chrome or Edge.

The mock stream is enabled by default. Use the three on-screen buttons or `ArrowUp`, `ArrowDown`, and `Enter`. Hold OK for about 0.7 seconds to test long-OK return.

For live desktop data, choose **Computer Bridge** in the simulator. The bridge
publishes the same `PCSTAT` records to the device and to a localhost SSE stream,
so the browser does not compete with the device for the same COM port:

```powershell
python -m pip install -r tools/requirements.txt
python tools/desktop_bridge.py --port auto
```

Or double-click `tools/start_bridge.bat` on Windows. Use `--port COM3` / `--port /dev/ttyACM0` to target an explicit port, or `--http-only` to stream live PC telemetry to the browser simulator without an ESP32 connected.

The simulator reads `http://127.0.0.1:8765/stream`. The bridge also exposes
`/health` for a quick check. **USB Serial** is reserved for a serial source that
itself emits `PCSTAT`; it will not mirror records that another process is
writing into the device.

## USB status protocol

The desktop bridge and the device use one newline-delimited record per update:

```text
PCSTAT|cpu=37|gpu=61|cpu_mhz=4200|gpu_mhz=2400|cpu_c=72|gpu_c=68|hz=165|mem=58|disk=68|rx=1.2|tx=0.4|mem_used_gb=9.3|mem_total_gb=16.0|disk_used_gb=341.2|disk_total_gb=512.0|time=14:28|uptime=3h18m|host=Windows
```

`cpu`, `gpu`, `mem`, and `disk` are percentages; `cpu_mhz` and `gpu_mhz` are clock speeds; `cpu_c` and `gpu_c` are temperatures; `hz` is the current display refresh rate; `rx` and `tx` are megabytes per second; `mem_used_gb`, `mem_total_gb`, `disk_used_gb`, and `disk_total_gb` are optional real capacity fields in GB (backward-compatible: the ESP32 `desktop_state.c` parser ignores unknown keys, and the simulator degrades gracefully when legacy frames omit them). Optional hardware values are omitted when the OS cannot expose them. The browser's Web Serial mode can inspect a real COM device when the page is served from HTTPS or `localhost` and the browser supports Web Serial.

The simulator exposes a small test surface at `window.PassportSimulator`:

```js
PassportSimulator.press("down");
PassportSimulator.parseSerialLine("PCSTAT|cpu=72|gpu=84|cpu_mhz=4200|gpu_mhz=2400|cpu_c=72|gpu_c=68|hz=165|mem=44|disk=68|rx=2.1|tx=0.4|mem_used_gb=14.1|mem_total_gb=32.0|disk_used_gb=348.0|disk_total_gb=512.0|time=14:28|uptime=3h18m|host=Windows");
```

No firmware is flashed by this simulator. The current device firmware remains unchanged until the UI and protocol are accepted.

## Windows device bridge

Run the bundled bridge with the ESP32-C3 connected over USB Serial/JTAG (or double-click `tools/start_bridge.bat`):

```powershell
python tools/desktop_bridge.py --port auto
```

It samples the host once per second and continuously sends `PCSTAT` records. With `--port auto`, it auto-detects Espressif USB Serial/JTAG devices (VID `0x303A`) via `serial.tools.list_ports` and automatically rescans/reconnects after cable unplug or device reset (explicit `--port COM3` / `/dev/ttyACM0` and `--http-only` are also supported). The bridge reads NVIDIA GPU telemetry through `nvidia-smi`, the primary display refresh rate through the Windows display API, and optional CPU temperature through `psutil` or LibreHardwareMonitor. It uses `psutil` and `pyserial`, opens the port without toggling DTR/RTS so the monitor does not reboot on every sample, and enables the localhost stream by default (disable with `--no-http`).
