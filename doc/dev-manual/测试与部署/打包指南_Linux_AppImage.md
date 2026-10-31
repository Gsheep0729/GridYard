# GridYard 打包指南：Linux AppImage

**作者**：GY 团队
**最后更新**：2026-06-29
**适用版本**：v6.8.1（含在线聊天与 SQLite 本地数据层）

---

## 1. 概述

本文档记录如何将 GridYard 客户端打包为 Linux AppImage，实现单文件分发、免安装运行。

v6.6.2 引入在线聊天链路与 SQLite 本地数据层后，运行时依赖相比 v4.x 多出两类：Qt6 SQL 驱动（`libqsqlite.so`）和 Qt6 Widgets（系统托盘后端依赖）。打包流程在原 v4.x 基础上补齐这两项即可。

**最终产物**：`release/v6.8.1/GridYard-v6.8.1-x86_64.AppImage`（约 93 MB）

**已验证可工作**：

- UDP 设备发现（45678）、TCP 文件传输（35100）、首帧路由分流聊天连接
- SQLite Schema v1 迁移与本地聊天、传输历史读写
- 设备目录、消息模型、传输会话模型与 QML 绑定

**已知限制**：

- AppImage 在 offscreen 或无 StatusNotifierItem 的桌面环境下，系统托盘后台运行功能不可用（仅输出告警，不影响主流程）
- 包内不含输入法、字体、Wayland shell 等扩展插件，需要时按 §5.7 自行补齐

---

## 2. 前置条件

### 2.1 系统依赖

```bash
# Manjaro / Arch Linux
sudo pacman -S qt6-base qt6-declarative qt6-quickcontrols2 qt6-svg qt6-imageformats

# Ubuntu / Debian
sudo apt install qt6-base-dev qt6-declarative-dev qt6-quickcontrols2-dev \
                 libqt6svg6-dev qt6-imageformats-dev libqt6widgets6
```

注意 v6.6.2 起需要 `qt6-base` 同时包含 `libQt6Sql` 和 `libQt6Widgets`：`libQt6Sql.so.6` 由 Qt6 base 提供，`libQt6Widgets.so.6` 同源。`qt6-imageformats` 提供 JPEG/SVG 等图片格式支持，文件类型图标显示更稳定。

### 2.2 打包工具

- **linuxdeploy**：依赖收集与 AppDir 结构生成
  - 下载：`wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage`
  - 推荐位置：`/opt/linuxdeploy`

- **appimagetool**：AppImage 打包工具
  - 下载：`wget https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage`
  - 推荐位置：`/tmp/appimagetool`（临时使用，无需 sudo）

```bash
sudo install -m 0755 linuxdeploy-x86_64.AppImage /opt/linuxdeploy
install -m 0755 appimagetool-x86_64.AppImage /tmp/appimagetool
```

### 2.3 源码与构建

仓库根目录下确认 `src/build-ninja/client/appGridYard` 已通过 Release 配置构建：

```bash
cmake -S src -B src/build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build src/build-ninja -j
```

Stage 6 的 storage 单元测试通过即可认为 SQLite 链路可用：

```bash
ctest --test-dir src/build-ninja --output-on-failure -R storage
```

---

## 3. 打包流程

下文以仓库根为工作目录，AppDir 临时放在 `src/dist/AppDir`，最终 AppImage 输出到 `release/v6.8.1/`。

### 3.1 准备 AppDir 结构

```bash
PROJECT=$(pwd)
APPDIR=$PROJECT/src/dist/AppDir
BIN=$PROJECT/src/build-ninja/client/appGridYard
ICON=$PROJECT/src/client/icons/gridyard.png

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/lib/qt6" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/128x128/apps" \
         "$APPDIR/usr/share/icons/256x256/apps"

cp "$BIN"   "$APPDIR/usr/bin/"
cp "$ICON"  "$APPDIR/usr/share/icons/hicolor/128x128/apps/gridyard.png"
cp "$ICON"  "$APPDIR/usr/share/icons/256x256/apps/gridyard.png"
cp "$ICON"  "$APPDIR/gridyard.png"
```

