[English](README.md) | 简体中文

# AI Passport 网页模拟器

这是为 **Desktop Monitor** 应用准备的浏览器优先测试台。它独立于 ESP-IDF，可在刷写硬件前验证视觉布局、三键导航、长按行为和电脑状态协议。

## 在线体验站（GitHub Pages）

无需本地搭环境，可直接在浏览器打开在线模拟器：

- **在线体验链接**：[https://1beyond1.github.io/ai-passport-playground/](https://1beyond1.github.io/ai-passport-playground/)
- **部署开启方式**：在仓库 **Settings → Pages → Build and deployment → Source** 中选择 **GitHub Actions**，推送到 `main` 后 `.github/workflows/pages.yml` 会自动发布本目录静态页面。
- **浏览器 Loopback 与 Web Serial 说明**：
  - 在 Chrome / Edge 中，HTTPS 在线体验站可直接连接本机 `http://127.0.0.1:8765/stream`（`127.0.0.1` 属于可信 loopback 来源，且桥接返回 CORS 与 `Access-Control-Allow-Private-Network: true`）。Firefox / Safari 可能因混合内容策略阻止访问本机 HTTP，此时请改用下方本地 HTTP 服务。
  - Web Serial 仅在 Chrome / Edge 的 **HTTPS** 或 `localhost` 页面下可用，且仅适用于串口本身主动输出 `PCSTAT` 的设备。

## 本地运行

在 `projects/desktop-monitor` 目录执行：

```powershell
python -m http.server 4173 --directory tools/simulator
```

然后使用 Chrome 或 Edge 打开 [http://127.0.0.1:4173](http://127.0.0.1:4173)。

默认启用模拟数据流。可以使用设备上的三个模拟按键，也可以使用 `ArrowUp`、`ArrowDown` 和 `Enter`。按住 OK 约 0.7 秒可测试长按返回总览。

要读取电脑实时数据，请在模拟器里选择“电脑桥接”。桥接程序会把同一份
`PCSTAT` 同时写入设备，并发布到本机 SSE 数据流；浏览器不会再和设备争抢同一个串口：

```powershell
python -m pip install -r tools/requirements.txt
python tools/desktop_bridge.py --port auto
```

在 Windows 上也可直接双击 `tools/start_bridge.bat` 一键启动。如需手动指定串口可传 `--port COM3` / `--port /dev/ttyACM0`；无 ESP32 硬件时可传 `--http-only` 仅向浏览器模拟器推送实时电脑指标。

模拟器读取 `http://127.0.0.1:8765/stream`，桥接程序还提供 `/health` 供快速检查。
“USB 串口”只适用于串口本身会主动输出 `PCSTAT` 的数据源；如果另一个进程正在把数据写入设备，
这个模式不会把写入内容镜像出来。

## USB 状态协议

电脑端桥接程序和设备之间使用每行一条记录的协议：

```text
PCSTAT|cpu=37|gpu=61|cpu_mhz=4200|gpu_mhz=2400|cpu_c=72|gpu_c=68|hz=165|mem=58|disk=68|rx=1.2|tx=0.4|mem_used_gb=9.3|mem_total_gb=16.0|disk_used_gb=341.2|disk_total_gb=512.0|time=14:28|uptime=3h18m|host=Windows
```

`cpu`、`gpu`、`mem` 和 `disk` 使用百分比；`cpu_mhz`、`gpu_mhz` 是频率；`cpu_c`、`gpu_c` 是温度；`hz` 是当前显示器刷新率；`rx` 和 `tx` 使用 MB/s；`mem_used_gb`、`mem_total_gb`、`disk_used_gb`、`disk_total_gb` 为可选的真实 GB 容量扩展字段（完全向后兼容：ESP32 固件 `desktop_state.c` 会自动忽略未知字段，模拟器在收到不含 `*_gb` 的旧格式帧时也会优雅降级）。系统无法提供的硬件字段会被省略。浏览器的 Web Serial 模式可以在页面通过 HTTPS 或 `localhost` 提供、且浏览器支持 Web Serial 时读取真实串口。

模拟器通过 `window.PassportSimulator` 暴露少量测试接口：

```js
PassportSimulator.press("down");
PassportSimulator.parseSerialLine("PCSTAT|cpu=72|gpu=84|cpu_mhz=4200|gpu_mhz=2400|cpu_c=72|gpu_c=68|hz=165|mem=44|disk=68|rx=2.1|tx=0.4|mem_used_gb=14.1|mem_total_gb=32.0|disk_used_gb=348.0|disk_total_gb=512.0|time=14:28|uptime=3h18m|host=Windows");
```

模拟器不会刷写固件。当前设备固件保持不变，直到 UI 和协议确认通过。

## Windows 设备桥接

将 ESP32-C3 通过 USB Serial/JTAG 连接后运行（或直接双击 `tools/start_bridge.bat`）：

```powershell
python tools/desktop_bridge.py --port auto
```

桥接程序每秒采集一次电脑状态并持续发送 `PCSTAT`。使用 `--port auto` 时会通过 `serial.tools.list_ports` 按 Espressif USB Serial/JTAG VID `0x303A` 自动识别串口，并在设备拔插或复位后优雅重扫重连（同时兼容显式 `--port COM3` / `/dev/ttyACM0` 与 `--http-only`）。GPU 数据通过 `nvidia-smi` 读取，主显示器刷新率通过 Windows 显示 API 读取，CPU 温度优先使用 `psutil`，也支持运行中的 LibreHardwareMonitor。它使用 `psutil` 和 `pyserial`，并在打开串口时关闭 DTR/RTS，避免每次采样都触发监控设备重启。默认同时开启本机浏览器数据流，如不需要可加 `--no-http`。
