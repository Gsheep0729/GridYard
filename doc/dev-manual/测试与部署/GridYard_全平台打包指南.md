# GridYard (鸽邮) 全平台打包与分发指南

**版本**：v1.1（对应工程版本 v8.0.0）
**维护组织**：GridYard Team
**适用操作系统**：Linux (Ubuntu / Debian / Arch / Manjaro) | Windows 10/11 (规划) | Android 10+ (规划)

---

## 1. 概述与分发战略

GridYard（鸽邮）作为一款基于 **C++23**、**Qt6 (QML 表现层)** 与 **自研 POSIX 网络层** 构建的局域网 P2P 文件传输、在线聊天与数据同步桌面/移动应用，需要面向不同的终端环境提供专业化、规范化的交付形态。

v8.0.0 起产品包含两个可执行程序：桌面客户端 `appGridYard`（Qt 仅存在于界面层）与协调/中继服务 `gridyard_rendezvous`（纯 C++ 实现，`ldd` 仅依赖系统 C++ 运行库，零 Qt），两个二进制随下述各分发形态一同交付。

为满足不同平台用户的使用习惯与交付环境，GridYard 确立了如下分发矩阵：

| 平台 / 体系 | 分发格式 | 特点与适用场景 | 状态 | 交付文件路径 |
| :--- | :--- | :--- | :--- | :--- |
| **Linux (通用便携)** | **.tar.gz** | 绿色解压版，解压即用，目录自包含，内置环境引导脚本 `GridYard.sh`，适合企业内网或无 root 权限环境。 | **现行支持** | `release/vX.Y.Z/GridYard-vX.Y.Z-linux-x86_64.tar.gz` |
| **Debian / Ubuntu** | **.deb** | **极致瘦身自包含安装包**（基于白名单精准剪裁），部署于 `/opt/gridyard/`，内嵌精简 Qt6 核心运行时与 QML 模块，注册系统级桌面菜单与 `/usr/bin` 软链接，**安装包约 31 MB**（解压约 134 MB），在 Ubuntu 20.04/22.04/24.04 开箱即用。 | **现行支持** | `release/vX.Y.Z/GridYard-vX.Y.Z-amd64.deb` |
| **Arch / Manjaro** | **PKGBUILD** | Arch 滚动发行版 pacman 规范包，支持源码自动编译、依赖关联与滚动升级。 | **现行支持** | `src/scripts/PKGBUILD` |
| **Linux (通用)** | AppImage | 单文件零依赖运行。 | 历史能力（v6.8.1 时代随附脚本，现行版本未提供，见 2.2） | — |
| **Windows** | **.exe / .zip** | 基于 `windeployqt` 依赖收集，采用 Inno Setup 制作向导式安装包，或提供绿色 Zip 包。 | **规划中** | `release/vX.Y.Z/GridYard-Setup-vX.Y.Z-x64.exe` |
| **Android** | **.apk / .aab** | 基于 Qt for Android 套件与 `androiddeployqt`，支持移动端局域网快速发现与互传。 | **规划中** | `release/vX.Y.Z/GridYard-vX.Y.Z-arm64-v8a.apk` |

---

## 2. Linux 分发体系（现行实现）

> **交付路线裁决（2026-10）**：交付不走 CPack，维持 `src/scripts/build_tarball.sh`、`src/scripts/build_deb.sh`、`src/scripts/PKGBUILD` 三脚本直拷路线——CPack 对"双二进制 + 白名单瘦身 + 系统注册"组合形态的控制力不及脚本直拷，工程内勿引入 CPack 配置。

### 2.1 环境依赖与前置准备

在进行打包前，需确保系统已安装基础编译环境与 Qt6 模块。工程要求 **CMake ≥ 4.2.3** 与 **GCC ≥ 15**（C++23 `import std` 依赖 libstdc++ 的 std 模块支持），发行版自带版本不足时需自行安装新版工具链：

#### Ubuntu / Debian (22.04 / 24.04 LTS)
```bash
sudo apt update
sudo apt install build-essential ninja-build dpkg-dev \
                 qt6-base-dev qt6-declarative-dev qt6-quickcontrols2-dev \
                 libqt6svg6-dev qt6-imageformats-dev
```

#### Arch Linux / Manjaro
```bash
sudo pacman -S base-devel cmake ninja gcc \
               qt6-base qt6-declarative qt6-quickcontrols2 \
               qt6-svg qt6-imageformats
```

