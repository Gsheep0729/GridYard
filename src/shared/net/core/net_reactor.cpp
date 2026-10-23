/**
* @file    net_reactor.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   单线程 poll 事件反应器实现
*
 * 主循环：poll(eventfd + 监听 fd 集，超时兜底) → 先应用命令队列
 * → 再派发就绪事件。命令优先于派发，配合代际校验，保证已注销的 fd
 * 即使本轮 poll 携带就绪位也不会触发回调。定时任务由 poll 超时驱动，
 * 每执行一个到期任务前重放一次待处理命令，保证任务回调中排入的
 * 取消请求立即生效。回调契约：不得抛异常、不得阻塞——异常被捕获
 * 吞掉以保住引擎线程存活。
 */

#include "core/net_reactor.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

namespace gy::net {

namespace {

// poll 等待上限：保证循环永不永久卡死（周期性自检运行标志）
constexpr int kPollTimeoutMs = 5000;

// 线程标识折叠为 64 位整数（原子存取；碰撞概率可忽略，
// 且碰撞后果仅是把外部调用误判为线程内调用，走直接执行路径）
uint64_t threadIdValue(std::thread::id id)
{
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(id));
}

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
    // 丢弃未处理的命令与定时任务，清空等待方
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        _pendingCmds.clear();
        _pendingTimers.clear();
        _ackSerial++;
        _taskAckSerial++;
    }
    _timers.clear();
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
    if (!_running.load() || onReactorThread()) {
        return;  // 未运行无需等待；Reactor 线程内调用禁止自等待
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

// 取消监听的即时形态（Reactor 线程内直接摘除）
void NetReactor::unwatchNow(int fd, uint64_t generation)
{
    if (onReactorThread()) {
        const auto it = _watches.find(fd);
        if (it != _watches.end() && it->second.generation == generation) {
            _watches.erase(it);
        }
        return;
    }
    unwatchSync(fd, generation);
}

// 延迟执行回调（到期后在 Reactor 线程触发）
NetReactor::TimerId NetReactor::scheduleAfter(int delayMs, std::function<void()> callback)
{
    if (!callback) {
        return 0;
    }
    const TimerId id = _nextTimerId.fetch_add(1);
    if (onReactorThread()) {
        // 线程内调用：直接生效，无需唤醒
        TimerEntry entry;
        entry.deadline = std::chrono::steady_clock::now()
                         + std::chrono::milliseconds(std::max(0, delayMs));
        entry.callback = std::move(callback);
        _timers[id] = std::move(entry);
        return id;
    }
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        TimerCmd cmd;
        cmd.id = id;
        cmd.delayMs = delayMs;
        cmd.task = std::move(callback);
        _pendingTimers.push_back(std::move(cmd));
    }
    wakeup();
    return id;
}

// 取消定时任务（尽力语义）
void NetReactor::cancelTimer(TimerId timerId)
{
    if (timerId == 0) {
        return;
    }
    if (onReactorThread()) {
        // 线程内调用：立即摘除，避免同批到期任务漏取消
        _timers.erase(timerId);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        TimerCmd cmd;
        cmd.id = timerId;
        cmd.isCancel = true;
        _pendingTimers.push_back(std::move(cmd));
    }
    wakeup();
}

// 在 Reactor 线程同步执行任务
void NetReactor::dispatchSync(std::function<void()> task)
{
    if (!task) {
        return;
    }
    if (!_running.load() || onReactorThread()) {
        task();  // 泵未运行或线程内调用：直接执行
        return;
    }
    uint64_t targetSerial = 0;
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        targetSerial = _taskAckSerial + 1;
        TimerCmd cmd;
        cmd.delayMs = 0;
        cmd.task = std::move(task);
        cmd.needAck = true;
        _pendingTimers.push_back(std::move(cmd));
    }
    wakeup();
    std::unique_lock<std::mutex> lock(_cmdMutex);
    _ackCond.wait(lock, [this, targetSerial] { return !_running.load() || _taskAckSerial >= targetSerial; });
}

// 当前调用是否发生在 Reactor 线程
bool NetReactor::onReactorThread() const
{
    const uint64_t loopId = _loopThreadId.load(std::memory_order_acquire);
    return loopId != 0 && loopId == threadIdValue(std::this_thread::get_id());
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
    _loopThreadId.store(threadIdValue(std::this_thread::get_id()), std::memory_order_release);
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

        const int ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), pollTimeoutMs());
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
        applyPendingTimers();
        if (!_running.load()) {
            break;
        }
        runDueTimers();
        if (!_running.load()) {
            break;
        }
        dispatchEvents(fds);
    }

    // 退出前确认所有 pending 命令（含 unwatchSync/dispatchSync 等待方），
    // 清空尚未到期的定时任务
    applyPendingCmds();
    applyPendingTimers();
    _timers.clear();
    _loopThreadId.store(0, std::memory_order_release);
}

// 计算本轮 poll 等待上限
int NetReactor::pollTimeoutMs() const
{
    if (_timers.empty()) {
        return kPollTimeoutMs;
    }
    auto earliest = _timers.begin()->second.deadline;
    for (const auto &entry : _timers) {
        earliest = std::min(earliest, entry.second.deadline);
    }
    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
        earliest - std::chrono::steady_clock::now());
    const auto clamped = std::clamp<decltype(delta)::rep>(delta.count(), 0, kPollTimeoutMs);
    return static_cast<int>(clamped);
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

// 把定时/任务命令应用到任务表
void NetReactor::applyPendingTimers()
{
    std::vector<TimerCmd> cmds;
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        cmds.swap(_pendingTimers);
    }
    if (cmds.empty()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    for (TimerCmd &cmd : cmds) {
        if (cmd.isCancel) {
            // 句柄已不存在（已执行或已取消）时静默忽略
            _timers.erase(cmd.id);
        } else if (cmd.delayMs <= 0 && cmd.needAck) {
            // 同步任务：立即执行，批处理完成后统一应答等待方
            try {
                cmd.task();
            } catch (...) {
                // 静默吞掉任务异常，保持 Reactor 存活
            }
        } else {
            TimerEntry entry;
            entry.deadline = now + std::chrono::milliseconds(std::max(0, cmd.delayMs));
            entry.callback = std::move(cmd.task);
            _timers[cmd.id] = std::move(entry);
        }
    }

    // 应答同步等待方（任务已在本批内执行完毕）
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        _taskAckSerial++;
    }
    _ackCond.notify_all();
}

// 逐个执行已到期的定时任务
void NetReactor::runDueTimers()
{
    while (_running.load()) {
        // 重放待处理命令：前一个任务回调排入的取消/新增立即生效
        applyPendingTimers();
        const auto now = std::chrono::steady_clock::now();

        // 挑选到期且最早的一个任务
        bool found = false;
        TimerId dueId = 0;
        std::chrono::steady_clock::time_point earliest{};
        for (const auto &entry : _timers) {
            if (entry.second.deadline <= now && (!found || entry.second.deadline < earliest)) {
                found = true;
                earliest = entry.second.deadline;
                dueId = entry.first;
            }
        }
        if (!found) {
            return;
        }

        // 先摘除再执行：回调内的自取消与重复触发均不可能
        auto node = _timers.extract(dueId);
        std::function<void()> callback = std::move(node.mapped().callback);
        try {
            callback();
        } catch (...) {
            // 静默吞掉回调异常，保持 Reactor 存活
        }
    }
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
