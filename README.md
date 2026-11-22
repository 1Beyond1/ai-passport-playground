English | [简体中文](README.zh_CN.md)

# AI Passport Playground

Independent experiments for the FoloToy AI Passport, each in its own project directory.
Based on [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport), under the MIT license. Original BSP and pixel mascot attribution is retained in [LICENSE](LICENSE).

- [Pocket Universe / Orbital Post](projects/orbital-post/README.md): a Chinese-first four-tool collection with interstellar letters, mood signs, randomizers, and a reaction game.
- [Desktop Monitor](projects/desktop-monitor/README.md): the previous USB computer monitor, preserved with its Windows bridge and browser simulator.

Both target ESP32-C3, ESP-IDF 5.5.3 and a 240x320 display. Each directory is a standalone ESP-IDF project with its own BSP, dependency lock and partition table. They are alternative applications, not simultaneous apps.

Never erase flash or overwrite the protected `cardid` partition at `0x356000`. Device backups, identities, credentials and local logs are intentionally excluded. Hardware validation is distinct from compilation; see each project's limitations.

## Preview

**Desktop Monitor** includes a browser-based `240 × 320` display simulator with three-key (`↑` / `OK` / `↓`) interactive controls, mock telemetry sliders, and transport mode switching (`Mock`, `Computer Bridge`, `USB Serial`) for verifying UI and input flow before flashing:

![Desktop Monitor 240×320 Web Simulator](docs/screenshots/desktop-monitor-simulator.png)