打包脚本的产物均输出到 `release/v<版本号>/` 目录，并附带 `.sha256` 校验文件。

### 2.2 AppImage（历史能力说明）

v6.8.1 时代 GridYard 曾随附 AppImage 打包脚本，提供单文件免安装运行形态；现行版本未提供该脚本（`src/scripts/` 下仅有 tar.gz、deb 与 PKGBUILD 三种形态），历史手册《打包指南_Linux_AppImage.md》仅适用于旧版本源码，不再随新版本维护。如需恢复 AppImage 交付，需重新编写对应脚本并对现行构建产物验证。

### 2.3 Linux 绿色解压版（.tar.gz）

#### 适用场景
- 用户没有 root 安装权限；
- 需要将 GridYard 部署到移动存储介质（U盘、移动硬盘）随插随用；
- 配合 CI/CD 流水线做轻量免挂载的分发测试。

#### 目录架构
绿色版完全遵循 AppDir 自包含标准，其内部组织如下（压缩包约 389 MB，解压约 1.1 GB，含完整 Qt 运行时）：
```text
GridYard-v8.0.0-linux-x86_64/
├── GridYard.sh                     # 核心引导脚本（自动导出动态库与QML搜索路径）
├── gridyard.desktop                # FreeDesktop 快捷方式
├── gridyard.png                    # 应用图标
└── usr/
    ├── bin/
    │   ├── appGridYard             # 客户端主二进制程序
    │   └── gridyard_rendezvous     # 协调/中继服务（零 Qt）
    ├── lib/
    │   ├── libQt6Widgets.so.6      # 系统托盘所需组件
    │   ├── libQt6Core.so.6 ...     # 核心 Qt 共享库与系统运行时
    │   └── qt6/
    │       ├── plugins/            # 核心平台插件 (platforms/libqxcb.so 等)
    │       └── qml/                # QtQuick 与 Controls 声明式组件树
    └── share/
        ├── applications/
        └── icons/
```

#### 构建命令
执行一键打包脚本：
```bash
bash src/scripts/build_tarball.sh
```
脚本将自动读取 `src/CMakeLists.txt` 版本号，提取构建产物，搜集 Qt 依赖与插件，并在 `release/vX.Y.Z/` 目录下生成 `GridYard-vX.Y.Z-linux-x86_64.tar.gz` 及对应的 `.sha256` 校验文件。若构建目录中缺少 `gridyard_rendezvous`，脚本会直接报错退出，请先完成完整构建。

#### 用户使用方法
```bash
tar -xzvf GridYard-v8.0.0-linux-x86_64.tar.gz
cd GridYard-v8.0.0-linux-x86_64
./GridYard.sh
```

---

### 2.4 Debian/Ubuntu 极致瘦身自包含型原生安装包（.deb）

#### 架构抉择与白名单瘦身工程亮点
- 深度契合 Ubuntu、Debian、Linux Mint 等系统的官方软件包管理体系（支持 `dpkg -i`、`apt remove`、自动生成系统启动菜单）；
- **为什么不采用"全量粗暴复制"，而是执行"白名单精准剪裁"？**
  - **传统全量打包的虚胖危机**：早期自包含脚本若采用全量拷贝 Qt SDK，会将 125MB 的 QML 语言服务器 `qmlls`、66MB 的性能分析器 `qmltooling`、近 200MB 的 3D 模型渲染器和解析器（`renderers`/`sceneparsers`）、40MB 的多媒体引擎 `multimedia` 等与运行无关的组件全部塞入，导致安装包体积失控。
  - **白名单精准剪裁工程**：GridYard 在 `src/scripts/build_deb.sh` 中确立了严格的白名单机制：
    1. **QML 模块精准收集**：仅白名单抽取实际 `import` 的 `QtQuick`（含 Controls, Dialogs, Effects, Layouts, Templates, Window）、`Qt.labs.platform` 和 `QtQml`；递归剔除所有 `designer` 插件目录、`.debug` 文件和 `.qmltypes` 元数据。
    2. **Plugins 插件精准收集**：保留 `platforms`、`platformthemes`、`xcbglintegrations`（X11 OpenGL 上下文支持）、`wayland-shell-integration`（XDG-Shell Wayland 原生支持）、`imageformats`（保留 svg, png, jpeg, ico 等）与 `tls`。自 v8.0.0 起存储层使用编译进二进制的 vendored SQLite，客户端闭包已无 Qt6Sql，`sqldrivers` 插件与 `libQt6Sql` 不再随包交付；`libQt6Network` 属 Qt Quick 框架自身的传递依赖，仍然保留。
    3. **核心动态库精准收集**：放弃全量拷贝，严格从 Qt 根目录抽取 20 余个关键核心库（Core, Gui, Widgets, Qml, Quick, QuickControls2, QuickTemplates2, Svg, OpenGL, DBus, icu 等），并保持符号链接。
    4. **全局 strip 极限优化**：对 `/opt/gridyard/` 下的所有 ELF 可执行文件与共享库统一执行 `strip --strip-unneeded`，剥离调试符号。
  - **实测结果**：安装包约 **31 MB**（压缩包字节数 32,279,436），解压部署约 **134 MB**（Installed-Size 137,044 KB），收敛于 `/opt/gridyard/`，兼顾了"极致轻巧"与"Wayland/X11 全环境开箱即用"。

