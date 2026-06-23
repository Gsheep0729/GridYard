# GridYard 打包指南：Linux AppImage

**作者**：GY 团队
**最后更新**：2026-06-04

---

## 1. 概述

本文档记录如何将 GridYard 客户端打包为 Linux AppImage 格式，实现单文件分发、免安装运行。

**最终产物**：`GridYard-x86_64.AppImage`（约 92MB）

---

## 2. 前置条件

### 2.1 系统依赖

```bash
# Manjaro / Arch Linux
sudo pacman -S qt6-base qt6-declarative qt6-quickcontrols2 qt6-svg

# Ubuntu / Debian
sudo apt install qt6-base-dev qt6-declarative-dev qt6-quickcontrols2-dev libqt6svg6-dev
```

### 2.2 打包工具

- **linuxdeploy**：依赖收集与 AppDir 结构生成
  - 下载：`wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage`
  - 位置：`/opt/linuxdeploy`

- **appimagetool**：AppImage 打包工具
  - 下载：`wget https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage`
  - 位置：`/tmp/appimagetool`（临时使用）

---

## 3. 打包流程

### 3.1 构建项目

```bash
cd src
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja -j
```

### 3.2 准备 AppDir 结构

```bash
# 创建目录结构
mkdir -p dist/AppDir/usr/bin
mkdir -p dist/AppDir/usr/lib
mkdir -p dist/AppDir/usr/share/applications
mkdir -p dist/AppDir/usr/share/icons/hicolor/256x256/apps

# 复制可执行文件
cp build-ninja/client/appGridYard dist/AppDir/usr/bin/

# 复制图标（需 256x256 PNG）
cp doc/CQNU.png dist/AppDir/usr/share/icons/hicolor/256x256/apps/gridyard.png

# 创建 desktop 文件
cat > dist/AppDir/usr/share/applications/gridyard.desktop << 'EOF'
[Desktop Entry]
Type=Application
Name=GridYard
Comment=局域网 P2P 文件传输工具
Exec=appGridYard
Icon=gridyard
Categories=Network;FileTransfer;
Terminal=false
EOF
```

### 3.3 收集依赖库

```bash
# 使用 linuxdeploy 收集共享库
/opt/linuxdeploy --appdir dist/AppDir --output appimage
```

**注意**：linuxdeploy 的 strip 命令在新版 glibc 上可能失败（`.relr.dyn` 段无法识别），但不影响最终打包。

### 3.4 手动补充 Qt6 插件和 QML 模块

linuxdeploy 不会自动收集 Qt6 插件和 QML 模块，需要手动复制：

```bash
# 复制 Qt6 插件
mkdir -p dist/AppDir/usr/lib/qt6/plugins
cp -r /usr/lib/qt6/plugins/* dist/AppDir/usr/lib/qt6/plugins/

# 确认 SQLite 驱动被带入包内
test -f dist/AppDir/usr/lib/qt6/plugins/sqldrivers/libqsqlite.so

# 复制 QML 模块
mkdir -p dist/AppDir/usr/lib/qt6/qml
cp -r /usr/lib/qt6/qml/* dist/AppDir/usr/lib/qt6/qml/
```

### 3.5 创建 AppRun 脚本

```bash
cat > dist/AppDir/AppRun << 'EOF'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin/:${PATH:+:$PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib/:${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="${HERE}/usr/share/${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export QT_PLUGIN_PATH="${HERE}/usr/lib/qt6/plugins/${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QML2_IMPORT_PATH="${HERE}/usr/lib/qt6/qml/${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
exec "${HERE}/usr/bin/appGridYard" "$@"
EOF

chmod +x dist/AppDir/AppRun
```

### 3.6 打包为 AppImage

```bash
# 复制 desktop 文件和图标到 AppDir 根目录
cp dist/AppDir/usr/share/applications/gridyard.desktop dist/AppDir/
cp dist/AppDir/usr/share/icons/hicolor/256x256/apps/gridyard.png dist/AppDir/

# 使用 appimagetool 打包
ARCH=x86_64 /tmp/appimagetool dist/AppDir GridYard-x86_64.AppImage
```

---

## 4. 目录结构说明

```
dist/AppDir/
├── AppRun                      # 启动脚本，设置环境变量
├── gridyard.desktop            # desktop 文件（根目录副本）
├── gridyard.png                # 图标（根目录副本）
└── usr/
    ├── bin/
    │   └── appGridYard         # 可执行文件
    ├── lib/
    │   ├── libQt6*.so.6        # Qt 共享库（linuxdeploy 收集）
    │   ├── lib*.so.*           # 系统共享库（linuxdeploy 收集）
    │   └── qt6/
    │       ├── plugins/        # Qt6 插件（手动复制）
    │       └── qml/            # QML 模块（手动复制）
    └── share/
        ├── applications/
        │   └── gridyard.desktop
        └── icons/hicolor/256x256/apps/
            └── gridyard.png
```