项目自带的 `src/client/icons/gridyard.png` 为 128×128 RGBA，hicolor 主题同时写入 128 与 256 路径以兼容旧桌面，根目录副本用于 appimagetool 自动识别图标。

写入 desktop 文件：

```bash
cat > "$APPDIR/usr/share/applications/gridyard.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=GridYard
GenericName=LAN P2P File Transfer
Comment=GridYard 局域网 P2P 文件传输、在线聊天与本地历史
Comment[zh_CN]=GridYard 局域网 P2P 文件传输、在线聊天与本地历史
Exec=appGridYard
Icon=gridyard
Categories=Network;FileTransfer;P2P;
Terminal=false
StartupWMClass=GridYard
Keywords=gridyard;p2p;lan;file;chat;
EOF

cp "$APPDIR/usr/share/applications/gridyard.desktop" "$APPDIR/gridyard.desktop"
```

### 3.2 收集系统与 Qt 共享库

```bash
env -u DISPLAY ARCH=x86_64 /opt/linuxdeploy \
    --appdir "$APPDIR" --output appimage
```

linuxdeploy 会读取 `appGridYard` 的 ELF 依赖，复制 `libQt6Core / Gui / Network / Qml / Quick / QuickControls2 / Sql / OpenGL / DBus` 等共享库到 `usr/lib/`，并尝试对每个 `.so` 调用 strip。新版 glibc 的 `.relr.dyn` 段会让 linuxdeploy 自带的 strip 报 `unknown type [0x13]`，错误可忽略（见 §5.1）。

注意 linuxdeploy 不会自动收集 Qt 运行时按需加载的 `plugins/`、`qml/`，也不会主动把 `libQt6Widgets.so.6` 加入 AppDir（因为 `appGridYard` 自身不直接链接 Widgets）。下一步需要手动补齐。

### 3.3 手动补齐 Qt6 插件、QML 模块与 Widgets

```bash
# 复制 Qt6 插件（含 sqldrivers/libqsqlite.so、platforms/libqxcb.so 等）
mkdir -p "$APPDIR/usr/lib/qt6/plugins"
cp -r /usr/lib/qt6/plugins/* "$APPDIR/usr/lib/qt6/plugins/"

# 复制 Qt6 QML 模块（QtQuick、QtQuick.Controls、QtQuick.Layouts、Qt5Compat.GraphicalEffects 等）
mkdir -p "$APPDIR/usr/lib/qt6/qml"
cp -r /usr/lib/qt6/qml/* "$APPDIR/usr/lib/qt6/qml/"

# 复制 libQt6Widgets.so.6（v6.6.x 系统托盘后端依赖）
cp -L /usr/lib/libQt6Widgets.so.6 "$APPDIR/usr/lib/"
```

校验 Stage 6 必备的运行时组件到位：

```bash
test -f "$APPDIR/usr/lib/qt6/plugins/sqldrivers/libqsqlite.so" \
  && echo OK qsqlite \
  || echo FAIL qsqlite

test -f "$APPDIR/usr/lib/qt6/plugins/platforms/libqxcb.so" \
  && echo OK xcb \
  || echo FAIL xcb

test -d "$APPDIR/usr/lib/qt6/qml/QtQuick/Controls" \
  && echo OK QtQuick.Controls \
  || echo FAIL QtQuick.Controls

test -f "$APPDIR/usr/lib/libQt6Widgets.so.6" \
  && echo OK Qt6Widgets \
  || echo FAIL Qt6Widgets
```

四个 OK 全部出现，AppDir 体积约 250–260 MB（含 Qt6 全套 plugins 和 qml 模块）。

### 3.4 创建 AppRun

AppRun 是 AppImage 挂载点的启动脚本，负责把内部路径前置到运行时搜索路径，再 exec 主程序：

```bash
cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/bash
# AppImage 启动入口：设置运行时搜索路径，再执行主程序
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin:${PATH:+:${PATH}}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:+:${XDG_DATA_DIRS}}"
export QT_PLUGIN_PATH="${HERE}/usr/lib/qt6/plugins:${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}"
export QML2_IMPORT_PATH="${HERE}/usr/lib/qt6/qml:${QML2_IMPORT_PATH:+:${QML2_IMPORT_PATH}}"
exec "${HERE}/usr/bin/appGridYard" "$@"
EOF

chmod +x "$APPDIR/AppRun"
```