#### 目录架构与系统集成
```text
GridYard-v8.0.0-amd64.deb 包结构:
├── DEBIAN/
│   ├── control                     # 系统底层运行时依赖声明 (libc6, libgl1, libopengl0, libxkbcommon 等)
│   ├── postinst                    # 安装后自动更新 desktop 与 icon 缓存
│   └── postrm                      # 卸载后清理缓存
├── opt/
│   └── gridyard/                   # 独立部署根目录
│       ├── bin/appGridYard         # 客户端二进制 (经 strip 优化)
│       ├── bin/gridyard_rendezvous # 协调/中继服务 (零 Qt)
│       ├── lib/                    # 精准剪裁的 Qt6 核心动态库与 ICU
│       ├── lib/qt6/plugins/        # 精选平台插件 (wayland-shell, xcbgl, platforms, svg 等)
│       ├── lib/qt6/qml/            # 精选 QML 核心模块树 (剔除 designer 与调试文件)
│       └── GridYard.sh             # 环境变量引导与程序启动入口
├── usr/
│   ├── bin/
│   │   ├── gridyard -> /opt/gridyard/GridYard.sh       # 系统命令行软链接
│   │   └── appGridYard -> /opt/gridyard/GridYard.sh    # 兼容历史命令习惯
│   └── share/
│       ├── applications/gridyard.desktop              # 系统应用列表快捷方式
│       ├── icons/hicolor/.../gridyard.png             # 高清桌面图标
│       └── doc/gridyard/                              # 版权与更新日志
```

#### 依赖声明设计（Depends）
```text
Depends: libc6 (>= 2.34), libstdc++6 (>= 13), libgl1, libopengl0, libglx0, libegl1, libxkbcommon0, libfontconfig1, libglib2.0-0t64 | libglib2.0-0, libx11-6, libxcb1, libdbus-1-3, libgssapi-krb5-2, libbz2-1.0
```
该依赖声明既规避了 Ubuntu 官方软件源 Qt 版本过低导致的崩溃，又精准约束了底层 OpenGL、X11、XKB、Fontconfig、GLib 和 D-Bus 系统运行时，确保即使在最最小化的纯净 Docker 容器中执行 `apt install ./gridyard.deb` 也能自动补齐底层依赖并一次性运行成功。

#### 构建命令
```bash
bash src/scripts/build_deb.sh
```
最终生成的安装包约 **31 MB**（解压部署后约 134 MB）。脚本对构建目录中缺失 `gridyard_rendezvous` 的情况同样直接报错退出。

#### 安装、运行与卸载
```bash
# 1. 安装构建出的 deb 包
sudo dpkg -i release/v8.0.0/GridYard-v8.0.0-amd64.deb

# 2. 命令行随时随地启动（通过 /usr/bin/ 软链接直接拉起）
gridyard
# 或者:
appGridYard

# 3. 卸载（自动清理 /opt 与 /usr 软链接并刷新桌面数据库）
sudo apt remove gridyard
```

---

### 2.5 Arch Linux / Manjaro 原生包（PKGBUILD）

#### 适用场景
- 适合 Arch Linux、Manjaro、EndeavourOS 等滚动发行版用户；
- 完美接入 `pacman` 包管理，支持投递至 AUR (Arch User Repository)。

