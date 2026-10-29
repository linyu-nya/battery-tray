# Battery Tray

这个项目是 vibecoding 产物，为了能在不打开罗技官方驱动时也能看到鼠标电量。

目前只验证过 **PRO X SUPERLIGHT 2 SE**（GPW2 金刚版）。理论上来说，使用相同 HID++ 电量协议的型号可能直接可用，不过没有验证。

从 [Releases](https://github.com/linyu-nya/battery-tray/releases/latest) 下载 `BatteryTray.exe`，双击运行即可（Windows 10/11 x64）。

从源码构建：在 PowerShell 中执行 `./build.ps1`，产物位于 `bin/BatteryTray.exe`。

开源协议：[MIT](LICENSE)。
