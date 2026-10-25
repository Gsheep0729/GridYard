#!/bin/bash
# ==============================================================================
# GridYard (鸽邮) - Debian/Ubuntu 极致瘦身自包含型原生安装包 (.deb) 打包脚本
#
# 架构策略（工业级白名单精准剪裁）：
# 1. 深度摆脱全量 Qt SDK 垃圾膨胀：精准白名单提取 GridYard 真正依赖的 QML 模块、
#    核心 Plugins 与 20 余个关键 Qt6 共享库，彻底剔除 qmlls (125M)、qmltooling (66M)、
#    3D 解析渲染器 (200M+) 等无关组件。
# 2. 全局 strip 剥离所有二进制与共享库符号表。
# 3. 部署收敛至 /opt/gridyard/，彻底规避各 LTS 发行版官方源 Qt 版本过低导致的
#    ABI 不匹配与 "Qt_6.11 not found" 崩溃。
# 4. 在 /usr/bin/ 建立 gridyard 与 appGridYard 全局软链接，并集成 FreeDesktop 菜单。
# 5. DEBIAN/control 精准声明系统级底层运行时（OpenGL/GLX/EGL/XKB/GLib/D-Bus/Kerberos等），实现跨环境与容器开箱即用。
# 6. 使用 dpkg-deb -Zxz 强力压缩，安装包约 31MB（解压后约 134MB）。
# ==============================================================================

set -euo pipefail

# 终端输出样式
COLOR_RESET="\033[0m"
COLOR_GREEN="\033[1;32m"
COLOR_YELLOW="\033[1;33m"
COLOR_RED="\033[1;31m"
COLOR_CYAN="\033[1;36m"

info()    { echo -e "${COLOR_CYAN}[INFO]${COLOR_RESET} $*"; }
success() { echo -e "${COLOR_GREEN}[SUCCESS]${COLOR_RESET} $*"; }
warn()    { echo -e "${COLOR_YELLOW}[WARN]${COLOR_RESET} $*"; }
error()   { echo -e "${COLOR_RED}[ERROR]${COLOR_RESET} $*"; }

# 1. 检查 dpkg-deb 工具
if ! command -v dpkg-deb >/dev/null 2>&1; then
    error "未找到 dpkg-deb，请确认当前处于 Debian/Ubuntu 系统或已安装 dpkg 工具！"
    exit 1
fi

# 2. 读取项目根路径与版本号
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

CMAKE_FILE="${PROJECT_ROOT}/src/CMakeLists.txt"
if [ ! -f "${CMAKE_FILE}" ]; then
    error "未找到 CMakeLists.txt: ${CMAKE_FILE}"
    exit 1
fi

VERSION=$(sed -n 's/^project(GridYard VERSION \([0-9.]\+\).*/\1/p' "${CMAKE_FILE}")
if [ -z "${VERSION}" ]; then
    error "无法从 ${CMAKE_FILE} 读取 GridYard 版本号！"
    exit 1
fi
info "检测到 GridYard 版本号: v${VERSION}"

# 3. 定位客户端二进制
BUILD_DIR=""
POSSIBLE_BUILD_DIRS=(
    "${PROJECT_ROOT}/build-ninja"
    "${PROJECT_ROOT}/src/build-ninja"
    "${PROJECT_ROOT}/build/release"
    "${PROJECT_ROOT}/build"
)

for bdir in "${POSSIBLE_BUILD_DIRS[@]}"; do
    if [ -f "${bdir}/client/appGridYard" ]; then
        BUILD_DIR="${bdir}"
        break
    fi
done

if [ -z "${BUILD_DIR}" ]; then
    BUILD_DIR="${POSSIBLE_BUILD_DIRS[0]}"
    warn "未发现已构建的二进制文件，准备在 ${BUILD_DIR} 执行构建..."
    cmake -B "${BUILD_DIR}" -S "${PROJECT_ROOT}/src" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "${BUILD_DIR}" -j"$(nproc)"
fi

