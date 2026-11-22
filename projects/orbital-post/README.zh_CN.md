[English](README.md) | 简体中文

# 小宇宙 / 轨道邮局

AI Passport 的中文四功能离线合集，包含汉化星际邮局、随身心情挂牌、随机决定工具和反应测试。不需要账号、联网、音乐文件或云服务。

## 包含功能

- 星际邮局：收集六封虚构来信和断电保留的邮戳。
- 心情挂牌：六种大字彩色状态，适合挂在身上展示。
- 随机器：抛硬币、六面骰子和六种简短建议。
- 反应测试：等待屏幕变绿，测量按键反应时间。

## 操作

- 首页：上下键选择，确认键进入。
- 功能内部：上下键换选项，确认键执行。
- 长按确认返回首页。
- 90 秒无操作自动熄灭背光；熄屏后的第一次按键只负责亮屏。

邮差会眨眼和跳动。每个地点首次投递后，在普通 NVS 的 `orbit_post` 命名空间保存邮戳。写入由工作任务执行，不阻塞按钮回调。存储初始化失败则只保存本次会话；刚完成时立即断电可能丢失尚未完成的写入。本应用没有重新定义实体电源开关。不播放音效、不进行真实无线扫描。

16 和 28 像素精简中文字库由思源黑体简体中文生成，遵循 SIL OFL 1.1；许可副本位于 [assets/fonts/SourceHanSansSC-OFL.txt](assets/fonts/SourceHanSansSC-OFL.txt)。

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
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_pocket_state.c main/pocket_state.c -o test_pocket
./test_pocket
```

测试覆盖菜单与功能导航、随机数范围、反应计时及回绕、取消、重复投递和六枚邮戳集齐。显示裁切、实体按钮反馈、续航及断电持久化仍需实机检查。
