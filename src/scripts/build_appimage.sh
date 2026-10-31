#!/bin/bash
# ==============================================================================
# GridYard (鸽邮) - Linux AppImage 自动化打包脚本（兼容勘察 C-08 构建降级修复）
#
# 功能说明：
# 1. 自动从 src/CMakeLists.txt 提取版本号，复用已配置的构建目录二进制。
# 2. 从 CMakeCache 解析 Qt 根目录（官方安装，GLIBC 基线 2.34），全量收集
#    Qt 动态库、plugins 与 QML 模块。
# 3. 以 ldd 定点迭代递归收集系统共享库（排除 glibc 核心库，交目标系统提供）。
# 4. 打包前强制执行 GLIBC 基线守卫（check_appimage_glibc.py）：
#    "构建机 glibc ≤ 目标发行版"检查项，杜绝 C-08 式工件（GLIBC_2.43 > 2.39
#    在 Ubuntu 24.04 启动即失败）复发。
# 5. 找到 appimagetool 时产出 release/v<VERSION>/GridYard-v<VERSION>-x86_64.AppImage；
#    未找到时仅产出并校验 AppDir（导出 APPIMAGETOOL 指向后可重跑补打包）。
#
# 用法：
#   bash src/scripts/build_appimage.sh
#   APPIMAGETOOL=/tmp/appimagetool APPDIR=/tmp/MyAppDir bash src/scripts/build_appimage.sh
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

# 允许的最高 GLIBC 基线（目标发行版 Ubuntu 24.04 = 2.39，见兼容勘察 C-08）
GLIBC_MAX="${GLIBC_MAX:-2.39}"

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

# 2. 定位构建目录与二进制（与 build_tarball.sh 同款发现逻辑）
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

# 3. 智能寻找 Qt6 安装路径（优先 CMakeCache 的 Qt6_DIR，与构建同源）
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
if [ -z "${QT_ROOT_DIR}" ]; then
    error "未找到带 plugins/ 与 qml/ 的 Qt6 根目录，无法组装自包含 AppDir"
    exit 1
fi
info "选定的 Qt6 依赖源路径: ${QT_ROOT_DIR}"

# 构建机 glibc 预检（C-08 检查项：构建机 glibc 必须 ≤ 目标发行版基线）
HOST_GLIBC="$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$' || true)"
if [ -n "${HOST_GLIBC}" ]; then
    if [ "$(printf '%s\n' "${GLIBC_MAX}" "${HOST_GLIBC}" | sort -V | head -1)" != "${GLIBC_MAX}" ]; then
        error "构建机 glibc ${HOST_GLIBC} 高于目标基线 ${GLIBC_MAX}（C-08 检查项），禁止在此环境打包"
        exit 1
    fi
    info "构建机 glibc ${HOST_GLIBC} ≤ 目标基线 ${GLIBC_MAX}，预检通过"
else
    warn "无法解析构建机 glibc 版本，跳过预检（稍后仍会执行工件级基线守卫）"
fi

# 4. 组装 AppDir（默认仓库根 dist/AppDir，与既有工件位置一致）
APPDIR="${APPDIR:-${PROJECT_ROOT}/dist/AppDir}"
OUT_DIR="${PROJECT_ROOT}/release/v${VERSION}"
APPIMAGE="${OUT_DIR}/GridYard-v${VERSION}-x86_64.AppImage"

info "清理旧目录并初始化 AppDir: ${APPDIR}"
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" \
         "${APPDIR}/usr/lib/qt6/plugins" \
         "${APPDIR}/usr/lib/qt6/qml" \
         "${APPDIR}/usr/share/applications" \
         "${APPDIR}/usr/share/icons/hicolor/128x128/apps" \
         "${APPDIR}/usr/share/icons/hicolor/256x256/apps" \
         "${APPDIR}/usr/share/pixmaps"

cp "${BIN_TARGET}" "${APPDIR}/usr/bin/"

ICON_FILE="${PROJECT_ROOT}/src/client/icons/gridyard.png"
ICON_FILE_128="${PROJECT_ROOT}/src/client/icons/gridyard-128.png"
if [ -f "${ICON_FILE}" ] && [ -f "${ICON_FILE_128}" ]; then
    cp "${ICON_FILE_128}" "${APPDIR}/usr/share/icons/hicolor/128x128/apps/gridyard.png"
    cp "${ICON_FILE}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/gridyard.png"
    cp "${ICON_FILE}" "${APPDIR}/usr/share/pixmaps/gridyard.png"
    cp "${ICON_FILE}" "${APPDIR}/gridyard.png"
elif [ -f "${ICON_FILE}" ]; then
    cp "${ICON_FILE}" "${APPDIR}/usr/share/icons/hicolor/128x128/apps/gridyard.png"
    cp "${ICON_FILE}" "${APPDIR}/gridyard.png"
else
    warn "未找到图标文件 ${ICON_FILE}，AppDir 将缺图标"
fi

cat > "${APPDIR}/usr/share/applications/gridyard.desktop" << 'EOF'
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
cp "${APPDIR}/usr/share/applications/gridyard.desktop" "${APPDIR}/gridyard.desktop"

# 5. 收集 Qt 动态库（官方安装构建，GLIBC 基线 2.34）、plugins 与 QML 模块
info "提取 Qt6 动态库与 ICU 依赖..."
cp -d "${QT_ROOT_DIR}/lib"/libQt6*.so* "${APPDIR}/usr/lib/" 2>/dev/null || true
cp -d "${QT_ROOT_DIR}/lib"/libicu*.so* "${APPDIR}/usr/lib/" 2>/dev/null || true

