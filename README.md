# MediaPreviewClient

一个基于 Qt6 的 Win11 本地媒体预览客户端，支持图片、视频、LIVP，并针对大目录与兼容性场景做了专门优化。

## 特性概览

- 支持格式:
	- 图片: `jpg` `jpeg` `png` `bmp` `gif` `webp` `heic`
	- 视频: `mp4` `mov` `m4v` `avi` `mkv`
	- 实况: `livp`
- 打开单文件后自动加载同目录文件
- 预览区支持图片缩放、视频播放、进度、音量、快捷键
- 底部相册画板:
	- 占位图优先，真实缩略图异步回填
	- 视频显示首帧并保留播放按钮标识
	- LIVP 支持策略切换（优先视频 / 优先静态图）
	- 当前项稳定居中
- 历史浏览模块:
	- 左侧“浏览记录”面板
	- 按每 50 条自动分组
	- 默认仅展开最后一组，其他组折叠
	- 历史回看会继续记录（不去重）
- 图标工程化:
	- 主窗口图标（Qt 资源）
	- Windows 任务栏/可执行文件图标（RC 资源）

## 交互与快捷键

- `Space`: 播放/暂停
- `Left` / `Right`: 快退/快进 5 秒
- `Up` / `Down`: 上一项/下一项
- 鼠标滚轮（图片模式）: 缩放

## 相册画板算法要点

- 可见区驱动: 只优先处理当前可见范围和近邻项
- 队列回填: 先占位、后异步补图，避免一次性重渲染卡顿
- 双缓存:
	- 内存缓存: 当前会话快速命中
	- 磁盘缓存: 跨会话复用（带版本键）
- 选中时居中、滚动时不回跳

## LIVP / HEIC 兼容策略

- LIVP 优先按压缩包结构提取候选图片/视频
- 支持 `LIVP优先视频` 开关
- 视频播放失败或超时时自动回退静态图（若可用）
- HEIC 无系统解码时自动转码（`ffmpeg` / `magick`）

## 项目结构

- `main.cpp`: 程序入口与应用图标
- `mainwindow.cpp`: 主窗口初始化与日志
- `mainwindow_ui.cpp`: UI 构建与信号连接
- `mainwindow_interaction.cpp`: 交互、选择联动、历史记录
- `mainwindow_media.cpp`: 媒体解析、LIVP/HEIC 处理
- `mainwindow_thumbnails.cpp`: 相册缩略图渲染、缓存与队列
- `resources.qrc`: Qt 图标资源
- `app_icon.rc`: Windows 可执行文件图标资源

## 环境要求

- 操作系统: Windows 11（当前开发机为 x86 架构）
- Qt: 6.5+（`Widgets` `Multimedia` `MultimediaWidgets`）
- CMake: 3.16+
- 编译器: MinGW 或 MSVC

## 构建与运行

### 使用 CMake Preset（推荐）

```powershell
cmake --preset mingw-debug
cmake --build --preset build-mingw-debug
```

### 通用 CMake

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="D:/Qt/6.x/mingw_64"
cmake --build build --config Release
```

### 运行

```powershell
.\build\mingw-debug\MediaPreviewClient.exe
```

> 如果链接阶段出现 `Permission denied`，请先关闭正在运行的 `MediaPreviewClient.exe`。

## VS Code / Qt Creator

- VS Code: 已提供 `CMakePresets.json` 与 `.vscode` 调试配置
- Qt Creator:
	- 可直接打开 `CMakeLists.txt`
	- 或打开 `MediaPreviewClient.pro`（qmake）

## 已知问题与说明

- HEIC 转码依赖外部工具，若无 `ffmpeg`/`magick`，部分 HEIC 可能无法渲染
- 历史记录当前为内存态，重启后不保留（后续可扩展持久化）

## 开源发布建议

- 本仓库已提供 `LICENSE`（MIT）与 `CONTRIBUTING.md`
- 若仓库包含自定义图标/素材，请确认其版权和再分发许可

## 日志排查

- 程序启动后会写入 `preview.log`（状态栏会显示路径）
- 关键日志包括: LIVP 解包、转码、播放器状态与错误、回退链路

查找日志示例:

```powershell
Get-ChildItem $env:APPDATA,$env:LOCALAPPDATA -Recurse -File -Filter preview.log -ErrorAction SilentlyContinue | Select-Object -First 5 -ExpandProperty FullName
```

## 文档

- 详细设计与迭代记录见 `PROJECT_DOCUMENTATION.md`
- 贡献指南见 `CONTRIBUTING.md`
- 许可证见 `LICENSE`