### 3.5 打包为 AppImage

```bash
VERSION=v6.8.1
mkdir -p "$PROJECT/release/$VERSION"

env -u DISPLAY ARCH=x86_64 /tmp/appimagetool \
    "$APPDIR" \
    "$PROJECT/release/$VERSION/GridYard-$VERSION-x86_64.AppImage"

ls -lh "$PROJECT/release/$VERSION"
```

成功输出示例：

```
-rwxr-xr-x 1 root root 93M ... GridYard-v6.8.1-x86_64.AppImage
```

---

## 4. 启动验证

### 4.1 离屏冒烟测试（无 X 环境）

CI 服务器或纯命令行环境可用 offscreen 平台验证二进制可执行、SQLite 迁移成功、UDP/TCP 端口绑定：

```bash
QT_QPA_PLATFORM=offscreen timeout 5 \
    release/v6.8.1/GridYard-v6.8.1-x86_64.AppImage
```

预期输出包含：

```
[DEBUG] ConfigManager: 使用已有的 deviceId: ...
[DEBUG] DiscoveryService: UDP socket 绑定成功，本地端口: 45678
[INFO]  [Storage] 数据库迁移完成，版本 1
[DEBUG] P2pServer: 监听端口 35100 成功
```

进程在 timeout 之前持续运行（事件循环工作）即视为通过。退出码 0 表示正常退出，124 表示 timeout 触发（同样视为正常启动）。

### 4.2 真实桌面测试

```bash
./release/v6.8.1/GridYard-v6.8.1-x86_64.AppImage
```

需双开测试时，用不同 `--config` 参数避免设备 ID 互相覆盖：

```bash
./GridYard-v6.8.1-x86_64.AppImage --config /tmp/gridyard_a.ini &
./GridYard-v6.8.1-x86_64.AppImage --config /tmp/gridyard_b.ini &
```

---

## 5. 常见问题

### 5.1 linuxdeploy strip 报错

**现象**：`ERROR: Strip call failed: unknown type [0x13] section '.relr.dyn'`

**原因**：linuxdeploy 内置 strip 版本过旧，无法识别新版 glibc 的 `.relr.dyn` 段。

**解决**：忽略该错误，linuxdeploy 退出码仍为 0，最终 AppImage 不受影响。如需消除告警，安装 `elfutils` 后通过环境变量切换到系统 strip：

```bash
export STRIP=$(command -v strip)
```

### 5.2 启动崩溃：缺少 Qt6 插件或 QML 模块

**现象**：AppImage 启动后立即 segfault；或日志出现 `Failed to load platform plugin "xcb"`、`module "QtQuick.Controls" is not installed`。

**原因**：未按 §3.3 复制 Qt6 plugins 与 QML 模块。

**解决**：确认 `AppDir/usr/lib/qt6/plugins/platforms/libqxcb.so` 和 `AppDir/usr/lib/qt6/qml/QtQuick/Controls/` 存在，再重新打包。

### 5.3 本地历史不可用

**现象**：应用可启动、能收发文件，但设置页提示「本地历史不可用」，或日志缺少 `[Storage] 数据库迁移完成`。

**原因**：包内缺少 Qt SQLite 驱动，运行时 `QSqlDatabase::drivers()` 不包含 `QSQLITE`。

**解决**：确认 `AppDir/usr/lib/qt6/plugins/sqldrivers/libqsqlite.so` 存在；同时检查 `AppRun` 中 `QT_PLUGIN_PATH` 是否指向 `usr/lib/qt6/plugins`。

### 5.4 系统托盘不可用

**现象**：日志输出 `No native SystemTrayIcon implementation available. Qt Labs Platform requires Qt Widgets on this setup.`

**原因**：

