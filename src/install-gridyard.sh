#!/usr/bin/env bash

set -euo pipefail

APP_NAME="GridYard"
VERSION="6.8.1"
VERSION_LABEL="v${VERSION}"
APPIMAGE_NAME="GridYard-${VERSION_LABEL}-x86_64.AppImage"
APPIMAGE_SIZE_HINT="92.2 MB"
DOWNLOAD_URL="https://github.com/Gsheep0729/GridYard/releases/download/${VERSION_LABEL}/${APPIMAGE_NAME}"

INSTALL_DIR="/opt/GridYard"
APPIMAGE_TARGET="${INSTALL_DIR}/${APPIMAGE_NAME}"
APPIMAGE_LINK="${INSTALL_DIR}/GridYard.AppImage"
VERSION_FILE="${INSTALL_DIR}/VERSION"
ICON_TARGET="${INSTALL_DIR}/CQNU.png"
DESKTOP_FILE="/usr/share/applications/gridyard.desktop"
DESKTOP_DATABASE_DIR="/usr/share/applications/"
TMP_FILE="/tmp/${APPIMAGE_NAME}.tmp"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ICON_SOURCE="${PROJECT_ROOT}/doc/CQNU.png"

ASSUME_YES=0
FORCE_INSTALL=0

print_usage() {
    cat <<EOF
用法：
  sudo ./src/install-gridyard.sh [选项]

选项：
  -y, --yes      跳过确认，直接安装
  --force        允许覆盖比 ${VERSION_LABEL} 更新的已安装版本
  -h, --help     显示帮助
EOF
}

log() {
    printf '%s\n' "$1"
}

die() {
    printf '错误：%s\n' "$1" >&2
    exit 1
}

version_is_newer_than_target() {
    local installed="$1"

    if [[ -z "${installed}" ]]; then
        return 1
    fi

    [[ "$(printf '%s\n%s\n' "${VERSION}" "${installed}" | sort -V | tail -n 1)" == "${installed}" \
        && "${installed}" != "${VERSION}" ]]
}

choose_downloader() {
    if command -v curl >/dev/null 2>&1; then
        DOWNLOADER="curl"
        return 0
    fi

    if command -v wget >/dev/null 2>&1; then
        DOWNLOADER="wget"
        return 0
    fi

    die "未找到 curl 或 wget，无法下载 AppImage。请先安装其中一个工具。"
}

download_appimage() {
    rm -f "${TMP_FILE}"

    if [[ "${DOWNLOADER}" == "curl" ]]; then
        curl -L --fail --progress-bar "${DOWNLOAD_URL}" -o "${TMP_FILE}"
    else
        wget --progress=bar:force:noscroll -O "${TMP_FILE}" "${DOWNLOAD_URL}"
    fi

    [[ -s "${TMP_FILE}" ]] || die "下载文件为空，已停止安装。"
}

cleanup() {
    rm -f "${TMP_FILE}"
}

cancel_install() {
    log ""
    log "已取消安装，脚本退出。"
    exit 130
}

trap cleanup EXIT
trap cancel_install INT TERM

while [[ $# -gt 0 ]]; do
    case "$1" in
        -y|--yes)
            ASSUME_YES=1
            shift
            ;;
        --force)
            FORCE_INSTALL=1
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            die "未知参数：$1"
            ;;
    esac
done

cat <<EOF
GridYard AppImage 一键部署脚本

这是面向 Manjaro 定制系统环境准备的一键安装脚本。

本脚本将执行以下操作：
1. 从 GitHub 下载 GridYard ${VERSION_LABEL} AppImage
   ${DOWNLOAD_URL}
   文件大小约 ${APPIMAGE_SIZE_HINT}，请确认当前网络环境稳定，并预留足够下载时间。

2. 安装到：
   ${APPIMAGE_TARGET}

3. 创建稳定启动链接：
   ${APPIMAGE_LINK}

4. 复制应用图标：
   ${ICON_SOURCE} -> ${ICON_TARGET}

5. 写入桌面菜单文件：
   ${DESKTOP_FILE}

6. 刷新桌面应用数据库：
   update-desktop-database ${DESKTOP_DATABASE_DIR}

该操作需要 root 权限，因为需要写入 /opt 和 /usr/share/applications。
EOF

log ""
log "正在检查运行权限..."
if [[ "$(id -u)" -ne 0 ]]; then
    cat <<EOF
权限不足：请使用 sudo 运行本脚本。
示例：
  sudo ./src/install-gridyard.sh
EOF
    exit 1
