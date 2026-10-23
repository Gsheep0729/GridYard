/**
* @file    net_reactor.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   单线程 poll 事件反应器——纯 C++23，不依赖任何 Qt 类型
*
 * 仅用于 Linux/POSIX 平台（poll 与 eventfd 均为平台能力）。
 * 所有回调在 Reactor 线程中同步触发，禁止在回调内阻塞。
 * 代际计数（generation）由调用方维护：同一 fd 号被新连接复用时，
 * 旧监听的陈旧事件按代际不匹配被静默丢弃，杜绝误伤新连接。
 * 定时任务（scheduleAfter）由 poll 超时驱动：存在未到期任务时
 * 等待上限自动收敛到最近到期时刻，任务到期后在 Reactor 线程执行。
 */

#pragma once

#include <poll.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gy::net {

// 事件掩码（poll 事件位的归一化表示）
enum class Event : uint32_t {
    kReadable = 0x01,  // 可读（POLLIN）
    kWritable = 0x02,  // 可写（POLLOUT）
    kError    = 0x04,  // 套接字错误（POLLERR，如 RST/ECONNRESET）
    kHangup   = 0x08,  // 对端挂断（POLLHUP）
};

// 事件处理回调：参数为 fd 与触发的 Event 掩码组合
using EventCallback = std::function<void(int fd, uint32_t events)>;

class NetReactor {
public:
    NetReactor();
    ~NetReactor();

    // 含线程与 fd，拷贝移动语义均不安全
    NetReactor(const NetReactor &)            = delete;
    NetReactor &operator=(const NetReactor &) = delete;

    // 启动 Reactor 线程（客户端进程模式，仅调用一次）
    void start();
    // 停止 Reactor 线程并等待退出（自有线程模式下有效）
    void stop();
    // 在当前线程运行主循环（独立服务进程模式，main 直接调用阻塞执行）
    void run();

    // 在任意线程调用：注册或更新 fd 的事件监听（events 为 Event 掩码组合）
    // generation：调用方维护的代际计数；同一 fd 再次 watch 即更新条目
    void watch(int fd, uint32_t events, uint64_t generation, EventCallback cb);
    // 在任意线程调用：取消 fd 的监听（generation 不匹配时静默忽略）
    void unwatch(int fd, uint64_t generation);
    // 取消监听并等待 Reactor 线程确认生效——用于 close(fd) 前的强制摘除，
    // 消除 fd 复用与对象析构窗口内的陈旧派发。禁止在 Reactor 线程内调用。
    void unwatchSync(int fd, uint64_t generation);
    // 取消监听的即时形态：Reactor 线程内调用时直接从监听表摘除，
    // 其余线程退化为 unwatchSync。同样用于 close(fd) 前的安全摘除。
    void unwatchNow(int fd, uint64_t generation);

    // 定时任务句柄（单调递增，0 表示无效）
    using TimerId = uint64_t;

    // 在 Reactor 线程延迟 delayMs 毫秒执行回调（任意线程可调用；
    // 返回句柄用于取消）。回调内禁止阻塞，禁止再同步等待本泵。
    TimerId scheduleAfter(int delayMs, std::function<void()> callback);
    // 取消定时任务（尽力语义：已进入执行的回调无法打断）。
    // Reactor 线程内调用立即生效；句柄为 0 时为空操作。
    void cancelTimer(TimerId timerId);
    // 在 Reactor 线程同步执行任务并返回：外部线程投递并等待完成，
    // Reactor 线程内或泵未运行时直接执行。用于跨线程安全触碰
    // 泵归属对象；禁止与 stop 并发调用，禁止任务内自等待。
    void dispatchSync(std::function<void()> task);

    // 事件泵是否在运行（start 后台线程或 run 前台循环）
    bool isRunning() const { return _running.load(); }
    // 当前调用是否发生在 Reactor 线程（start 的泵线程或 run 的调用线程）
    bool onReactorThread() const;

    // 唤醒 Reactor 线程（watch/unwatch 内部已自动调用）
    void wakeup();

private:
    // 主循环（start 的后台线程与 run 的调用线程共用）
    void runLoop();
    // 把命令队列应用到监听表（仅 Reactor 线程调用）
    void applyPendingCmds();
    // 把定时/任务命令应用到任务表（仅 Reactor 线程调用）
    void applyPendingTimers();
    // 逐个执行已到期的定时任务（仅 Reactor 线程调用）
    void runDueTimers();
    // 计算本轮 poll 等待上限：有定时任务时收敛到最近到期时刻
    int pollTimeoutMs() const;
    // 派发一轮就绪事件（仅 Reactor 线程调用）
    void dispatchEvents(const std::vector<struct pollfd> &fds);

    // 跨线程投递的监听命令
    struct WatchCmd {
        int           fd;
        uint32_t      events;
        uint64_t      generation;
        EventCallback cb;
        bool          isUnwatch = false;  // true 表示取消监听
        bool          needAck   = false;  // true 表示完成时通知等待方
    };

    // 监听表条目（仅 Reactor 线程访问，无锁）
    struct FdEntry {
        uint64_t      generation;  // 代际计数，防 fd 复用后陈旧事件
        uint32_t      events;      // 当前监听的 Event 掩码
        EventCallback cb;          // 事件回调
    };

    // 跨线程投递的定时/任务命令（与监听命令共用一把锁）
    struct TimerCmd {
        uint64_t              id = 0;      // 目标定时任务句柄
        int                   delayMs = 0; // 延迟毫秒数（0 表示尽快）
        std::function<void()> task;        // 到期回调
        bool                  isCancel = false;  // true 表示取消
        bool                  needAck = false;   // true 表示同步任务，立即执行并应答
    };

    // 定时任务条目（仅 Reactor 线程访问，无锁）
    struct TimerEntry {
        std::chrono::steady_clock::time_point deadline;  // 到期时刻
        std::function<void()>                 callback;  // 到期回调
    };

    int                        _eventfd = -1;   // eventfd 唤醒句柄
    std::thread                _thread;         // Reactor 线程（start 模式）
    std::atomic<bool>          _running{false};  // 主循环运行标志
    bool                       _ownThread = false;  // start 模式标志（stop 需 join）
    std::atomic<uint64_t>      _loopThreadId{0};  // Reactor 线程标识（thread::id 哈希，0=未运行）

    std::mutex                 _cmdMutex;       // 保护命令队列
    std::vector<WatchCmd>      _pendingCmds;    // 跨线程投递的监听命令
    std::vector<TimerCmd>      _pendingTimers;  // 跨线程投递的定时/任务命令
    uint64_t                   _ackSerial = 0;  // 已确认完成的监听命令序号
    uint64_t                   _taskAckSerial = 0;  // 已确认完成的任务批序号
    std::condition_variable    _ackCond;        // unwatchSync/dispatchSync 等待确认

    std::atomic<uint64_t>      _nextTimerId{1};  // 定时任务句柄发号器
    std::unordered_map<int, FdEntry> _watches;  // fd → 监听条目（仅 Reactor 线程）
    std::unordered_map<TimerId, TimerEntry> _timers;  // 生效中的定时任务（仅 Reactor 线程）
};

// 客户端进程共享的默认 Reactor 单例：首次访问自动 start，进程退出随静态析构 stop
NetReactor &sharedReactor();

}  // namespace gy::net