- v6.6.x 系统托盘使用 Qt Labs Platform 的 `SystemTrayIcon`，Linux 上需要 `libQt6Widgets.so.6` 与桌面环境提供的 StatusNotifierItem D-Bus 服务
- offscreen 测试环境没有窗口系统，必然报错
- 实机在 KDE Plasma、GNOME（带 AppIndicator 扩展）、XFCE 等支持 StatusNotifierItem 的桌面可用；纯 X11 老式托盘（如某些最小化 WM）不可用

**解决**：

- 确认 §3.3 已复制 `libQt6Widgets.so.6`
- 实机环境若仍不可用，安装 `statusnotifieritem` 相关扩展（GNOME：AppIndicator and KStatusNotifierItem Support；KDE 自带）
- 主流程（文件传输、在线聊天、SQLite 历史）不受托盘影响

### 5.5 appimagetool 找不到 desktop 文件

**现象**：`Desktop file not found, aborting`

**原因**：appimagetool 要求 desktop 文件在 AppDir 根目录。

**解决**：执行 §3.1 末尾的 `cp .../gridyard.desktop "$APPDIR/gridyard.desktop"`。

### 5.6 appimagetool 架构错误

**现象**：`More than one architectures were found`

**解决**：导出 `ARCH=x86_64` 后重试。

### 5.7 缺少输入法、Wayland、字体等扩展

AppImage 默认只携带 xcb 平台插件，Wayland 桌面需要额外补齐：

```bash
# Wayland 支持
mkdir -p "$APPDIR/usr/lib/qt6/plugins/waylandshellintegration"
mkdir -p "$APPDIR/usr/lib/qt6/plugins/waylandgraphicsintegrationclient"
mkdir -p "$APPDIR/usr/lib/qt6/plugins/waylanddecorationclient"
cp -r /usr/lib/qt6/plugins/wayland*/*.so "$APPDIR/usr/lib/qt6/plugins/" 2>/dev/null

# 中文字体（确保无字体环境也能显示）
mkdir -p "$APPDIR/usr/share/fonts"
cp /usr/share/fonts/noto/*.ttf "$APPDIR/usr/share/fonts/" 2>/dev/null
```

体积会显著增大，按需启用。

---

## 6. 自动化脚本

把 §3 流程封装为 `src/scripts/build_appimage.sh`：

