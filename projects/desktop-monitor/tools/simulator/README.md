<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport Web Simulator

This is a browser-first test bench for the planned **Desktop Monitor** play. It is deliberately independent of ESP-IDF so visual layout, three-button navigation, long-press behavior, and the PC status protocol can be tested before flashing hardware.

## Run locally

From the repository root:

```powershell
python -m http.server 4173 --directory tools/simulator
```

Open [http://127.0.0.1:4173](http://127.0.0.1:4173) in Chrome or Edge.

The mock stream is enabled by default. Use the three on-screen buttons or `ArrowUp`, `ArrowDown`, and `Enter`. Hold OK for about 0.7 seconds to test long-OK return.

For live desktop data, choose **Computer Bridge** in the simulator. The bridge
publishes the same `PCSTAT` records to the device and to a localhost SSE stream,
so the browser does not compete with the device for the same COM port:

```powershell
python tools/desktop_bridge.py --port COM3
```

The simulator reads `http://127.0.0.1:8765/stream`. The bridge also exposes
`/health` for a quick check. **USB Serial** is reserved for a serial source that
itself emits `PCSTAT`; it will not mirror records that another process is
writing into the device.

## USB status protocol

The future desktop bridge and the device use one newline-delimited record per update:

```text
PCSTAT|cpu=37|gpu=61|cpu_mhz=4200|gpu_mhz=2400|cpu_c=72|gpu_c=68|hz=165|mem=58|disk=68|rx=1.2|tx=0.4|time=14:28|uptime=3h18m|host=Windows
```

`cpu`, `gpu`, `mem`, and `disk` are percentages; `cpu_mhz` and `gpu_mhz` are clock speeds; `cpu_c` and `gpu_c` are temperatures; `hz` is the current display refresh rate; `rx` and `tx` are megabytes per second. Optional hardware values are omitted when Windows cannot expose them. The browser's Web Serial mode can inspect a real COM device when the page is served from `localhost` and the browser supports Web Serial.

The simulator exposes a small test surface at `window.PassportSimulator`:

```js
PassportSimulator.press("down");
PassportSimulator.parseSerialLine("PCSTAT|cpu=72|gpu=84|cpu_mhz=4200|gpu_mhz=2400|cpu_c=72|gpu_c=68|hz=165|mem=44|disk=68|rx=2.1|tx=0.4|time=14:28|uptime=3h18m|host=Windows");
```

No firmware is flashed by this simulator. The current device firmware remains unchanged until the UI and protocol are accepted.

## Windows device bridge

Run the bundled bridge with the ESP32-C3 connected over USB Serial/JTAG:

```powershell
python tools/desktop_bridge.py --port COM3
```

It samples Windows once per second and continuously sends `PCSTAT` records. The bridge reads NVIDIA GPU telemetry through `nvidia-smi`, the primary display refresh rate through the Windows display API, and optional CPU temperature through `psutil` or LibreHardwareMonitor's WMI namespace. The bridge uses `psutil` and `pyserial`, reconnects after a cable or device reset, and opens the port without toggling DTR/RTS so the monitor does not reboot on every sample. The optional localhost stream is enabled by default and can be disabled with `--no-http`.