#### 规范与流程
脚本文件位于 `src/scripts/PKGBUILD`。标准流程包含：
1. `prepare()`：自动定位本地或上游源码根目录；
2. `build()`：调用 `cmake` 指定 `-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr`；
3. `check()`：执行 CTest 自动化测试（`test_integration` 为本机回环集成测试，打包环境排除；测试失败将使打包失败）；
4. `package()`：将客户端与服务端两个二进制、图标、桌面文件及 LICENSE 正式安装到 `$pkgdir` 标准系统沙箱中。

`makedepends` 声明为 `cmake>=4.2.3` 与 `gcc>=15`，与工程的 C++23 `import std` 工具链要求一致；运行依赖 `qt6-base`、`qt6-declarative`、`qt6-svg`、`qt6-imageformats`（客户端 QML 界面与 SVG 图标渲染），服务端二进制仅依赖系统 C++ 运行库，无需额外包。

#### 构建与安装方法（在 Arch / Manjaro 机器上）
```bash
cd src/scripts
# 执行标准 makepkg，-s 自动解决依赖，-i 编译后直接安装
makepkg -si
```

### 2.6 gridyard_rendezvous 服务端交付说明

`gridyard_rendezvous` 是 GridYard 的协调节点与流式中继服务，独立于客户端进程运行，用于跨 AP 的校园网可达性增强场景：设备注册与候选端点拉取（`--mode rendezvous`，默认）与 TCP 字节流转发（`--mode relay`）。该二进制为纯 C++ 实现，`ldd` 仅依赖 libstdc++/libm/libgcc/libc 四个系统库，不链接任何 Qt 库，因此 deb 与 tar.gz 形态随包交付、PKGBUILD 形态随源码编译交付，均不引入额外运行时依赖。

典型部署：
```bash
# 协调节点模式（默认端口 45679，可选 --token 开启访问令牌校验）
gridyard_rendezvous --port 45679 --token <部署口令>

# 中继模式
gridyard_rendezvous --mode relay --port 45679
```

客户端在设置页填写协调服务器地址与端口（及令牌）即可接入；服务端的资源上限、超时与日志行为见《GridYard_协调节点与中继服务设计》。

### 2.7 Linux 3 种分发方案全景对比

| 维度 | .tar.gz 绿色版 | .deb 极致瘦身自包含包 | PKGBUILD (Arch) |
| :--- | :--- | :--- | :--- |
| **安装包体积** | 约 389 MB（含全套运行时） | **约 31 MB (白名单精准剪裁)** | 视源码包而定，二进制小 |
| **安装解压后占用** | 约 1.1 GB（含全套 Qt 运行时） | **约 134 MB (收敛于 /opt)** | 约 4 MB ~ 8 MB (共享系统库) |
| **是否需要 Root 权限** | **否** | 是 (`sudo dpkg`) | 是 (`sudo pacman`) |
| **安装部署路径** | 用户自定义解压路径 | **`/opt/gridyard/`** | 注册至系统 `/usr` 目录 |
| **对系统 Qt 环境依赖** | **完全无依赖（内嵌 Qt）** | **完全无依赖（内嵌 Qt）** | 强依赖（Arch pacman 提供最新库） |
| **快捷方式与命令集成** | 需手动放置或脚本配置 | **自动注册 `/usr/bin/gridyard` 与桌面图标** | **自动注册 `/usr/bin/appGridYard` 与桌面图标** |
| **推荐用户群** | 便携式/内网离线/无Root环境 | **Ubuntu/Debian 正式装机用户** | Arch/Manjaro 极客用户 |

---

## 3. Windows 平台前瞻（规划与实施蓝图）

### 3.1 编译架构选型
Windows 客户端的构建有两种技术路线：
1. **方案 A（首选推荐）：Windows 环境原生编译**
   - 采用 Windows 10/11 + Visual Studio 2022 (MSVC v143+) / Clang-cl + 官方 Qt 6.8+ (MSVC 64-bit)。
   - 优点：原生调试符号完整，DirectX/D3D11/D3D12 图形后端性能最佳，与 Windows 原生 API 契合度高。
2. **方案 B：Linux 下 MinGW-w64 交叉编译**
   - 在 Ubuntu 下配置 `x86_64-w64-mingw32-g++` 配合 MinGW 预编译的 Qt6 库。
   - 优点：可在同一台 Linux 构建机/CI 完成多平台出包。

> [!NOTE]
> GridYard 采用 C++23 标准并依赖 C++23 std 模块（`import std`），Windows 侧需选用支持该特性的工具链版本。网络通信层基于 Linux 原生 POSIX 封装，针对 Windows 平台需通过 Winsock2 (`ws2_32.lib`) 和 `WSAStartup` 实现轻量适配层。