```bash
#!/bin/bash
set -euo pipefail

PROJECT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$PROJECT/src/build-ninja"
APPDIR="$PROJECT/src/dist/AppDir"
APPIMAGETOOL="${APPIMAGETOOL:-/tmp/appimagetool}"
LINUXDEPLOY="${LINUXDEPLOY:-/opt/linuxdeploy}"

VERSION=$(sed -n 's/^project(GridYard VERSION \([0-9.]\+\).*/\1/p' "$PROJECT/src/CMakeLists.txt")
[ -n "$VERSION" ] || { echo "无法从 CMakeLists.txt 读取版本号"; exit 1; }

OUT_DIR="$PROJECT/release/v$VERSION"
APPIMAGE="$OUT_DIR/GridYard-v$VERSION-x86_64.AppImage"

echo ">>> 1. 增量构建"
cmake --build "$BUILD_DIR" -j

echo ">>> 2. 准备 AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/lib/qt6" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/128x128/apps" \
         "$APPDIR/usr/share/icons/256x256/apps"

cp "$BUILD_DIR/client/appGridYard" "$APPDIR/usr/bin/"
cp "$PROJECT/src/client/icons/gridyard.png" \
   "$APPDIR/usr/share/icons/hicolor/128x128/apps/gridyard.png"
cp "$PROJECT/src/client/icons/gridyard.png" \
   "$APPDIR/usr/share/icons/256x256/apps/gridyard.png"
cp "$PROJECT/src/client/icons/gridyard.png" "$APPDIR/gridyard.png"

cat > "$APPDIR/usr/share/applications/gridyard.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=GridYard
GenericName=LAN P2P File Transfer
Comment=GridYard 局域网 P2P 文件传输、在线聊天与本地历史
Comment[zh_CN]=GridYard 局域网 P2P 文件传输、在线聊天与本地历史
Exec=appGridYard
Icon=gridyard
Categories=Network;FileTransfer;P2P;
Terminal=false
StartupWMClass=GridYard
Keywords=gridyard;p2p;lan;file;chat;
EOF
cp "$APPDIR/usr/share/applications/gridyard.desktop" "$APPDIR/gridyard.desktop"

echo ">>> 3. linuxdeploy 收集依赖"
env -u DISPLAY ARCH=x86_64 "$LINUXDEPLOY" \
    --appdir "$APPDIR" --output appimage || true

echo ">>> 4. 补齐 Qt6 plugins / qml / Widgets"
mkdir -p "$APPDIR/usr/lib/qt6/plugins" "$APPDIR/usr/lib/qt6/qml"
cp -r /usr/lib/qt6/plugins/* "$APPDIR/usr/lib/qt6/plugins/"
cp -r /usr/lib/qt6/qml/*     "$APPDIR/usr/lib/qt6/qml/"
cp -L /usr/lib/libQt6Widgets.so.6 "$APPDIR/usr/lib/"

# 校验 Stage 6 关键依赖
for f in \
  "$APPDIR/usr/lib/qt6/plugins/sqldrivers/libqsqlite.so" \
  "$APPDIR/usr/lib/qt6/plugins/platforms/libqxcb.so" \
  "$APPDIR/usr/lib/qt6/qml/QtQuick/Controls" \
  "$APPDIR/usr/lib/libQt6Widgets.so.6"; do
  [ -e "$f" ] || { echo "缺失：$f"; exit 1; }
done

echo ">>> 5. AppRun"
cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin:${PATH:+:${PATH}}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:+:${XDG_DATA_DIRS}}"
export QT_PLUGIN_PATH="${HERE}/usr/lib/qt6/plugins:${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}"
export QML2_IMPORT_PATH="${HERE}/usr/lib/qt6/qml:${QML2_IMPORT_PATH:+:${QML2_IMPORT_PATH}}"
exec "${HERE}/usr/bin/appGridYard" "$@"
EOF
chmod +x "$APPDIR/AppRun"

echo ">>> 6. 打包 AppImage"
mkdir -p "$OUT_DIR"
env -u DISPLAY ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$APPIMAGE"

echo ">>> 完成：$APPIMAGE"
ls -lh "$APPIMAGE"
```

使用：

```bash
# 首次准备工具
install -m 0755 appimagetool-x86_64.AppImage /tmp/appimagetool
sudo install -m 0755 linuxdeploy-x86_64.AppImage /opt/linuxdeploy

# 一键打包
bash src/scripts/build_appimage.sh
```

脚本会从 `src/CMakeLists.txt` 自动读取版本号（当前为 `project(GridYard VERSION 6.8.1 ...)`），输出到 `release/v<VERSION>/GridYard-v<VERSION>-x86_64.AppImage`。

---

## 7. 发布物清单

每个版本目录建议包含：

```
release/v6.8.1/
├── GridYard-v6.8.1-x86_64.AppImage   # 主交付物
├── release-notes.md                   # 本版本变更要点
└── SHA256SUMS                         # 校验和（可选）
```

`release-notes.md` 模板：

```markdown
# GridYard v6.8.1

## 主要变更
- 在线聊天链路与首帧路由
- SQLite 本地聊天 / 传输历史与设备目录
- 历史恢复、保留期限清理、托盘后台运行

## 已知限制
- 无 StatusNotifierItem 的桌面环境托盘不可用

## 校验
- ctest 全部通过
- AppImage offscreen 冒烟测试通过
```

生成 SHA256：

```bash
cd release/v6.8.1
sha256sum GridYard-v6.8.1-x86_64.AppImage > SHA256SUMS
```

---

## 8. 参考资料

- [AppImage 官方文档](https://docs.appimage.org/)
- [linuxdeploy GitHub](https://github.com/linuxdeploy/linuxdeploy)
- [appimagetool GitHub](https://github.com/AppImage/AppImageKit)
- [Qt6 Linux 部署](https://doc.qt.io/qt-6/linux-deployment.html)
- [StatusNotifierItem 规范](https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/)
