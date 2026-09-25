# Qt 6 Windows 卸载器模板

这是一个面向 Windows 的 Qt 6/C++ 卸载器模板。界面使用 Qt Widgets，安装目录的最终删除由独立的 `cleanup_helper.exe` 完成，因此卸载器可以安全删除自身。

## 功能

- 图形化确认、进度和操作日志
- 按进程名结束目标应用
- 删除快捷方式、用户数据和卸载注册项
- 用户可选择是否保留设置、缓存和登录状态
- 主程序退出后删除整个安装目录
- 对磁盘根目录、用户主目录和非白名单注册表路径做安全拦截
- 支持静默卸载、管理员自动提权、环境变量与 PATH 精确清理
- 在 `%TEMP%\<appId>-uninstall.log` 保存诊断日志

## 配置

编辑 `uninstall-config.json`：

- `appName`：界面显示名称。
- `appId`：只允许字母、数字、点、下划线和连字符，用于临时目录名。
- `displayVersion`、`publisher`：界面显示的版本和发布者。
- `installRootRelative`：安装根目录相对于卸载器 EXE 的位置，仅允许 `.` 或 `..`。当前打包布局使用 `..`。
- `processNames`：要结束的可执行文件名，必须是文件名而不是路径。
- `userDataPaths`：勾选“同时删除”后清理的目录，仅允许用户主目录内的子路径。
- `shortcutPaths`：快捷方式的完整路径。
- `registryKeys`：仅允许 HKCU/HKLM 的 Windows 卸载注册表分支。
- `pathEntries`：要从用户级或系统级 PATH 中精确移除的项目。
- `environmentVariables`：要删除的用户级或系统级环境变量名称。
- `removeInstallDirectory`：主窗口退出后是否删除安装目录。
- `removeUserDataByDefault`：是否默认勾选删除用户数据。
- `requiresAdmin`：是否自动请求管理员权限。仅在需要清理 HKLM、系统 PATH 或受保护目录时开启。

支持 `%APPDATA%`、`%LOCALAPPDATA%`、`%USERPROFILE%` 等环境变量。请把 JSON 与两个 EXE 放在同一安装目录。

## 使用 Qt 6 + MSVC 构建

先打开 x64 Native Tools Command Prompt for VS 2022，并确保 `cmake` 和 `ninja` 位于 `PATH`：

```bat
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64
cmake --build build
cmake --install build --prefix "%CD%\package"
```

如果 Qt 安装在其他位置，请相应修改 `CMAKE_PREFIX_PATH`。也可以使用 Qt Creator 直接打开根目录的 `CMakeLists.txt`。

打包结果位于 `package`：卸载器、辅助程序、配置和 Qt 运行库位于 `package/bin`，Qt 插件位于其他子目录。根目录中的 `uninstaller.marker` 是防止误删目录的安全标记，不要移动、改名或删除。把 `Qt6Uninstaller.exe` 改名为你需要的名称不会影响功能。

## 静默卸载

```bat
Qt6Uninstaller.exe --silent --keep-user-data
Qt6Uninstaller.exe --silent --remove-user-data
```

成功退出码为 `0`；部分清理失败为 `2`；安全标记缺失为 `3`；提权失败为 `5`；命令行参数冲突为 `64`。

## 权限说明

示例使用当前用户（HKCU）的卸载注册表项，不要求管理员权限。如果你的应用安装在 `Program Files`、使用 HKLM 注册表项或需要结束高权限进程，请让卸载器以管理员身份运行，并给最终程序添加合适的 Windows manifest。不要在没有必要时强制申请管理员权限。

## 集成建议

安装程序应保留打包目录结构，并创建 Windows“应用和功能”卸载项。推荐设置 `UninstallString` 为 `Qt6Uninstaller.exe`，`QuietUninstallString` 为 `Qt6Uninstaller.exe --silent --keep-user-data`。
