#!/bin/bash
# GridYard 本机回环测试脚本
# 启动两个实例：发送端（端口 35101）和接收端（端口 35100）

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build-ninja/client"
APP="$BUILD_DIR/appGridYard"

# 检查可执行文件是否存在
if [ ! -f "$APP" ]; then
    echo "错误：找不到可执行文件 $APP"
    echo "请先构建项目：cd src && cmake --build build-ninja -j"
    exit 1
fi

echo "=== GridYard 本机回环测试 ==="
echo ""
echo "启动接收端（端口 35100）..."
echo "启动发送端（端口 35101）..."
echo ""
echo "测试步骤："
echo "1. 在发送端窗口，拖拽文件到接收端的设备卡片"
echo "2. 在接收端窗口，点击'接受'按钮"
echo "3. 观察传输进度"
echo ""
echo "按 Ctrl+C 停止测试"
echo ""

# 后台启动接收端
"$APP" --port 35100 --name "接收端" &
RECEIVER_PID=$!

# 等待接收端启动
sleep 1

# 启动发送端
"$APP" --port 35101 --name "发送端" &
SENDER_PID=$!

# 等待用户中断
trap "kill $RECEIVER_PID $SENDER_PID 2>/dev/null; exit 0" INT TERM

# 等待进程结束
wait
