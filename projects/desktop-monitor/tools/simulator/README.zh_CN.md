[English](README.md) | 简体中文

# AI Passport 网页模拟器

这是为 **Desktop Monitor** 应用准备的浏览器优先测试台。它独立于 ESP-IDF，可在刷写硬件前验证视觉布局、三键导航、长按行为和电脑状态协议。

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
python tools/desktop_bridge.py --port COM3
```

模拟器读取 `http://127.0.0.1:8765/stream`，桥接程序还提供 `/health` 供快速检查。
“USB 串口”只适用于串口本身会主动输出 `PCSTAT` 的数据源；如果另一个进程正在把数据写入设备，
这个模式不会把写入内容镜像出来。

## USB 状态协议

电脑端桥接程序和设备之间使用每行一条记录的协议：

```text
PCSTAT|cpu=37|gpu=61|cpu_mhz=4200|gpu_mhz=2400|cpu_c=72|gpu_c=68|hz=165|mem=58|disk=68|rx=1.2|tx=0.4|time=14:28|uptime=3h18m|host=Windows
```

`cpu`、`gpu`、`mem` 和 `disk` 使用百分比；`cpu_mhz`、`gpu_mhz` 是频率；`cpu_c`、`gpu_c` 是温度；`hz` 是当前显示器刷新率；`rx` 和 `tx` 使用 MB/s。Windows 无法提供的硬件字段会被省略。浏览器的 Web Serial 模式可以在页面通过 `localhost` 提供、且浏览器支持 Web Serial 时读取真实串口。

模拟器通过 `window.PassportSimulator` 暴露少量测试接口：

```js
PassportSimulator.press("down");
PassportSimulator.parseSerialLine("PCSTAT|cpu=72|gpu=84|cpu_mhz=4200|gpu_mhz=2400|cpu_c=72|gpu_c=68|hz=165|mem=44|disk=68|rx=2.1|tx=0.4|time=14:28|uptime=3h18m|host=Windows");
```

模拟器不会刷写固件。当前设备固件保持不变，直到 UI 和协议确认通过。

## Windows 设备桥接

将 ESP32-C3 通过 USB Serial/JTAG 连接后运行：

```powershell
python tools/desktop_bridge.py --port COM3
```

桥接程序每秒采集一次 Windows 状态并持续发送 `PCSTAT`。GPU 数据通过 `nvidia-smi` 读取，主显示器刷新率通过 Windows 显示 API 读取，CPU 温度优先使用 `psutil`，也支持运行中的 LibreHardwareMonitor WMI 命名空间。它使用 `psutil` 和 `pyserial`，线缆或设备复位后会自动重连，并在打开串口时关闭 DTR/RTS，避免每次采样都触发监控设备重启。默认同时开启本机浏览器数据流，如不需要可加 `--no-http`。
