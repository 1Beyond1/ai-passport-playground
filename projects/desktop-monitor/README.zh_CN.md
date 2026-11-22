[English](README.md) | 简体中文

# 电脑监控（保留快照）

开发轨道邮局前的 USB 遥测应用。在本目录用 ESP-IDF 5.5.3 执行 `idf.py build` 编译。不要擦除 Flash 或修改受保护的 `cardid` 分区。

在 Windows（或 Linux / macOS）启动桥接：

```sh
python -m pip install -r tools/requirements.txt
python tools/desktop_bridge.py --port auto
```

在 Windows 上也可直接双击 `tools/start_bridge.bat`，脚本会自动检查并安装依赖，然后以 `--port auto` 启动串口桥接与本机 SSE 服务。

- `--port auto`（默认）通过 USB VID `0x303A` 自动识别 Espressif USB Serial/JTAG 串口，设备拔插或复位后会自动重新扫描并重连。
- 完全兼容显式指定串口（`--port COM3` / `--port /dev/ttyACM0`），并支持 `--http-only`（无硬件时仅启动本机浏览器 SSE 数据流）。
- 桥接通过 `http://127.0.0.1:8765/stream`（及 `/health`）提供模拟器实时数据，并在 `PCSTAT` 帧中向后兼容地附加真实容量字段（`mem_used_gb`、`mem_total_gb`、`disk_used_gb`、`disk_total_gb`）。网页模拟器会展示真实内存／磁盘 GB 容量（收到不含 `*_gb` 的旧格式时优雅降级）；保留的 ESP32 固件解析器（`desktop_state.c`）会自动忽略未知键名且总帧长严格控制在 256 字节以内。
- CPU 温度需要 LibreHardwareMonitor 等支持的传感器来源；NVIDIA 数据使用 `nvidia-smi`。屏幕刷新率 Hz 不是游戏 FPS。

这是归档固件原型快照，不是完善发行版。设备固件端的部分次要内存／存储卡片仍为写死占位值，仅 `desktop_state.c` 解析的字段为硬件实时数据；网页模拟器在连接新版 `desktop_bridge.py` 时会显示真实内存与磁盘容量。保存快照不代表把固件占位值认证为真实遥测。本地设备固件备份不公开上传。

## 在线体验与网页模拟器预览

- **在线体验站（GitHub Pages）**：[https://1beyond1.github.io/ai-passport-playground/](https://1beyond1.github.io/ai-passport-playground/)
- **开启 Pages 部署**：在仓库 **Settings → Pages → Build and deployment → Source** 选择 **GitHub Actions**（由 `.github/workflows/pages.yml` 自动部署）。
- **浏览器注意事项**：Chrome / Edge 可在 HTTPS 在线体验站连接本机 `http://127.0.0.1:8765/stream`，并在 HTTPS 或 `localhost` 下支持 Web Serial；若 Firefox / Safari 因混合内容限制拦截访问本机 HTTP，请在本地运行 `python -m http.server 4173 --directory tools/simulator`。详见[模拟器说明](tools/simulator/README.zh_CN.md)。

![Desktop Monitor 240×320 网页模拟器预览](../../docs/screenshots/desktop-monitor-simulator.png)

*浏览器端 `240 × 320` 屏幕仿真、三键（`↑` / `OK` / `↓`）交互测试与模拟数据控制面板（详见[模拟器说明](tools/simulator/README.zh_CN.md)）。*

## 逻辑与协议测试

在 `projects/desktop-monitor` 目录执行：

```sh
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Imain tests/test_desktop_state.c main/desktop_state.c -o test_desktop_state
./test_desktop_state
python -m py_compile tools/desktop_bridge.py tools/tests/test_desktop_bridge.py
python -m unittest discover -s tools/tests -p "test_*.py" -v
node --check tools/simulator/pcstat.js && node --check tools/simulator/app.js
node --test tools/tests/test_simulator_pcstat.test.js
```