---

## 5. 常见问题

### 5.1 linuxdeploy strip 报错

**现象**：`ERROR: Strip call failed: unknown type [0x13] section '.relr.dyn'`

**原因**：linuxdeploy 内置的 strip 工具版本过旧，无法识别新版 glibc 的 `.relr.dyn` 段。

**解决**：忽略该错误，不影响最终打包。或升级 linuxdeploy 到最新版本。

### 5.2 AppImage 启动崩溃

**现象**：`timeout: 被监视的命令已核心转储`

**原因**：缺少 Qt6 插件或 QML 模块。

**解决**：手动复制 `/usr/lib/qt6/plugins` 和 `/usr/lib/qt6/qml` 到 AppDir。

### 5.3 本地历史不可用

**现象**：应用可以启动和收发文件，但设置页提示本地历史不可用。

**原因**：包内缺少 Qt SQLite 驱动，运行时 `QSqlDatabase::drivers()` 不包含 `QSQLITE`。

**解决**：确认 `dist/AppDir/usr/lib/qt6/plugins/sqldrivers/libqsqlite.so` 存在，并检查 `AppRun` 中的 `QT_PLUGIN_PATH` 是否指向 `usr/lib/qt6/plugins`。

### 5.4 appimagetool 找不到 desktop 文件

**现象**：`Desktop file not found, aborting`

**原因**：appimagetool 要求 desktop 文件在 AppDir 根目录。

**解决**：将 desktop 文件复制到 `dist/AppDir/` 根目录。

### 5.5 appimagetool 架构错误

**现象**：`More than one architectures were found`

**原因**：appimagetool 无法自动检测架构。

**解决**：设置环境变量 `ARCH=x86_64`。

### 5.6 图标未找到

**现象**：`gridyard{.png,.svg,.xpm} defined in desktop file but not found`

**原因**：appimagetool 在 AppDir 根目录查找图标。

**解决**：将图标文件复制到 `dist/AppDir/` 根目录。

---

## 6. 自动化脚本

可将以下脚本保存为 `scripts/build_appimage.sh`：

```bash
#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/src/build"
APPDIR="$PROJECT_ROOT/dist/AppDir"
APPIMAGE="$PROJECT_ROOT/GridYard-x86_64.AppImage"

# 1. 构建
echo ">>> 构建项目..."
cd "$PROJECT_ROOT/src"
cmake --build build-ninja -j

# 2. 准备 AppDir
echo ">>> 准备 AppDir..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib/qt6"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BUILD_DIR/client/appGridYard" "$APPDIR/usr/bin/"
cp "$PROJECT_ROOT/doc/CQNU.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/gridyard.png"
# 创建 desktop 文件...

# 3. 收集依赖
echo ">>> 收集依赖库..."
/opt/linuxdeploy --appdir "$APPDIR" --output appimage 2>&1 || true

# 4. 复制 Qt6 组件
echo ">>> 复制 Qt6 插件和 QML 模块..."
cp -r /usr/lib/qt6/plugins/* "$APPDIR/usr/lib/qt6/plugins/"
test -f "$APPDIR/usr/lib/qt6/plugins/sqldrivers/libqsqlite.so"
cp -r /usr/lib/qt6/qml/* "$APPDIR/usr/lib/qt6/qml/"

# 5. 创建 AppRun
echo ">>> 创建 AppRun..."
cat > "$APPDIR/AppRun" << 'APPRUN'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin/:${PATH:+:$PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib/:${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/usr/lib/qt6/plugins/${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QML2_IMPORT_PATH="${HERE}/usr/lib/qt6/qml/${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
exec "${HERE}/usr/bin/appGridYard" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# 6. 打包
echo ">>> 打包 AppImage..."
cp "$APPDIR/usr/share/applications/gridyard.desktop" "$APPDIR/"
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/gridyard.png" "$APPDIR/"
ARCH=x86_64 /tmp/appimagetool "$APPDIR" "$APPIMAGE"

echo ">>> 完成：$APPIMAGE"
ls -lh "$APPIMAGE"
```

---

## 7. 参考资料

- [AppImage 官方文档](https://docs.appimage.org/)
- [linuxdeploy GitHub](https://github.com/linuxdeploy/linuxdeploy)
- [appimagetool GitHub](https://github.com/AppImage/AppImageKit)
- [Qt6 部署文档](https://doc.qt.io/qt-6/linux-deployment.html)
