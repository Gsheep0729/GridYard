#!/bin/bash
# ==============================================================================
# GridYard (鸽邮) - Linux 绿色解压版 (.tar.gz) 自动化打包脚本
#
# 功能说明：
# 1. 自动从 src/CMakeLists.txt 提取版本号。
# 2. 从 Qt 根目录（或 ldd 回退）收集 Qt 动态库、插件与 QML 模块，覆盖系统托盘依赖。
# 3. 生成符合自包含规范的独立分发目录：GridYard-v<VERSION>-linux-x86_64/
# 4. 内置标准化 GridYard.sh 启动脚本（自动配置 LD_LIBRARY_PATH、QML2_IMPORT_PATH 等）。
# 5. 使用 tar -czvf 压缩生成 release/ 目录下的 .tar.gz 发布包，并生成 SHA256 校验码。
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

# 1. 确定目录基准与读取版本号
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

info "工程根目录: ${PROJECT_ROOT}"

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

# 2. 定位构建目录与二进制
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
    error "可执行文件不存在: ${BIN_TARGET}"
    exit 1
fi
info "使用客户端二进制: ${BIN_TARGET}"

# 3. 准备打包工作目录
DIST_ROOT="${PROJECT_ROOT}/src/dist"
PKG_BASENAME="GridYard-v${VERSION}-linux-x86_64"
PKG_DIR="${DIST_ROOT}/${PKG_BASENAME}"
RELEASE_DIR="${PROJECT_ROOT}/release/v${VERSION}"
TARBALL_PATH="${RELEASE_DIR}/${PKG_BASENAME}.tar.gz"

info "清理旧目录并初始化绿色包目录: ${PKG_DIR}"
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin" \
         "${PKG_DIR}/usr/lib/qt6" \
         "${PKG_DIR}/usr/share/applications" \
         "${PKG_DIR}/usr/share/icons/hicolor/128x128/apps" \
         "${PKG_DIR}/usr/share/icons/hicolor/256x256/apps" \
         "${PKG_DIR}/usr/share/pixmaps"

# 4. 拷贝可执行文件、图标与桌面文件
cp "${BIN_TARGET}" "${PKG_DIR}/usr/bin/"

# 协调/中继服务二进制（纯 C++ 零 Qt，--mode rendezvous/relay）随包交付
RENDZVOUS_BIN="${BUILD_DIR}/rendezvous/gridyard_rendezvous"
if [ ! -f "${RENDZVOUS_BIN}" ]; then
    error "协调/中继服务二进制不存在: ${RENDZVOUS_BIN}（请先完整构建后重试）"
    exit 1
fi
cp "${RENDZVOUS_BIN}" "${PKG_DIR}/usr/bin/"

ICON_FILE="${PROJECT_ROOT}/src/client/icons/gridyard.png"
if [ -f "${ICON_FILE}" ]; then
    cp "${ICON_FILE}" "${PKG_DIR}/usr/share/icons/hicolor/128x128/apps/gridyard.png"
    cp "${ICON_FILE}" "${PKG_DIR}/usr/share/icons/hicolor/256x256/apps/gridyard.png"
    cp "${ICON_FILE}" "${PKG_DIR}/usr/share/pixmaps/gridyard.png"
    cp "${ICON_FILE}" "${PKG_DIR}/gridyard.png"
fi

cat > "${PKG_DIR}/usr/share/applications/gridyard.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=GridYard
GenericName=LAN P2P File Transfer
Comment=GridYard 局域网 P2P 文件传输、在线聊天与本地历史
Comment[zh_CN]=GridYard 局域网 P2P 文件传输、在线聊天与本地历史
Exec=GridYard.sh
Icon=gridyard
Categories=Network;FileTransfer;P2P;
Terminal=false
StartupWMClass=GridYard
Keywords=gridyard;p2p;lan;file;chat;
EOF
cp "${PKG_DIR}/usr/share/applications/gridyard.desktop" "${PKG_DIR}/gridyard.desktop"

# 5. 智能寻找 Qt6 安装路径并补齐动态库、plugins 与 qml
QT_ROOT_DIR=""

# 从 CMakeCache 中尝试解析 Qt6_DIR
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

# 常见路径备选
if [ -z "${QT_ROOT_DIR}" ]; then
    POSSIBLE_QT_DIRS=(
        "/opt/QT/6.11.1/gcc_64"
        "/opt/Qt/6.11.1/gcc_64"
        "/usr/lib/qt6"
        "/usr/lib/x86_64-linux-gnu/qt6"
        "/usr"
    )
    for qdir in "${POSSIBLE_QT_DIRS[@]}"; do
        if [ -d "${qdir}/plugins" ] && [ -d "${qdir}/qml" ]; then
            QT_ROOT_DIR="${qdir}"
            break
        fi
    done
fi
info "选定的 Qt6 依赖源路径: ${QT_ROOT_DIR:-系统标准目录}"

info "正在提取 Qt6 核心动态库与 ICU 依赖..."
mkdir -p "${PKG_DIR}/usr/lib"
if [ -n "${QT_ROOT_DIR}" ] && [ -d "${QT_ROOT_DIR}/lib" ]; then
    cp -d "${QT_ROOT_DIR}/lib"/libQt6*.so* "${PKG_DIR}/usr/lib/" 2>/dev/null || true
    cp -d "${QT_ROOT_DIR}/lib"/libicu*.so* "${PKG_DIR}/usr/lib/" 2>/dev/null || true
