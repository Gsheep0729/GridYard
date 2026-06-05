#!/bin/bash
# GridYard 单机多实例测试脚本
# 在同一台机器上启动多个实例，模拟多设备传输场景

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 配置
BUILD_DIR="build/client"
APP_NAME="appGridYard"
BASE_PORT=35100
CONFIG_DIR="/tmp/gridyard_test"

# 清理函数
cleanup() {
    echo -e "${YELLOW}清理测试环境...${NC}"
    # 杀死所有测试进程
    pkill -f "$APP_NAME.*--port" 2>/dev/null || true
    # 删除临时配置
    rm -rf "$CONFIG_DIR"
    echo -e "${GREEN}清理完成${NC}"
}

# 捕获退出信号
trap cleanup EXIT

# 检查构建
if [ ! -f "$BUILD_DIR/$APP_NAME" ]; then
    echo -e "${RED}错误：找不到可执行文件 $BUILD_DIR/$APP_NAME${NC}"
    echo "请先运行: cmake --build build -j"
    exit 1
fi

# 创建配置目录
mkdir -p "$CONFIG_DIR"

echo -e "${GREEN}=== GridYard 单机多实例测试 ===${NC}"
echo ""

# 测试 1: 启动两个实例
echo -e "${YELLOW}测试 1: 启动两个实例（接收端 + 发送端）${NC}"

# 启动接收端（端口 35100）
echo "启动接收端 (端口 $BASE_PORT)..."
./$BUILD_DIR/$APP_NAME --port $BASE_PORT --name "接收端" &
RECV_PID=$!
sleep 2

# 启动发送端（端口 35101）
echo "启动发送端 (端口 $((BASE_PORT+1)))..."
./$BUILD_DIR/$APP_NAME --port $((BASE_PORT+1)) --name "发送端" &
SEND_PID=$!
sleep 2

# 检查进程是否存活
if kill -0 $RECV_PID 2>/dev/null && kill -0 $SEND_PID 2>/dev/null; then
    echo -e "${GREEN}✓ 两个实例启动成功${NC}"
else
    echo -e "${RED}✗ 实例启动失败${NC}"
    exit 1
fi

# 等待用户操作
echo ""
echo -e "${YELLOW}请在两个窗口中测试以下功能：${NC}"
echo "1. 设备发现 - 检查是否能看到对方设备"
echo "2. 文件传输 - 拖拽文件到对方设备卡片"
echo "3. 传输进度 - 观察传输面板进度更新"
echo "4. 取消传输 - 测试取消功能"
echo ""
echo -e "${YELLOW}测试完成后按 Enter 继续...${NC}"
read

# 测试 2: 启动三个实例
echo -e "${YELLOW}测试 2: 启动三个实例（测试多设备发现）${NC}"

# 杀死之前的进程
kill $RECV_PID $SEND_PID 2>/dev/null || true
sleep 1

# 启动三个实例
echo "启动设备 A (端口 $BASE_PORT)..."
./$BUILD_DIR/$APP_NAME --port $BASE_PORT --name "设备A" &
PID_A=$!

echo "启动设备 B (端口 $((BASE_PORT+1)))..."
./$BUILD_DIR/$APP_NAME --port $((BASE_PORT+1)) --name "设备B" &
PID_B=$!

echo "启动设备 C (端口 $((BASE_PORT+2)))..."
./$BUILD_DIR/$APP_NAME --port $((BASE_PORT+2)) --name "设备C" &
PID_C=$!

sleep 3

# 检查进程
if kill -0 $PID_A 2>/dev/null && kill -0 $PID_B 2>/dev/null && kill -0 $PID_C 2>/dev/null; then
    echo -e "${GREEN}✓ 三个实例启动成功${NC}"
else
    echo -e "${RED}✗ 实例启动失败${NC}"
    exit 1
fi

echo ""
echo -e "${YELLOW}请测试：${NC}"
echo "1. 三个设备是否都能互相发现"
echo "2. A 向 B 发送文件"
echo "3. B 向 C 发送文件"
echo "4. C 向 A 发送文件"
echo ""
echo -e "${YELLOW}测试完成后按 Enter 继续...${NC}"
read

# 测试 3: 文件传输验证
echo -e "${YELLOW}测试 3: 创建测试文件并验证传输${NC}"

# 创建测试文件
TEST_FILE="$CONFIG_DIR/test_file.txt"
echo "Hello, GridYard! 这是测试文件内容。" > "$TEST_FILE"
echo "创建测试文件: $TEST_FILE"

# 创建测试目录
TEST_DIR="$CONFIG_DIR/test_dir"
mkdir -p "$TEST_DIR"
echo "文件1内容" > "$TEST_DIR/file1.txt"
echo "文件2内容" > "$TEST_DIR/file2.txt"
echo "文件3内容" > "$TEST_DIR/file3.txt"
echo "创建测试目录: $TEST_DIR (包含3个文件)"

echo ""
echo -e "${YELLOW}请使用上述文件/目录测试传输功能${NC}"
echo -e "${YELLOW}测试完成后按 Enter 退出...${NC}"
read

echo -e "${GREEN}=== 测试完成 ===${NC}"
