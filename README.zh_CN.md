[English](README.md) | 简体中文

# AI Passport Playground

FoloToy AI Passport 独立实验合集，每个项目放在自己的文件夹。
基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)，遵循 MIT 许可。[LICENSE](LICENSE) 保留原始 BSP 与像素角色的作者声明。

- [小宇宙 / 轨道邮局](projects/orbital-post/README.zh_CN.md)：中文四功能合集，包含星际来信、心情挂牌、随机器和反应测试。
- [电脑监控](projects/desktop-monitor/README.zh_CN.md)：保留之前的 USB 监控程序、Windows 桥接和浏览器模拟器。

两个项目均使用 ESP32-C3、ESP-IDF 5.5.3 和 240x320 屏幕。各目录都是独立 ESP-IDF 项目，包含 BSP、依赖锁文件和分区表；两者是替代应用，不能同时运行。

不要擦除 Flash，也不要覆盖 `0x356000` 的受保护 `cardid` 分区。仓库不含设备备份、身份数据、凭据和本机日志。编译成功不等于硬件验证，使用限制见各项目说明。

## 界面预览与在线体验

**电脑监控（Desktop Monitor）**提供可在浏览器直接运行的 `240 × 320` 网页模拟器，支持三键（`↑` / `OK` / `↓`）交互测试、模拟数据滑块调节以及数据源切换（模拟数据 / 电脑桥接 / USB 串口），便于在刷入硬件前验证页面布局与按键逻辑：

- **在线体验站（GitHub Pages）**：[https://1beyond1.github.io/ai-passport-playground/](https://1beyond1.github.io/ai-passport-playground/)
- **开启 GitHub Pages 部署**：在仓库 **Settings → Pages → Build and deployment → Source** 中选择 **GitHub Actions**，推送到 `main` 后 `.github/workflows/pages.yml` 会自动发布 `projects/desktop-monitor/tools/simulator/`。
- **浏览器 Loopback 与 Web Serial 注意事项**：
  - **电脑桥接（`http://127.0.0.1:8765/stream`）**：在 Chrome / Edge 打开 HTTPS 在线体验站时可直接连接本机桥接（`127.0.0.1` 属于可信 loopback 来源，且桥接已返回 CORS 与 Private Network Access 响应头）。Firefox / Safari 可能因混合内容策略拦截 HTTPS 页面访问本机 HTTP，如遇拦截请改用本地静态服务 `python -m http.server 4173 --directory projects/desktop-monitor/tools/simulator`。
  - **Web Serial**：仅在 Chrome / Edge 的 **HTTPS**（含 GitHub Pages）或 `localhost` 下可用，且仅适用于串口本身主动输出 `PCSTAT` 的设备；不要与 `desktop_bridge.py` 争抢同一个 COM 串口。

![Desktop Monitor 240×320 网页模拟器预览](docs/screenshots/desktop-monitor-simulator.png)

## 自动化回归检查（CI）

`.github/workflows/ci.yml` 会在推送或 PR 到 `main` 时自动执行：
- C11 状态机与协议解析单测（`test_orbit_state.c`、`test_pocket_state.c`、`test_desktop_state.c`）
- Python 桥接脚本编译检查（`py_compile`）与单测（`unittest`）
- 网页模拟器 JS 语法检查（`node --check`）与 `PCSTAT` 解析单测（`node --test`）
