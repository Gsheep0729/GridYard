/**
* @file    net_reactor.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   单线程 poll 事件反应器实现
*
* 主循环：poll(eventfd + 监听 fd 集，5 秒超时兜底) → 先应用命令队列
* → 再派发就绪事件。命令优先于派发，配合代际校验，保证已注销的 fd
* 即使本轮 poll 携带就绪位也不会触发回调。回调契约：不得抛异常、
* 不得阻塞——异常被捕获吞掉以保住引擎线程存活。
*/

#include "core/net_reactor.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace gy::net {

namespace {

// poll 等待上限：保证循环永不永久卡死（周期性自检运行标志）
constexpr int kPollTimeoutMs = 5000;

// 把 poll 事件位归一化为 Event 掩码（错误与挂断分开归位）
uint32_t mapPollEvents(int revents)
{
    uint32_t events = 0;
    if (revents & POLLIN) {
        events |= static_cast<uint32_t>(Event::kReadable);
    }
    if (revents & POLLOUT) {
        events |= static_cast<uint32_t>(Event::kWritable);
    }
    if (revents & POLLERR) {
        events |= static_cast<uint32_t>(Event::kError);
    }
    if (revents & POLLHUP) {
        events |= static_cast<uint32_t>(Event::kHangup);
    }
    return events;
}

}  // namespace

// 构造函数：创建非阻塞 eventfd 唤醒句柄
NetReactor::NetReactor()
{
    _eventfd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

// 析构函数：确保线程退出并关闭唤醒句柄
NetReactor::~NetReactor()
{
    stop();
    if (_eventfd >= 0) {
        ::close(_eventfd);
        _eventfd = -1;
    }
}

// 启动自有 Reactor 线程
void NetReactor::start()
{
    if (_running.load() || _ownThread) {
        return;
    }
    _ownThread = true;
    _running.store(true);
    _thread = std::thread([this] { runLoop(); });
}

// 停止并回收线程
void NetReactor::stop()
{
    if (!_running.exchange(false)) {
        return;
    }
    wakeup();
    if (_ownThread && _thread.joinable()) {
        _thread.join();
    }
    _ownThread = false;
    // 丢弃未处理的命令，清空等待方
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        _pendingCmds.clear();
        _ackSerial++;
    }
    _ackCond.notify_all();
}

// 在当前线程运行主循环（独立服务进程模式）
void NetReactor::run()
{
    if (_running.exchange(true)) {
        return;  // 已在运行，直接返回
    }
    runLoop();
}

// 注册或更新 fd 监听
void NetReactor::watch(int fd, uint32_t events, uint64_t generation, EventCallback cb)
{
    if (fd < 0 || !cb) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        _pendingCmds.push_back(WatchCmd{fd, events, generation, std::move(cb), false, false});
    }
    wakeup();
}

// 取消 fd 监听（代际不匹配时静默忽略）
void NetReactor::unwatch(int fd, uint64_t generation)
{
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        _pendingCmds.push_back(WatchCmd{fd, 0, generation, nullptr, true, false});
    }
    wakeup();
}

// 取消监听并等待 Reactor 确认（close 前的安全摘除）
void NetReactor::unwatchSync(int fd, uint64_t generation)
{
    if (_running.load() && _ownThread && _thread.joinable()
        && _thread.get_id() == std::this_thread::get_id()) {
        return;  // Reactor 线程内调用：禁止自等待（防御，正常不应发生）
    }
    uint64_t targetSerial = 0;
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        targetSerial = _ackSerial + 1;
        _pendingCmds.push_back(WatchCmd{fd, 0, generation, nullptr, true, true});
    }
    wakeup();
    std::unique_lock<std::mutex> lock(_cmdMutex);
    _ackCond.wait(lock, [this, targetSerial] { return !_running.load() || _ackSerial >= targetSerial; });
}