---

### 3.2 依赖收集引擎：`windeployqt`

Windows 下 Qt 动态库与 QML 模块的收集标准化工具为 `windeployqt`。

#### 标准执行参数
```cmd
:: 假设构建输出在 build/bin/Release/appGridYard.exe
windeployqt.exe build/bin/Release/appGridYard.exe ^
    --qmldir src/client ^
    --plugindir build/bin/Release/plugins ^
    --no-translations ^
    --compiler-runtime ^
    --verbose 1
```

#### 关键要点
- `--qmldir` 参数：**极其关键**。必须指定包含 QML 源码的目录（`src/client`），`windeployqt` 会扫描所有 `.qml` 文件中的 `import` 语句（如 `QtQuick`, `QtQuick.Controls`, `QtQuick.Layouts`, `QtQuick.Dialogs`），并将对应的 QML 插件目录完整复制到输出目录中。
- 托盘图标组件：复制 `plugins/platforms/qwindows.dll`，以及 `libQt6Widgets.dll` 与 `plugins/styles/`。

---

### 3.3 向导式安装包制作：Inno Setup (.iss)

Inno Setup 是 Windows 平台首选的轻量、高效且美观的开源安装包压制工具。

#### Inno Setup 脚本设计大纲（未来实施模板）
在 `src/scripts/windows/setup.iss` 中规划如下配置：
```pascal
[Setup]
AppName=GridYard
AppVersion=8.0.0
AppPublisher=GridYard Team
AppPublisherURL=https://github.com/cqnu-gridyard/GridYard
DefaultDirName={autopf}\GridYard
DefaultGroupName=GridYard
OutputDir=..\..\release\v8.0.0
OutputBaseFilename=GridYard-Setup-v8.0.0-x64
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\appGridYard.exe

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"

[Files]
Source: "..\..\dist\windows_x64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\GridYard"; Filename: "{app}\appGridYard.exe"
Name: "{group}\卸载 GridYard"; Filename: "{uninstallexe}"
Name: "{autodesktop}\GridYard"; Filename: "{app}\appGridYard.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\appGridYard.exe"; Description: "运行 GridYard"; Flags: postinstall nowait skipifsilent
```

---

### 3.4 Windows 专属注意事项与系统适配
1. **Windows 防火墙弹窗拦截**：
   - GridYard 在首次启动 UDP 广播 (45678) 和 TCP 监听 (35100) 时，Windows 会弹出"Windows Defender 防火墙已阻止此应用的部分功能"。
   - 解决方案：在 Inno Setup 安装过程的 `[Run]` 阶段，可调用 `netsh advfirewall firewall add rule` 为 `appGridYard.exe` 自动添加局域网入站规则允许。
2. **高清 DPI 适配**：
   - Qt6 默认开启感知系统 DPI 缩放（Per-Monitor V2），需在 `appGridYard.exe` 的清单（Manifest）中声明 `dpiAware`。
3. **数字签名 (Code Signing)**：
   - 避免 Windows SmartScreen 报毒拦截，生产分发建议为可执行文件及安装程序签署 EV/OV 代码签名证书。

---

## 4. Android 移动平台打包蓝图（规划与实施蓝图）

### 4.1 环境准备与架构体系

基于 Qt 6 for Android，移动端的打包构建需要在构建机上准备以下 SDK 链条：

1. **Java Development Kit (JDK)**：推荐 JDK 17（Qt 6 强力推荐基线，兼容最新 Gradle 8.x）。
2. **Android SDK**：
   - `platform-tools`
   - `build-tools` (如 34.0.0+)
   - `platforms;android-34` (Android 14 API)
3. **Android NDK**：
   - NDK r25b 或 r26c（根据 Qt 版本的 NDK 官方要求匹配）。
4. **Qt 6 for Android 套件**：
   - 包含针对 `arm64-v8a`（主流真机）、`armeabi-v7a`（旧机型）和 `x86_64`（模拟器）的预编译库与工具链。

---

### 4.2 清单配置与 Android 权限 (`AndroidManifest.xml`)