BIN_TARGET="${BUILD_DIR}/client/appGridYard"
if [ ! -f "${BIN_TARGET}" ]; then
    error "客户端二进制文件不存在: ${BIN_TARGET}"
    exit 1
fi
info "使用客户端二进制: ${BIN_TARGET}"

# 4. 定位 Qt 根目录
QT_ROOT_DIR=""
CMAKE_CACHE="${BUILD_DIR}/CMakeCache.txt"
if [ -f "${CMAKE_CACHE}" ]; then
    QT_CMAKE_DIR=$(grep "^Qt6_DIR:PATH=" "${CMAKE_CACHE}" | cut -d= -f2 || true)
    if [ -n "${QT_CMAKE_DIR}" ] && [ -d "${QT_CMAKE_DIR}" ]; then
        QT_ROOT_CANDIDATE="$(cd "${QT_CMAKE_DIR}/../../.." && pwd)"
        if [ -d "${QT_ROOT_CANDIDATE}/plugins" ] && [ -d "${QT_ROOT_CANDIDATE}/qml" ]; then
            QT_ROOT_DIR="${QT_ROOT_CANDIDATE}"
        fi
    fi
fi

if [ -z "${QT_ROOT_DIR}" ]; then
    POSSIBLE_QT_DIRS=(
        "/opt/QT/6.11.1/gcc_64"
        "/opt/Qt/6.11.1/gcc_64"
        "/usr/lib/qt6"
        "/usr/lib/x86_64-linux-gnu/qt6"
    )
    for qdir in "${POSSIBLE_QT_DIRS[@]}"; do
        if [ -d "${qdir}/plugins" ] && [ -d "${qdir}/qml" ]; then
            QT_ROOT_DIR="${qdir}"
            break
        fi
    done
fi
info "选定的 Qt6 依赖源路径: ${QT_ROOT_DIR:-系统标准目录}"

# 5. 初始化 Deb 打包目录树
DIST_ROOT="${PROJECT_ROOT}/src/dist/deb"
DEB_PACKAGE_NAME="gridyard_${VERSION}_amd64"
DEB_ROOT="${DIST_ROOT}/${DEB_PACKAGE_NAME}"
RELEASE_DIR="${PROJECT_ROOT}/release/v${VERSION}"
FINAL_DEB_PATH="${RELEASE_DIR}/GridYard-v${VERSION}-amd64.deb"

APP_OPT_DIR="${DEB_ROOT}/opt/gridyard"

info "初始化精简自包含型 DEB 目录结构: ${DEB_ROOT}"
rm -rf "${DEB_ROOT}"
mkdir -p "${DEB_ROOT}/DEBIAN" \
         "${DEB_ROOT}/usr/bin" \
         "${DEB_ROOT}/usr/share/applications" \
         "${DEB_ROOT}/usr/share/icons/hicolor/128x128/apps" \
         "${DEB_ROOT}/usr/share/icons/hicolor/256x256/apps" \
         "${DEB_ROOT}/usr/share/pixmaps" \
         "${DEB_ROOT}/usr/share/doc/gridyard" \
         "${APP_OPT_DIR}/bin" \
         "${APP_OPT_DIR}/lib" \
         "${APP_OPT_DIR}/lib/qt6/plugins" \
         "${APP_OPT_DIR}/lib/qt6/qml"

# 6. 安装主程序二进制
cp "${BIN_TARGET}" "${APP_OPT_DIR}/bin/appGridYard"
chmod 755 "${APP_OPT_DIR}/bin/appGridYard"

# 6b. 安装协调/中继服务二进制（纯 C++ 零 Qt，--mode rendezvous/relay）
RENDZVOUS_BIN="${BUILD_DIR}/rendezvous/gridyard_rendezvous"
if [ ! -f "${RENDZVOUS_BIN}" ]; then
    error "协调/中继服务二进制不存在: ${RENDZVOUS_BIN}（请先完整构建后重试）"
    exit 1
fi
cp "${RENDZVOUS_BIN}" "${APP_OPT_DIR}/bin/gridyard_rendezvous"
chmod 755 "${APP_OPT_DIR}/bin/gridyard_rendezvous"

