English | [简体中文](README.zh_CN.md)

# AI Passport Playground

Independent experiments for the FoloToy AI Passport, each in its own project directory.
Based on [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport), under the MIT license. Original BSP and pixel mascot attribution is retained in LICENSE.

- [Orbital Post](projects/orbital-post/README.md): an offline courier passport with animated pixel art, six fictional letters and persistent postmarks.
- [Desktop Monitor](projects/desktop-monitor/README.md): the previous USB computer monitor, preserved with its Windows bridge and browser simulator.

Both target ESP32-C3, ESP-IDF 5.5.3 and a 240x320 display. Each directory is a standalone ESP-IDF project with its own BSP, dependency lock and partition table. They are alternative applications, not simultaneous apps.

Never erase flash or overwrite the protected `cardid` partition at `0x356000`. Device backups, identities, credentials and local logs are intentionally excluded. Hardware validation is distinct from compilation; see each project's limitations.