局域网文件传输与发现在 Android 平台受到严格的系统安全沙箱管控，必须声明如下权限：

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="cqnu.gridyard.client"
    android:versionCode="80000"
    android:versionName="8.0.0">

    <!-- 基础网络权限 -->
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
    <uses-permission android:name="android.permission.ACCESS_WIFI_STATE" />

    <!-- 局域网组播与发现核心权限（Android 必备） -->
    <uses-permission android:name="android.permission.CHANGE_WIFI_MULTICAST_STATE" />

    <!-- 存储读写（Android 10+ 建议使用 SAF 或应用私有存储） -->
    <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" android:maxSdkVersion="32" />
    <uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" android:maxSdkVersion="29" />

    <!-- 后台传输不被息屏中断 -->
    <uses-permission android:name="android.permission.WAKE_LOCK" />
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE_DATA_SYNC" />

    <application
        android:label="GridYard"
        android:icon="@mipmap/ic_launcher"
        android:requestLegacyExternalStorage="true">

        <activity
            android:name="org.qtproject.qt.android.bindings.QtActivity"
            android:configChanges="orientation|uiMode|screenLayout|screenSize|smallestScreenSize|layoutDirection|locale|fontScale|keyboard|keyboardHidden|navigation"
            android:exported="true"
            android:launchMode="singleTop">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

---

### 4.3 构建与打包流程：CMake + `androiddeployqt`

Qt6 原生支持通过 CMake 自动集成 Gradle 打包：

#### 1. CMake 交叉编译配置
```bash
cmake -S src -B build-android \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="/opt/QT/6.8.0/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DANDROID_SDK_ROOT="/opt/android-sdk" \
    -DANDROID_NDK_ROOT="/opt/android-ndk-r26c" \
    -DANDROID_ABI="arm64-v8a" \
    -DANDROID_PLATFORM=android-34 \
    -DQT_ANDROID_PACKAGE_SOURCE_DIR="${PROJECT_ROOT}/src/android"
```

#### 2. 生成 APK
```bash
# 编译二进制
cmake --build build-android --target appGridYard

# 触发 Android APK 打包目标
cmake --build build-android --target appGridYard_make_apk
```
最终产物将输出在 `build-android/android-build/build/outputs/apk/release/appGridYard-release-unsigned.apk`。

---

### 4.4 移动端核心挑战与解决方案
1. **Wi-Fi 组播锁定 (MulticastLock)**：
   - **痛点**：Android 系统在默认情况下为了省电会过滤掉 UDP 组播与广播包，导致局域网设备无法相互发现。
   - **解决方案**：在客户端启动初期，通过 JNI 调用 Android Java 原生 API `WifiManager.createMulticastLock("GridYardLock")` 并调用 `lock.acquire()`，在退出时调用 `release()`。
2. **后台休眠保活**：
   - **痛点**：长时间大文件传输过程中，如果用户锁屏，系统 Doze 模式会切断网络套接字。
   - **解决方案**：文件传输期间挂载 Foreground Service（前台通知），持有 `WakeLock` 保持 CPU 和网络唤醒状态。

---

## 5. 发布物组织规范与 CI/CD 展望

### 5.1 发布目录结构规范
每个正式版本发布后，`release/vX.Y.Z/` 建议包含以下标准产物：
```text
release/v8.0.0/
├── GridYard-v8.0.0-linux-x86_64.tar.gz     # Linux 通用绿色解压版
├── GridYard-v8.0.0-amd64.deb               # Debian/Ubuntu 极速原生安装包
├── GridYard-Setup-v8.0.0-x64.exe           # (规划) Windows 原生向导安装包
├── GridYard-v8.0.0-arm64-v8a.apk           # (规划) Android 移动版客户端
├── SHA256SUMS                              # 统一哈希校验清单
└── release-notes.md                        # 版本发布更新日志
```

### 5.2 生成全局校验码
```bash
cd release/v8.0.0
sha256sum *.{tar.gz,deb,exe,apk} > SHA256SUMS 2>/dev/null || true
```

### 5.3 CI/CD 矩阵式自动化展望
后续建议在 GitHub Actions / 自建 GitLab CI 中引入矩阵流水线（Build Matrix）：
- `ubuntu-24.04` Runner：执行自动化构建，同时调用 `build_tarball.sh` 和 `build_deb.sh`，生成 Linux 绿色版与 deb 包。
- `windows-latest` Runner：调用 MSVC + `windeployqt` + Inno Setup 生成 Windows 安装包。
- `android-docker` 镜像：内置 JDK 17 与 Android SDK/NDK，执行 `androiddeployqt` 自动生成 APK。
- 最终触发 Release 自动创建并挂载发布物附件。