fi
log "权限检查通过。"

if [[ "${ASSUME_YES}" -ne 1 ]]; then
    log ""
    read -r -p "是否继续安装 GridYard ${VERSION_LABEL}？[y/N] " CONFIRM
    case "${CONFIRM}" in
        y|Y)
            ;;
        *)
            log "已取消安装，脚本退出。"
            exit 0
            ;;
    esac
fi

log ""
log "[1/7] 正在检查本地项目信息..."
log "      脚本目录：${SCRIPT_DIR}"
log "      项目目录：${PROJECT_ROOT}"
log "      图标文件：${ICON_SOURCE}"
[[ -f "${ICON_SOURCE}" ]] || die "未找到图标文件：${ICON_SOURCE}"

choose_downloader
log "      下载工具：${DOWNLOADER}"

log ""
log "[2/7] 正在检查已安装版本..."
INSTALLED_VERSION=""
if [[ -f "${VERSION_FILE}" ]]; then
    INSTALLED_VERSION="$(tr -d '[:space:]' < "${VERSION_FILE}")"
fi

if [[ -z "${INSTALLED_VERSION}" ]]; then
    log "      当前未检测到已安装版本，将执行全新安装。"
elif version_is_newer_than_target "${INSTALLED_VERSION}" && [[ "${FORCE_INSTALL}" -ne 1 ]]; then
    cat <<EOF
      已安装版本：${INSTALLED_VERSION}
      目标版本：${VERSION}
      已安装版本比目标版本更新，默认不降级。
      如确需覆盖，请重新执行：
        sudo ./src/install-gridyard.sh --force
      脚本退出。
EOF
    exit 0
elif [[ "${INSTALLED_VERSION}" == "${VERSION}" ]]; then
    log "      已安装版本：${INSTALLED_VERSION}"
    log "      将重新安装当前版本，覆盖现有文件。"
else
    log "      已安装版本：${INSTALLED_VERSION}"
    log "      目标版本：${VERSION}"
    log "      将覆盖安装 GridYard ${VERSION_LABEL}。"
fi

log ""
log "[3/7] 正在创建安装目录..."
log "      ${INSTALL_DIR}"
mkdir -p "${INSTALL_DIR}"

log ""
log "[4/7] 正在下载 AppImage..."
log "      下载地址：${DOWNLOAD_URL}"
log "      文件大小约：${APPIMAGE_SIZE_HINT}"
log "      请保持网络连接稳定，下载过程中不要关闭终端。"
log "      临时文件：${TMP_FILE}"
download_appimage
log "      下载完成。"

log ""
log "[5/7] 正在安装 AppImage..."
log "      写入：${APPIMAGE_TARGET}"
install -m 0755 "${TMP_FILE}" "${APPIMAGE_TARGET}"
log "      创建链接：${APPIMAGE_LINK}"
ln -sfn "${APPIMAGE_TARGET}" "${APPIMAGE_LINK}"
printf '%s\n' "${VERSION}" > "${VERSION_FILE}"

log ""
log "[6/7] 正在安装图标和 desktop 文件..."
log "      图标：${ICON_TARGET}"
install -m 0644 "${ICON_SOURCE}" "${ICON_TARGET}"
log "      desktop：${DESKTOP_FILE}"
cat > "${DESKTOP_FILE}" <<EOF
[Desktop Entry]
Type=Application
Name=GridYard
GenericName=LAN P2P File Transfer
Comment=GridYard 局域网 P2P 文件传输、在线聊天与本地历史
Exec=${APPIMAGE_LINK}
Icon=${ICON_TARGET}
Categories=Network;FileTransfer;P2P;
Terminal=false
StartupWMClass=GridYard
Keywords=gridyard;p2p;lan;file;chat;
EOF
chmod 0644 "${DESKTOP_FILE}"

log ""
log "[7/7] 正在刷新桌面应用数据库..."
if command -v update-desktop-database >/dev/null 2>&1; then
    log "      update-desktop-database ${DESKTOP_DATABASE_DIR}"
    update-desktop-database "${DESKTOP_DATABASE_DIR}"
else
    log "      未找到 update-desktop-database，已跳过刷新。"
    log "      应用菜单通常会在重新登录或桌面环境刷新后自动识别。"
fi

cat <<EOF

GridYard ${VERSION_LABEL} 安装完成。

你现在可以：
1. 在应用菜单中搜索 GridYard 启动
2. 或直接运行：
   ${APPIMAGE_LINK}

脚本执行完毕，即将退出。
EOF

exit 0