info "全量收集 Qt6 plugins 与 qml 模块..."
cp -r "${QT_ROOT_DIR}/plugins/"* "${APPDIR}/usr/lib/qt6/plugins/"
cp -r "${QT_ROOT_DIR}/qml/"*     "${APPDIR}/usr/lib/qt6/qml/"

# 6. ldd 定点迭代递归收集系统共享库（排除 glibc 核心：由目标系统提供，
#    捆绑更高版本的 glibc 核心正是 C-08 工件不可运行的反面教材）
EXCLUDE_RE='^(linux-vdso|ld-linux|libc\.so\.|libm\.so\.|libmvec\.so\.|libpthread\.so\.|libdl\.so\.|librt\.so\.|libresolv\.so\.|libutil\.so\.|libanl\.so\.|libnss_)'
declare -A HANDLED=()
declare -a MISSING=()
ADDED=0

collect_pass() {
    ADDED=0
    while IFS= read -r -d '' elf; do
        local real
        real="$(readlink -f "${elf}")"
        [ -n "${HANDLED[${real}]:-}" ] && continue
        HANDLED["${real}"]=1
        local deps
        deps="$(LD_LIBRARY_PATH="${APPDIR}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
                ldd "${elf}" 2>/dev/null \
                | awk '/not found/ { print "MISSING\t" $0; next } /=> \// { print $1 "\t" $3 }')" || true
        [ -n "${deps}" ] || continue
        while IFS=$'\t' read -r soname path; do
            if [ "${soname}" = "MISSING" ]; then
                case "${elf}" in
                    # 可选插件（plugins/qml 树）缺依赖时 Qt 运行时按需加载失败即
                    # 跳过该插件，不破坏主流程（与绿色包同语义），降级为告警；
                    # 核心件（主程序与 usr/lib 顶层库）缺依赖才致命。
                    "${APPDIR}"/usr/lib/qt6/*)
                        warn "可选插件缺依赖（运行时将被跳过）：${elf}: ${path}"
                        ;;
                    *)
                        MISSING+=("${elf}: ${path}")
                        ;;
                esac
                continue
            fi
            [[ "${soname}" =~ ${EXCLUDE_RE} ]] && continue
            case "${path}" in
                "${APPDIR}"/*) continue ;;   # 已捆绑（Qt 自身库等）
            esac
            [ -n "${HANDLED[${soname}]:-}" ] && continue
            [ -e "${APPDIR}/usr/lib/${soname}" ] && continue
            HANDLED["${soname}"]=1
            if cp -L "${path}" "${APPDIR}/usr/lib/${soname}" 2>/dev/null; then
                ADDED=$((ADDED+1))
            else
                error "依赖拷贝失败: ${soname} (${path})"
                exit 1
            fi
        done <<< "${deps}"
    done < <(find "${APPDIR}" \( -type f -o -type l \) -name '*.so*' -print0 \
             ; printf '%s\0' "${APPDIR}/usr/bin/appGridYard")
}

info "定点迭代收集系统依赖（排除 glibc 核心: ld-linux/libc/libm/libpthread 等）..."
for i in 1 2 3 4 5 6 7 8 9 10; do
    collect_pass
    info "  第 ${i} 轮新增 ${ADDED} 个依赖库"
    if [ "${ADDED}" -eq 0 ]; then
        break
    fi
done

if [ "${#MISSING[@]}" -gt 0 ]; then
    error "存在无法解析的依赖（即使以 AppDir/usr/lib 优先搜索）："
    printf '  %s\n' "${MISSING[@]}"
    exit 1
fi

# 7. 写入 AppRun（AppImage 挂载点启动脚本）
cat > "${APPDIR}/AppRun" << 'EOF'
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
chmod +x "${APPDIR}/AppRun"
[ -e "${APPDIR}/.DirIcon" ] || ln -s gridyard.png "${APPDIR}/.DirIcon"

# 8. GLIBC 基线守卫（C-08 检查项闸门，超基线即失败不产 AppImage）
info "执行 GLIBC 基线守卫（允许最高 GLIBC_${GLIBC_MAX}）..."
python3 "${PROJECT_ROOT}/src/scripts/check_appimage_glibc.py" --max "${GLIBC_MAX}" "${APPDIR}"

info "AppDir 组装完成: ${APPDIR}（$(du -sh "${APPDIR}" | cut -f1)）"

# 9. 打包 AppImage（appimagetool 可用时）
APPIMAGETOOL="${APPIMAGETOOL:-/tmp/appimagetool}"
if [ -x "${APPIMAGETOOL}" ]; then
    mkdir -p "${OUT_DIR}"
    info "正在使用 appimagetool 打包: ${APPIMAGE}"
    if ! env -u DISPLAY ARCH=x86_64 "${APPIMAGETOOL}" "${APPDIR}" "${APPIMAGE}"; then
        warn "直接调用失败（可能缺 FUSE），改用 --appimage-extract-and-run 重试..."
        env -u DISPLAY ARCH=x86_64 "${APPIMAGETOOL}" --appimage-extract-and-run \
            "${APPDIR}" "${APPIMAGE}"
    fi
    success "AppImage 打包完成: ${APPIMAGE}"
else
    warn "未找到 appimagetool（${APPIMAGETOOL}）；已产出并校验 AppDir。"
    warn "导出 APPIMAGETOOL 指向 appimagetool 后重跑本脚本即可补打包。"
fi

success "========================================================"
success "GLIBC 基线: 构建 glibc ≤ ${GLIBC_MAX}（Ubuntu 24.04），工件可运行性已守卫"
success "========================================================"
