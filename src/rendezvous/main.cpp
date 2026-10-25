/**
* @file    main.cpp
* @version 8.0.0
* @date    2026-10-24
* @author  GridYard Team
* @brief   协调节点和中继服务入口
*
* 支持两种模式：
* - 协调节点模式（默认）：同端口提供设备注册、候选端点拉取、中继邀请信令
*   和流式中继转发（按握手类型分流）
* - 中继模式：仅提供流式中继转发服务
*
* 纯 C++ 实现：手写 argv 解析，事件泵 run() 在主线程驱动，
* signalfd 监听 SIGINT/SIGTERM 优雅退出，日志直写标准流。
*/

#include "rendezvous_log.h"
#include "relay_server.h"
#include "rendezvous_server.h"

#include "core/net_reactor.h"

#include <sys/signalfd.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// 服务版本（与 CMake 工程 VERSION 一致）
constexpr char kServiceVersion[] = "8.0.0";

// 打印用法说明
void printUsage(const char *program)
{
    std::fprintf(stdout,
                 "用法: %s [选项]\n"
                 "GridYard 协调节点/中继服务\n"
                 "选项:\n"
                 "  -m, --mode <mode>    运行模式：rendezvous=协调节点，relay=中继（默认 rendezvous）\n"
                 "  -p, --port <port>    监听端口（默认 45679）\n"
                 "  -t, --token <token>  访问口令\n"
                 "  -h, --help           显示本帮助\n"
                 "  -v, --version        显示版本号\n",
                 program);
}

}  // namespace

int main(int argc, char *argv[])
{
    // 对端关闭后继续写入时忽略 SIGPIPE，套接字层另有 MSG_NOSIGNAL 逐次兜底
    std::signal(SIGPIPE, SIG_IGN);

    std::string mode = "rendezvous";
    std::string token;
    int portValue = 45679;
    bool showHelp = false;
    bool showVersion = false;
    bool badOption = false;

    // 手写 argv 解析：--key value 与 -k value 两种形态
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option != "-m" && option != "--mode" && option != "-p" && option != "--port"
            && option != "-t" && option != "--token") {
            if (option == "-h" || option == "--help") {
                showHelp = true;
                continue;
            }
            if (option == "-v" || option == "--version") {
                showVersion = true;
                continue;
            }
            gy::rendezvous::logWarn(std::string("[Main] 未知选项: ") + option);
            badOption = true;
            continue;
        }
        if (i + 1 >= argc) {
            gy::rendezvous::logWarn("[Main] 选项缺少参数: " + option);
            badOption = true;
            continue;
        }
        const std::string value = argv[++i];
        if (option == "-m" || option == "--mode") {
            mode = value;
        } else if (option == "-p" || option == "--port") {
            portValue = std::atoi(value.c_str());
        } else {
            token = value;
        }
    }

    if (showHelp) {
        printUsage(argv[0]);
        return 0;
    }
    if (showVersion) {
        std::fprintf(stdout, "gridyard-rendezvous %s\n", kServiceVersion);
        return 0;
    }
    if (badOption) {
        printUsage(argv[0]);
        return 1;
    }

    // 端口必须落在 1~65535，否则 OS 会分配随机端口，
    // 用户以为服务在指定端口实则监听在别处
    if (portValue < 1 || portValue > 65535) {
        gy::rendezvous::logWarn("[Main] 无效的端口参数: " + std::to_string(portValue));
        return 1;
    }
    const uint16_t port = static_cast<uint16_t>(portValue);

    // 只接受 rendezvous / relay 两种模式，拼写错误不再静默回退
    if (mode != "rendezvous" && mode != "relay") {
        gy::rendezvous::logWarn("[Main] 无效的运行模式: " + mode + "（可选 rendezvous 或 relay）");
        return 1;
    }

    // 阻塞退出信号并转为 signalfd，由事件泵监听后优雅退出
    sigset_t exitSignals;
    sigemptyset(&exitSignals);
    sigaddset(&exitSignals, SIGINT);
    sigaddset(&exitSignals, SIGTERM);
    ::sigprocmask(SIG_BLOCK, &exitSignals, nullptr);
    const int signalFd = ::signalfd(-1, &exitSignals, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signalFd < 0) {
        gy::rendezvous::logWarn(std::string("[Main] signalfd 创建失败: ") + std::strerror(errno));
        return 1;
    }

    gy::net::NetReactor reactor;

    if (mode == "relay") {
        RelayServer relay{port, token, &reactor};
        if (!relay.start()) {
            gy::rendezvous::logWarn("[Main] 中继服务启动失败: " + relay.errorString());
            ::close(signalFd);
            return 1;
        }
        gy::rendezvous::logInfo("[Main] GridYard 中继服务已启动");

        reactor.watch(signalFd, static_cast<uint32_t>(gy::net::Event::kReadable), 1,
                      [&reactor](int, uint32_t) {
                          gy::rendezvous::logInfo("[Main] 收到退出信号");
                          reactor.stop();
                      });
        reactor.run();
    } else {
        RendezvousServer rendezvous{port, token, -1, &reactor};
        // 协调节点模式同端口内置中继：分流出的 relay_create/relay_join 连接
        // 移交给该实例（不独立监听，仅承接），校园网只需开放一个端口
        RelayServer relay{port, token, &reactor};
        rendezvous.setRelayPipeHandler(
            [&relay](gy::net::TcpSocketCore *socket, const nlohmann::json &hello,
                     const std::vector<uint8_t> &pendingData) {
                relay.adoptConnection(socket, hello, pendingData);
            });

        if (!rendezvous.start()) {
            gy::rendezvous::logWarn("[Main] 协调节点服务启动失败: " + rendezvous.errorString());
            ::close(signalFd);
            return 1;
        }
        gy::rendezvous::logInfo("[Main] GridYard 协调节点服务已启动（同端口内置中继）");

        reactor.watch(signalFd, static_cast<uint32_t>(gy::net::Event::kReadable), 1,
                      [&reactor](int, uint32_t) {
                          gy::rendezvous::logInfo("[Main] 收到退出信号");
                          reactor.stop();
                      });
        reactor.run();
    }

    ::close(signalFd);
    gy::rendezvous::logInfo("[Main] 服务已退出");
    return 0;
}