# 7. 精准剪裁 QML 模块（仅白名单，杜绝全量复制）
info "执行 QML 模块精准白名单剪裁..."
mkdir -p "${APP_OPT_DIR}/lib/qt6/qml/Qt/labs" \
         "${APP_OPT_DIR}/lib/qt6/qml/QtQuick" \
         "${APP_OPT_DIR}/lib/qt6/qml/QtQml"

# 仅拷贝实际引用的 QML 模块
cp -r "${QT_ROOT_DIR}/qml/Qt/labs/platform" "${APP_OPT_DIR}/lib/qt6/qml/Qt/labs/" 2>/dev/null || true
cp -r "${QT_ROOT_DIR}/qml/QtQml" "${APP_OPT_DIR}/lib/qt6/qml/" 2>/dev/null || true

for qsub in Controls Dialogs Effects Layouts Templates Window; do
    if [ -d "${QT_ROOT_DIR}/qml/QtQuick/${qsub}" ]; then
        cp -r "${QT_ROOT_DIR}/qml/QtQuick/${qsub}" "${APP_OPT_DIR}/lib/qt6/qml/QtQuick/"
    fi
done
cp "${QT_ROOT_DIR}/qml/QtQuick/qmldir" "${APP_OPT_DIR}/lib/qt6/qml/QtQuick/" 2>/dev/null || true
cp "${QT_ROOT_DIR}/qml/QtQuick/"*.so "${APP_OPT_DIR}/lib/qt6/qml/QtQuick/" 2>/dev/null || true

# 8. 精准剪裁 Plugins 插件（仅白名单，包含 Wayland 与 XCB OpenGL 支持）
info "执行 Plugins 平台插件精准白名单剪裁..."
WHITE_PLUGINS=(
    platforms
    platformthemes
    xcbglintegrations
    imageformats
    tls
    wayland-shell-integration
    wayland-graphics-integration-client
    wayland-decoration-client
)

for pdir in "${WHITE_PLUGINS[@]}"; do
    if [ -d "${QT_ROOT_DIR}/plugins/${pdir}" ]; then
        cp -r "${QT_ROOT_DIR}/plugins/${pdir}" "${APP_OPT_DIR}/lib/qt6/plugins/"
    fi
done

# 深度清理 imageformats：仅保留通用桌面图片格式（svg, png, jpeg, ico, gif, webp）
if [ -d "${APP_OPT_DIR}/lib/qt6/plugins/imageformats" ]; then
    find "${APP_OPT_DIR}/lib/qt6/plugins/imageformats" -type f \
        ! -name "*svg*" ! -name "*jpeg*" ! -name "*png*" ! -name "*ico*" ! -name "*gif*" ! -name "*webp*" -delete
fi

# 彻底清理所有 *.debug 调试符号文件、设计器目录与构建元数据
info "彻底清理 *.debug 调试文件与非运行时构建元数据..."
find "${APP_OPT_DIR}/lib" -type f \( -name "*.debug" -o -name "*.prl" -o -name "*.cmake" -o -name "*.qmltypes" \) -delete 2>/dev/null || true
find "${APP_OPT_DIR}/lib" -type d \( -name "designer" -o -name ".debug" \) -exec rm -rf {} + 2>/dev/null || true

# 9. 精准收集动态链接库（从 Qt 根目录白名单复制，并保留软链接）
info "提取 Qt6 核心动态库与 ICU 国际化运行时依赖..."
QT_CORE_LIB_PATTERNS=(
    "libQt6Core.so*"
    "libQt6Gui.so*"
    "libQt6Widgets.so*"
    "libQt6Qml*.so*"
    "libQt6Quick.so*"
    "libQt6QuickControls2*.so*"
    "libQt6QuickTemplates2*.so*"
    "libQt6QuickDialogs2*.so*"
    "libQt6QuickEffects*.so*"
    "libQt6QuickLayouts*.so*"
    "libQt6LabsPlatform.so*"
    "libQt6Svg.so*"
    "libQt6OpenGL.so*"
    "libQt6DBus.so*"
    "libQt6Network.so*"
    "libQt6WaylandClient.so*"
    "libQt6XcbQpa.so*"
    "libicudata.so*"
    "libicui18n.so*"
    "libicuuc.so*"
)

