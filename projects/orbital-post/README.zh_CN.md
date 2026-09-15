[English](README.md) | 简体中文

# 轨道邮局 · Orbital Post

像素邮差 PIP-01 的离线小护照，从六个虚构地点收集来信。不需要账号、联网、音乐文件或云服务。这是一个短篇互动小品，不是自动生成的游戏，也不是真实无线电接收器。

## 操作

- 证件／收藏页：上下键切页；确认键进入目的地。
- 目的地：上下键选地点；确认键开始五秒投递。
- 抵达后：阅读来信；确认键查看邮戳收藏。
- 任意页面长按确认：取消并回到证件。

邮差会眨眼和跳动。每个地点首次投递后，在普通 NVS 的 `orbit_post` 命名空间保存邮戳。写入由工作任务执行，不阻塞按钮回调。存储初始化失败则只保存本次会话；刚完成时立即断电可能丢失尚未完成的写入。本版没有自动息屏，也没有重新定义实体电源开关。不播放音效、不进行真实无线扫描。

## 编译

激活 ESP-IDF **5.5.3**，在本项目目录执行：

```sh
idf.py build
idf.py -p YOUR_PORT flash
```

已经使用相同分区布局的设备，可以只更新应用，保留现有数据：

```sh
python -m esptool --chip esp32c3 --port YOUR_PORT --baud 115200 write_flash 0x10000 build/FoloToy-AI-Passport.bin
```

不要执行 erase-flash。`cardid` 固定在 `0x356000`，应用上限 3 MB。替换前请备份当前应用。

## 逻辑测试

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_orbit_state.c main/orbit_state.c -o test_orbit
./test_orbit
```

测试覆盖导航、计时回绕、取消、重复投递和六枚邮戳集齐。显示裁切、实体按钮反馈、续航及断电持久化仍需实机检查。
