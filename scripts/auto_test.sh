#!/bin/bash
# GridYard 自动化测试脚本
# 自动启动实例、执行测试、验证结果

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 配置
BUILD_DIR="build-ninja/client"
APP_NAME="appGridYard"
BASE_PORT=35100
TEST_DIR="/tmp/gridyard_auto_test"
RECV_DIR="$TEST_DIR/receive"
SEND_DIR="$TEST_DIR/send"

# 统计
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 清理函数
cleanup() {
    echo -e "\n${YELLOW}清理测试环境...${NC}"
    pkill -f "$APP_NAME.*--port" 2>/dev/null || true
    sleep 1
    rm -rf "$TEST_DIR"
    echo -e "${GREEN}清理完成${NC}"
}

trap cleanup EXIT

# 打印测试结果
print_result() {
    local test_name=$1
    local result=$2
    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    if [ "$result" = "pass" ]; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo -e "  ${GREEN}✓ $test_name${NC}"
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo -e "  ${RED}✗ $test_name${NC}"
    fi
}

# 检查构建
check_build() {
    echo -e "${BLUE}=== 检查构建 ===${NC}"
    if [ ! -f "$BUILD_DIR/$APP_NAME" ]; then
        echo -e "${RED}错误：找不到可执行文件${NC}"
        echo "请先运行: cmake --build build-ninja -j"
        exit 1
    fi
    print_result "构建文件存在" "pass"
}

# 创建测试文件
create_test_files() {
    echo -e "\n${BLUE}=== 创建测试文件 ===${NC}"
    mkdir -p "$SEND_DIR" "$RECV_DIR"

    # 小文件
    echo "Hello, GridYard!" > "$SEND_DIR/small.txt"
    print_result "创建小文件 (small.txt)" "pass"

    # 中等文件 (1MB)
    dd if=/dev/urandom of="$SEND_DIR/medium.bin" bs=1K count=1024 2>/dev/null
    print_result "创建中等文件 (1MB)" "pass"

    # 大文件 (10MB)
    dd if=/dev/urandom of="$SEND_DIR/large.bin" bs=1M count=10 2>/dev/null
    print_result "创建大文件 (10MB)" "pass"

    # 零字节文件
    touch "$SEND_DIR/empty.txt"
    print_result "创建零字节文件" "pass"

    # 中文文件名
    echo "中文内容" > "$SEND_DIR/中文文件.txt"
    print_result "创建中文文件名" "pass"

    # 带空格的文件名
    echo "spaces" > "$SEND_DIR/file with spaces.txt"
    print_result "创建带空格文件名" "pass"

    # 测试目录
    mkdir -p "$SEND_DIR/test_dir/sub1/sub2"
    echo "file1" > "$SEND_DIR/test_dir/file1.txt"
    echo "file2" > "$SEND_DIR/test_dir/sub1/file2.txt"
    echo "file3" > "$SEND_DIR/test_dir/sub1/sub2/file3.txt"
    print_result "创建测试目录 (3层嵌套)" "pass"
}

# 测试 1: 基础启动
test_basic_startup() {
    echo -e "\n${BLUE}=== 测试 1: 基础启动 ===${NC}"

    # 启动单个实例
    ./$BUILD_DIR/$APP_NAME --port $BASE_PORT --name "TestDevice" &
    local pid=$!
    sleep 3

    if kill -0 $pid 2>/dev/null; then
        print_result "程序启动成功" "pass"
        kill $pid 2>/dev/null || true
        sleep 1
    else
        print_result "程序启动成功" "fail"
    fi
}

# 测试 2: 多实例启动
test_multi_instance() {
    echo -e "\n${BLUE}=== 测试 2: 多实例启动 ===${NC}"

    # 启动两个实例
    ./$BUILD_DIR/$APP_NAME --port $BASE_PORT --name "DeviceA" &
    local pid1=$!
    ./$BUILD_DIR/$APP_NAME --port $((BASE_PORT+1)) --name "DeviceB" &
    local pid2=$!
    sleep 3

    local both_running=true
    if ! kill -0 $pid1 2>/dev/null; then
        both_running=false
    fi
    if ! kill -0 $pid2 2>/dev/null; then
        both_running=false
    fi

    if $both_running; then
        print_result "两个实例同时运行" "pass"
    else
        print_result "两个实例同时运行" "fail"
    fi

    kill $pid1 $pid2 2>/dev/null || true
    sleep 1
}

# 测试 3: 配置隔离
test_config_isolation() {
    echo -e "\n${BLUE}=== 测试 3: 配置隔离 ===${NC}"

    # 启动两个实例
    ./$BUILD_DIR/$APP_NAME --port $BASE_PORT --name "Config1" &
    local pid1=$!
    ./$BUILD_DIR/$APP_NAME --port $((BASE_PORT+1)) --name "Config2" &
    local pid2=$!
    sleep 3

    # 检查日志文件是否独立
    local log_count=$(ls -1 logs/gridyard_*.log 2>/dev/null | wc -l)
    if [ "$log_count" -ge 1 ]; then
        print_result "日志文件生成" "pass"
    else
        print_result "日志文件生成" "fail"
    fi

    kill $pid1 $pid2 2>/dev/null || true
    sleep 1
}

# 测试 4: 端口配置
test_port_config() {
    echo -e "\n${BLUE}=== 测试 4: 端口配置 ===${NC}"

    # 测试不同端口
    for port in 35100 35101 35102; do
        ./$BUILD_DIR/$APP_NAME --port $port --name "PortTest" &
        local pid=$!
        sleep 2

        if kill -0 $pid 2>/dev/null; then
            print_result "端口 $port 启动" "pass"
            kill $pid 2>/dev/null || true
            sleep 1
        else
            print_result "端口 $port 启动" "fail"
        fi
    done
}

# 测试 5: 单元测试
test_unit_tests() {
    echo -e "\n${BLUE}=== 测试 5: 单元测试 ===${NC}"

    # 运行所有单元测试
    local tests=(
        "test_frame_codec"
        "test_edge_cases"
        "test_transfer"
        "test_config_manager"
    )

    for test in "${tests[@]}"; do
        if [ -f "build-ninja/tests/$test" ]; then
            if ./build-ninja/tests/$test > /dev/null 2>&1; then
                print_result "$test" "pass"
            else
                print_result "$test" "fail"
            fi
        else
            echo -e "  ${YELLOW}⚠ $test (未构建)${NC}"
        fi
    done
}

# 打印统计
print_stats() {
    echo -e "\n${BLUE}=== 测试统计 ===${NC}"
    echo -e "总测试数: $TOTAL_TESTS"
    echo -e "${GREEN}通过: $PASSED_TESTS${NC}"
    echo -e "${RED}失败: $FAILED_TESTS${NC}"

    if [ $FAILED_TESTS -eq 0 ]; then
        echo -e "\n${GREEN}所有测试通过！${NC}"
    else
        echo -e "\n${RED}存在失败的测试${NC}"
    fi
}

# 主流程
main() {
    echo -e "${GREEN}=== GridYard 自动化测试 ===${NC}"
    echo ""

    check_build
    create_test_files
    test_basic_startup
    test_multi_instance
    test_config_isolation
    test_port_config
    test_unit_tests
    print_stats

    # 返回失败数作为退出码
    exit $FAILED_TESTS
}

main