else
    ldd "${BIN_TARGET}" | grep "=>" | awk '{print $3}' | grep -E "libQt6|libicu" | while read -r libpath; do
        if [ -n "$libpath" ] && [ -f "$libpath" ]; then
            cp -L "$libpath" "${PKG_DIR}/usr/lib/" 2>/dev/null || true
        fi
    done
fi

# 补齐 Qt6 plugins
mkdir -p "${PKG_DIR}/usr/lib/qt6/plugins"
if [ -n "${QT_ROOT_DIR}" ] && [ -d "${QT_ROOT_DIR}/plugins" ]; then
    cp -r "${QT_ROOT_DIR}/plugins/"* "${PKG_DIR}/usr/lib/qt6/plugins/" 2>/dev/null || true
elif [ -d "/usr/lib/qt6/plugins" ]; then
    cp -r /usr/lib/qt6/plugins/* "${PKG_DIR}/usr/lib/qt6/plugins/" 2>/dev/null || true
elif [ -d "/usr/lib/x86_64-linux-gnu/qt6/plugins" ]; then
    cp -r /usr/lib/x86_64-linux-gnu/qt6/plugins/* "${PKG_DIR}/usr/lib/qt6/plugins/" 2>/dev/null || true
fi

# 补齐 Qt6 qml 模块
mkdir -p "${PKG_DIR}/usr/lib/qt6/qml"
if [ -n "${QT_ROOT_DIR}" ] && [ -d "${QT_ROOT_DIR}/qml" ]; then
    cp -r "${QT_ROOT_DIR}/qml/"* "${PKG_DIR}/usr/lib/qt6/qml/" 2>/dev/null || true
elif [ -d "/usr/lib/qt6/qml" ]; then
    cp -r /usr/lib/qt6/qml/* "${PKG_DIR}/usr/lib/qt6/qml/" 2>/dev/null || true
elif [ -d "/usr/lib/x86_64-linux-gnu/qt6/qml" ]; then
    cp -r /usr/lib/x86_64-linux-gnu/qt6/qml/* "${PKG_DIR}/usr/lib/qt6/qml/" 2>/dev/null || true
fi

# 补齐 libQt6Widgets.so.6（托盘后端支持）与 libQt6Svg.so.6（SVG 图标渲染支持）
mkdir -p "${PKG_DIR}/usr/lib"
EXTRA_QT_LIBS=("libQt6Widgets.so.6" "libQt6Svg.so.6")

for libname in "${EXTRA_QT_LIBS[@]}"; do
    FOUND_LIB=false
    POSSIBLE_PATHS=(
        "${QT_ROOT_DIR:+${QT_ROOT_DIR}/lib/${libname}}"
        "/usr/lib/${libname}"
        "/usr/lib/x86_64-linux-gnu/${libname}"
    )
    for lpath in "${POSSIBLE_PATHS[@]}"; do
        if [ -n "${lpath}" ] && [ -e "${lpath}" ]; then
            cp -L "${lpath}" "${PKG_DIR}/usr/lib/"
            FOUND_LIB=true
            break
        fi
    done
    if [ "${FOUND_LIB}" = false ]; then
        warn "未能在标准路径下找到 ${libname}，可能影响对应功能（系统托盘或矢量图标）。"
    fi
done

# 7. 写入独立的 GridYard.sh 绿色版启动脚本
info "生成绿色解压版启动引导脚本: ${PKG_DIR}/GridYard.sh"
cat > "${PKG_DIR}/GridYard.sh" << 'EOF'
#!/bin/bash
# ==============================================================================
# GridYard (鸽邮) - 绿色解压版启动脚本
# ==============================================================================
SELF=$(readlink -f "$0")
HERE=${SELF%/*}

export PATH="${HERE}/usr/bin:${PATH:+:${PATH}}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:+:${XDG_DATA_DIRS}}"
export QT_PLUGIN_PATH="${HERE}/usr/lib/qt6/plugins:${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}"
export QML2_IMPORT_PATH="${HERE}/usr/lib/qt6/qml:${QML2_IMPORT_PATH:+:${QML2_IMPORT_PATH}}"

# 启动客户端程序，透传所有命令行参数
exec "${HERE}/usr/bin/appGridYard" "$@"
EOF

chmod +x "${PKG_DIR}/GridYard.sh"

# 8. 打包为 .tar.gz
mkdir -p "${RELEASE_DIR}"
info "正在使用 tar -czvf 打包为 ${TARBALL_PATH} ..."

tar -czf "${TARBALL_PATH}" -C "${DIST_ROOT}" "${PKG_BASENAME}"

# 生成 SHA256 校验码
if command -v sha256sum >/dev/null 2>&1; then
    (cd "${RELEASE_DIR}" && sha256sum "${PKG_BASENAME}.tar.gz" > "${PKG_BASENAME}.tar.gz.sha256")
fi

success "========================================================"
success "GridYard Linux 绿色解压版构建成功！"
success "输出包路径: ${TARBALL_PATH}"
success "包体大小: $(du -h "${TARBALL_PATH}" | cut -f1)"
if [ -f "${RELEASE_DIR}/${PKG_BASENAME}.tar.gz.sha256" ]; then
    success "SHA256: $(cat "${RELEASE_DIR}/${PKG_BASENAME}.tar.gz.sha256" | cut -d' ' -f1)"
fi
success "解压与运行方法:"
echo "  tar -xzvf GridYard-v${VERSION}-linux-x86_64.tar.gz"
echo "  cd GridYard-v${VERSION}-linux-x86_64"
echo "  ./GridYard.sh"
success "========================================================"
