#!/bin/bash

# GridYard AppImage 测试脚本
#
# 用法：
#   ./scripts/test_appimage.sh                  # 默认测试 release/v4.16.0/
#   ./scripts/test_appimage.sh v4.16.0          # 指定版本
#   ./scripts/test_appimage.sh v4.15.2          # 测试历史版本
#   ./scripts/test_appimage.sh /path/to/X.AppImage  # 直接传完整路径

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEFAULT_VERSION="v4.16.0"

# 解析参数：第 1 个参数可以是版本号（vX.Y.Z）或完整 AppImage 路径
ARG="${1:-$DEFAULT_VERSION}"

if [[ "$ARG" == /* ]]; then
    # 绝对路径：直接使用
    APPIMAGE="$ARG"
elif [[ "$ARG" == *.AppImage ]]; then
    # 相对路径：基于仓库根解析
    APPIMAGE="$PROJECT_ROOT/$ARG"
else
    # 版本号：拼成 release/<version>/GridYard-<version>-x86_64.AppImage
    APPIMAGE="$PROJECT_ROOT/release/$ARG/GridYard-$ARG-x86_64.AppImage"
fi

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查 AppImage 是否存在
check_appimage() {
    if [ ! -f "$APPIMAGE" ]; then
        log_error "AppImage 不存在: $APPIMAGE"
        log_error "可执行：./scripts/test_appimage.sh [版本号 | AppImage 路径]"
        log_error "例如：./scripts/test_appimage.sh $DEFAULT_VERSION"
        exit 1
    fi
    log_info "AppImage 文件存在: $(ls -lh "$APPIMAGE" | awk '{print $5}')"
    log_info "目标文件：$APPIMAGE"
}

# 测试 1：帮助信息
test_help() {
    log_info "测试 1：帮助信息"
    if "$APPIMAGE" --help > /dev/null 2>&1; then
        log_info "  通过：--help 正常工作"
    else
        log_error "  失败：--help 无法运行"
        return 1
    fi
}

# 测试 2：版本信息
test_version() {
    log_info "测试 2：版本信息"
    VERSION=$("$APPIMAGE" --version 2>&1 | head -1)
    if [ -n "$VERSION" ]; then
        log_info "  通过：版本信息 = $VERSION"
    else
        log_error "  失败：无法获取版本信息"
        return 1
    fi
}

# 测试 3：启动并检查进程
test_start() {
    log_info "测试 3：启动测试"

    # 启动 AppImage
    "$APPIMAGE" --port 35200 --name "AppImage测试" &
    PID=$!

    # 等待启动
    sleep 3

    # 检查进程是否存在
    if ps -p $PID > /dev/null 2>&1; then
        log_info "  通过：进程启动成功 (PID: $PID)"

        # 停止进程
        kill $PID 2>/dev/null || true
        wait $PID 2>/dev/null || true
        log_info "  进程已停止"
    else
        log_error "  失败：进程未能启动或立即退出"
        return 1
    fi
}

# 测试 4：检查日志输出
test_log() {
    log_info "测试 4：日志测试"

    # 启动 AppImage 并捕获输出
    "$APPIMAGE" --port 35201 --name "日志测试" > /tmp/appimage_test.log 2>&1 &
    PID=$!

    sleep 3

    # 检查日志
    if [ -f /tmp/appimage_test.log ]; then
        LOG_CONTENT=$(cat /tmp/appimage_test.log)
        if echo "$LOG_CONTENT" | grep -q "ConfigManager"; then
            log_info "  通过：日志正常输出"
        else
            log_warn "  警告：日志内容可能不完整"
        fi
    fi

    # 停止进程
    kill $PID 2>/dev/null || true
    wait $PID 2>/dev/null || true
}

# 测试 5：检查依赖库
test_dependencies() {
    log_info "测试 5：依赖库检查"

    # 检查 AppImage 内的依赖
    if command -v ldd > /dev/null 2>&1; then
        # 挂载 AppImage
        "$APPIMAGE" --appimage-extract > /dev/null 2>&1 || true

        # 解压目录位置：跟随调用进程的 CWD（仓库根）
        SQUASHFS_DIR="$PROJECT_ROOT/squashfs-root"

        if [ -d "$SQUASHFS_DIR" ]; then
            # 检查 Qt6 库
            QT6_LIBS=$(find "$SQUASHFS_DIR" -name "libQt6*.so*" | wc -l)
            if [ "$QT6_LIBS" -gt 0 ]; then
                log_info "  通过：找到 $QT6_LIBS 个 Qt6 库"
            else
                log_error "  失败：未找到 Qt6 库"
            fi

            # 检查 Qt6 插件
            QT6_PLUGINS=$(find "$SQUASHFS_DIR" -path "*/qt6/plugins/*" -name "*.so" | wc -l)
            if [ "$QT6_PLUGINS" -gt 0 ]; then
                log_info "  通过：找到 $QT6_PLUGINS 个 Qt6 插件"
            else
                log_warn "  警告：未找到 Qt6 插件"
            fi

            # 检查 QML 模块
            QML_MODULES=$(find "$SQUASHFS_DIR" -path "*/qt6/qml/*" -name "*.so" | wc -l)
            if [ "$QML_MODULES" -gt 0 ]; then
                log_info "  通过：找到 $QML_MODULES 个 QML 模块"
            else
                log_warn "  警告：未找到 QML 模块"
            fi

            # 清理
            rm -rf "$SQUASHFS_DIR"
        else
            log_error "  失败：无法解压 AppImage"
        fi
    else
        log_warn "  跳过：ldd 命令不可用"
    fi
}

# 主函数
main() {
    log_info "=== GridYard AppImage 测试 ==="
    echo ""

    check_appimage
    echo ""

    test_help
    echo ""

    test_version
    echo ""

    test_start
    echo ""

    test_log
    echo ""

    test_dependencies
    echo ""

    log_info "=== 测试完成 ==="
}

main "$@"