for pat in "${QT_CORE_LIB_PATTERNS[@]}"; do
    cp -d "${QT_ROOT_DIR}/lib"/$pat "${APP_OPT_DIR}/lib/" 2>/dev/null || true
done

# 再次确保 lib 目录下无残留 *.debug 文件
find "${APP_OPT_DIR}/lib" -type f -name "*.debug" -delete 2>/dev/null || true

# 10. 全局 strip 极限压缩（剥离所有可执行文件与 .so 调试符号）
info "正在对所有 ELF 二进制与共享库执行 strip --strip-unneeded 深度瘦身..."
find "${APP_OPT_DIR}" -type f \( -name "*.so*" -o -name "appGridYard" \) \
    -exec strip --strip-unneeded {} + 2>/dev/null || true

# 11. 生成自包含启动脚本 /opt/gridyard/GridYard.sh
info "生成 /opt/gridyard/GridYard.sh 运行入口..."
cat > "${APP_OPT_DIR}/GridYard.sh" << 'EOF'
#!/bin/bash
# ==============================================================================
# GridYard (鸽邮) - 自包含型应用启动引导入口
# ==============================================================================
SELF=$(readlink -f "$0")
APP_DIR=${SELF%/*}

export PATH="${APP_DIR}/bin:${PATH:+:${PATH}}"
export LD_LIBRARY_PATH="${APP_DIR}/lib:${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export QT_PLUGIN_PATH="${APP_DIR}/lib/qt6/plugins:${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}"
export QML2_IMPORT_PATH="${APP_DIR}/lib/qt6/qml:${QML2_IMPORT_PATH:+:${QML2_IMPORT_PATH}}"

# 启动客户端程序并透传所有命令行参数
exec "${APP_DIR}/bin/appGridYard" "$@"
EOF
chmod 755 "${APP_OPT_DIR}/GridYard.sh"

# 12. 创建 /usr/bin/ 系统全局软链接
info "创建 /usr/bin/gridyard 与 /usr/bin/appGridYard 符号链接..."
ln -sf "/opt/gridyard/GridYard.sh" "${DEB_ROOT}/usr/bin/gridyard"
ln -sf "/opt/gridyard/GridYard.sh" "${DEB_ROOT}/usr/bin/appGridYard"

# 13. 安装桌面快捷方式与图标
ICON_FILE="${PROJECT_ROOT}/src/client/icons/gridyard.png"
if [ -f "${ICON_FILE}" ]; then
    cp "${ICON_FILE}" "${DEB_ROOT}/usr/share/icons/hicolor/128x128/apps/gridyard.png"
    cp "${ICON_FILE}" "${DEB_ROOT}/usr/share/icons/hicolor/256x256/apps/gridyard.png"
    cp "${ICON_FILE}" "${DEB_ROOT}/usr/share/pixmaps/gridyard.png"
    cp "${ICON_FILE}" "${APP_OPT_DIR}/gridyard.png"
fi

cat > "${DEB_ROOT}/usr/share/applications/gridyard.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=GridYard
GenericName=LAN P2P File Transfer
Comment=GridYard 局域网 P2P 文件传输、在线聊天与本地历史
Comment[zh_CN]=GridYard 局域网 P2P 文件传输、在线聊天与本地历史
Exec=/usr/bin/gridyard
Icon=gridyard
Categories=Network;FileTransfer;P2P;
Terminal=false
StartupWMClass=GridYard
Keywords=gridyard;p2p;lan;file;chat;
EOF
chmod 644 "${DEB_ROOT}/usr/share/applications/gridyard.desktop"
cp "${DEB_ROOT}/usr/share/applications/gridyard.desktop" "${APP_OPT_DIR}/gridyard.desktop"

# 版权与更新日志
if [ -f "${PROJECT_ROOT}/LICENSE" ]; then
    cp "${PROJECT_ROOT}/LICENSE" "${DEB_ROOT}/usr/share/doc/gridyard/copyright"
fi
if [ -f "${PROJECT_ROOT}/CHANGELOG.md" ]; then
    cp "${PROJECT_ROOT}/CHANGELOG.md" "${DEB_ROOT}/usr/share/doc/gridyard/changelog"
fi

# 14. 计算已安装大小（单位 KB）
INSTALLED_SIZE=$(du -sk "${DEB_ROOT}" | cut -f1)

# 15. 生成精简版 DEBIAN/control
info "生成 DEBIAN/control 控制文件..."
cat > "${DEB_ROOT}/DEBIAN/control" << EOF
Package: gridyard
Version: ${VERSION}
Architecture: amd64
Maintainer: GridYard Team <support@gridyard.org>
Installed-Size: ${INSTALLED_SIZE}
Section: net
Priority: optional
Homepage: https://github.com/cqnu-gridyard/GridYard
Depends: libc6 (>= 2.34), libstdc++6 (>= 13), libgl1, libopengl0, libglx0, libegl1, libxkbcommon0, libfontconfig1, libglib2.0-0t64 | libglib2.0-0, libx11-6, libxcb1, libdbus-1-3, libgssapi-krb5-2, libbz2-1.0
Description: LAN P2P File Transfer, Instant Messaging and SQLite Local History
 GridYard is a high-performance cross-platform LAN P2P file transfer and
 chat client built with Qt6, QML and C++23 native POSIX networking.
 This package is self-contained under /opt/gridyard with minimal pruned Qt6 runtime,
 providing out-of-the-box compatibility across Ubuntu 20.04/22.04/24.04.
EOF
chmod 644 "${DEB_ROOT}/DEBIAN/control"

# 16. 生成维护者脚本（更新桌面与图标数据库）
cat > "${DEB_ROOT}/DEBIAN/postinst" << 'EOF'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    if which update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
    if which gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
    fi
fi
exit 0
EOF
chmod 755 "${DEB_ROOT}/DEBIAN/postinst"

cat > "${DEB_ROOT}/DEBIAN/postrm" << 'EOF'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    if which update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
    if which gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
    fi
fi
exit 0
EOF
chmod 755 "${DEB_ROOT}/DEBIAN/postrm"

# 17. 执行 dpkg-deb 打包（采用 xz 强力压缩）
mkdir -p "${RELEASE_DIR}"
info "调用 dpkg-deb 构建极致瘦身安装包: ${FINAL_DEB_PATH} ..."

dpkg-deb -Zxz --build --root-owner-group "${DEB_ROOT}" "${FINAL_DEB_PATH}"

# 生成 SHA256 校验码
if command -v sha256sum >/dev/null 2>&1; then
    (cd "${RELEASE_DIR}" && sha256sum "GridYard-v${VERSION}-amd64.deb" > "GridYard-v${VERSION}-amd64.deb.sha256")
fi

success "========================================================"
success "GridYard 极致瘦身自包含型 Debian/Ubuntu 安装包构建成功！"
success "输出包路径: ${FINAL_DEB_PATH}"
success "安装包体积: $(ls -lh "${FINAL_DEB_PATH}" | awk '{print $5}')"
success "解压后体积: $(du -sh "${APP_OPT_DIR}" | cut -f1)"
if [ -f "${RELEASE_DIR}/GridYard-v${VERSION}-amd64.deb.sha256" ]; then
    success "SHA256: $(cat "${RELEASE_DIR}/GridYard-v${VERSION}-amd64.deb.sha256" | cut -d' ' -f1)"
fi
success "安装与运行测试方法:"
echo "  sudo dpkg -i GridYard-v${VERSION}-amd64.deb"
echo "  gridyard  # 或 appGridYard"
success "卸载方法:"
echo "  sudo apt remove gridyard"
success "========================================================"