// 写 eventfd 唤醒 Reactor 线程
void NetReactor::wakeup()
{
    if (_eventfd < 0) {
        return;
    }
    const uint64_t one = 1;
    const ssize_t written = ::write(_eventfd, &one, sizeof(one));
    (void)written;  // EAGAIN 表示已积压唤醒，无需处理
}

// 主循环
void NetReactor::runLoop()
{
    std::vector<struct pollfd> fds;
    while (_running.load()) {
        fds.clear();
        // 唤醒句柄恒在首位
        struct pollfd wake{};
        wake.fd = _eventfd;
        wake.events = POLLIN;
        fds.push_back(wake);

        for (const auto &entry : _watches) {
            struct pollfd item{};
            item.fd = entry.first;
            item.events = 0;
            if (entry.second.events & static_cast<uint32_t>(Event::kReadable)) {
                item.events |= POLLIN;
            }
            if (entry.second.events & static_cast<uint32_t>(Event::kWritable)) {
                item.events |= POLLOUT;
            }
            fds.push_back(item);
        }

        const int ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), kPollTimeoutMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;  // poll 本身故障，退出循环
        }

        if (fds[0].revents & POLLIN) {
            uint64_t value = 0;
            while (::read(_eventfd, &value, sizeof(value)) > 0) {
                // 排空唤醒计数
            }
        }

        // 命令优先于派发：本轮注销的 fd 即便带就绪位也不会回调
        applyPendingCmds();
        if (!_running.load()) {
            break;
        }
        dispatchEvents(fds);
    }

    // 退出前确认所有 pending 命令（含 unwatchSync 等待方）
    applyPendingCmds();
}

// 应用命令队列到监听表
void NetReactor::applyPendingCmds()
{
    std::vector<WatchCmd> cmds;
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        cmds.swap(_pendingCmds);
    }

    for (const WatchCmd &cmd : cmds) {
        const auto it = _watches.find(cmd.fd);
        if (cmd.isUnwatch) {
            // 代际不匹配的注销请求静默忽略（已被更新的监听接管）
            if (it != _watches.end() && it->second.generation == cmd.generation) {
                _watches.erase(it);
            }
        } else {
            FdEntry entry;
            entry.generation = cmd.generation;
            entry.events = cmd.events;
            entry.cb = cmd.cb;
            _watches[cmd.fd] = std::move(entry);
        }
    }

    // 唤醒所有等待确认的调用方
    if (!cmds.empty()) {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        _ackSerial++;
    }
    _ackCond.notify_all();
}

// 派发一轮就绪事件
void NetReactor::dispatchEvents(const std::vector<struct pollfd> &fds)
{
    // 跳过首位的 eventfd
    for (size_t i = 1; i < fds.size(); ++i) {
        if (fds[i].revents == 0) {
            continue;
        }
        const int fd = fds[i].fd;
        const auto it = _watches.find(fd);
        if (it == _watches.end()) {
            continue;  // 本轮已注销
        }

        uint32_t events = mapPollEvents(fds[i].revents);
        // 未请求读监听时只剥离挂断位：对齐旧双通知器"读通知器关闭期间
        // HUP 不派发"的语义（连接建立窗口因此存在）。错误位（POLLERR）
        // 不剥离——旧实现 POLLERR 同样激活写通知器，由写回调的 SO_ERROR
        // 检查兜底连接失败与写错误
        if (!(it->second.events & static_cast<uint32_t>(Event::kReadable))) {
            events &= ~static_cast<uint32_t>(Event::kHangup);
        }
        if (events == 0) {
            continue;
        }

        // 回调契约：不得抛异常；异常被吞以保证引擎线程存活
        try {
            it->second.cb(fd, events);
        } catch (...) {
            // 静默吞掉回调异常，保持 Reactor 存活（核心层无日志设施）
        }
    }
}

// 客户端进程共享的默认 Reactor 单例
NetReactor &sharedReactor()
{
    static NetReactor reactor;
    static bool started = false;
    if (!started) {
        started = true;
        reactor.start();
    }
    return reactor;
}

}  // namespace gy::net
