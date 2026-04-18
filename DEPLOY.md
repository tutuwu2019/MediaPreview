# Windows 发布部署说明

本文用于解决运行时缺少以下 DLL 的问题:

- Qt6Core.dll
- Qt6Gui.dll
- Qt6Multimedia.dll
- Qt6MultimediaWidgets.dll

## 原因

Qt 程序在开发机可运行，不代表可直接拷贝 EXE 到其他机器运行。
需要把 Qt 运行库和插件一起打包到发布目录。

## 推荐方式: windeployqt 一键部署

项目已提供脚本 [deploy.ps1](deploy.ps1)。
脚本会在部署完成后自动做完整性校验，若关键 DLL 或插件缺失会直接报错并列出缺项。

### 1. 先构建 Release

MinGW 示例:

```powershell
$env:QT_ROOT = "D:/QT/6.9.3/mingw_64"
$env:MINGW_ROOT = "D:/QT/Tools/mingw1310_64"
cmake --preset mingw-release
cmake --build --preset build-mingw-release
```

MSVC 示例:

```powershell
$env:QT_ROOT = "D:/QT/6.9.3/msvc2022_64"
cmake --preset msvc-release
cmake --build --preset build-msvc-release
```

### 2. 执行部署脚本

MinGW:

```powershell
.\deploy.ps1 -QtRoot "D:/QT/6.9.3/mingw_64" -Toolchain mingw -Config Release
```

MSVC:

```powershell
.\deploy.ps1 -QtRoot "D:/QT/6.9.3/msvc2022_64" -Toolchain msvc -Config Release
```

默认输出目录:

- MinGW: `dist/mingw-release`
- MSVC: `dist/msvc-release`

重要:

1. `build/*` 目录仅用于编译，不是发布包。
2. 测试机必须运行 `dist/*/MediaPreviewClient.exe`。
3. 不要把 `build/mingw-release/MediaPreviewClient.exe` 直接给测试同事。

## 自检清单

在发布目录中确认存在:

1. `MediaPreviewClient.exe`
2. `Qt6Core.dll` `Qt6Gui.dll` `Qt6Multimedia.dll` `Qt6MultimediaWidgets.dll`
3. `platforms/qwindows.dll`
4. `multimedia/` 相关插件目录
5. `imageformats/` 插件目录（含 `qgif.dll`，否则 GIF 只能静态显示或无法解码）

脚本自动校验项（已内置）:

1. `MediaPreviewClient.exe`
2. `Qt6Core.dll` `Qt6Gui.dll` `Qt6Multimedia.dll` `Qt6MultimediaWidgets.dll`
3. `platforms/qwindows.dll`
4. `imageformats/*.dll`
5. `multimedia/*.dll`
6. `imageformats/qgif.dll`

## 常见坑

1. 混用了工具链
- 例如用 MinGW 编译，却用 MSVC 的 windeployqt。
- 必须确保 QtRoot 与编译工具链一致。

2. 只拷贝了 EXE
- 必须使用 windeployqt 复制依赖与插件。

3. 拷贝 Debug 包到无开发环境机器
- 推荐发布 Release 包。

4. 目标机缺少 VC++ 运行库（MSVC）
- 可安装对应版本 Microsoft Visual C++ Redistributable。

## 手动命令（不使用脚本）

```powershell
D:/QT/6.9.3/mingw_64/bin/windeployqt.exe --release --compiler-runtime --dir dist/mingw-release build/mingw-release/MediaPreviewClient.exe
```
