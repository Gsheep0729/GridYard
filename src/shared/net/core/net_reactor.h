/**
* @file    net_reactor.h
* @version 8.1.0
* @date    2026-10-27
* @author  GridYard Team
* @brief   单线程网络事件反应器——纯 C++23，不依赖任何 Qt 类型
*
* 仅用于 Linux/POSIX 平台，引擎为 Boost.Asio io_context（vendored，纯头库）：
* 引擎线程串行运行全部就绪派发、定时任务与同步投递任务，语义与旧 poll 泵
* 一致（命令先于派发、注销屏障、定时任务先摘除再执行）。对外 fd 的就绪
* 监听经 asio 描述符等待实现，同一描述符对象天然承担 fd 复用后的代际隔离——
* 摘除后旧对象上的事件按对象身份丢弃，绝不会派发到重新打开的同号 fd 上。
* 所有回调在 Reactor 线程中同步触发，禁止在回调内阻塞。
* 定时任务（scheduleAfter）由 asio 稳定时钟定时器驱动，到期后在 Reactor
* 线程执行。
*/

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace boost {
namespace asio {
class io_context;
}  // namespace asio
namespace system {
class error_code;
}  // namespace system
}  // namespace boost

namespace gy::net {

// 事件掩码（就绪事件位的归一化表示）
enum class Event : uint32_t {
    kReadable = 0x01,  // 可读（含对端挂断：挂断以 EOF 可读形态到达）
    kWritable = 0x02,  // 可写
    kError    = 0x04,  // 套接字错误（如 RST/ECONNRESET）
    kHangup   = 0x08,  // 对端挂断（保留位；挂断现随可读就绪一并交付）
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

    // 唤醒 Reactor 线程（投递通道自带唤醒，保留为兼容空操作）
    void wakeup();

    // 事件引擎上下文：供核心套接字类把异步对象挂到同一执行域、
    // 以及独立服务入口把退出信号集挂到同一 io_context 上使用。
    // 仅允许在引擎运行或对象未投递任何异步操作的生命周期内触碰。
    boost::asio::io_context &engineContext();

private:
    // 监听条目、定时任务条目与同步屏障登记（完整定义在实现文件，含 asio 对象）
    struct WatchEntry;
    struct TimerEntry;
    struct BarrierTask;

    // 把监听条目应用到引擎（仅 Reactor 线程调用）
    void applyWatch(int fd, uint32_t events, uint64_t generation, EventCallback cb);
    // 从监听表摘除条目（仅 Reactor 线程调用；generation 不匹配时静默忽略）
    void removeWatch(int fd, uint64_t generation);
    // 按条目的监听意愿调整读/写两类在途等待（仅 Reactor 线程调用）
    void refreshWatch(int fd, WatchEntry &entry);
    // 挂读就绪等待（仅 Reactor 线程调用）
    void armReadWait(int fd, WatchEntry &entry);
    // 挂写就绪等待（仅 Reactor 线程调用）
    void armWriteWait(int fd, WatchEntry &entry);
    // 就绪等待完成入口（仅 Reactor 线程调用）
    void onWatchReady(int fd, uint64_t serial, int waitType,
                      const boost::system::error_code &ec);
    // 创建定时任务（仅 Reactor 线程调用）
    void applyTimer(TimerId id, int delayMs, std::function<void()> callback);
    // 定时任务到期入口（仅 Reactor 线程调用）
    void onTimerFired(TimerId id, const boost::system::error_code &ec);
    // 登记同步屏障并投递执行，阻塞等待完成（unwatchSync/dispatchSync 共用）
    void postBarrierAndWait(std::function<void()> work);
    // 执行同步屏障任务并应答等待方（仅 Reactor 线程或停机清理调用）
    void runBarrier(std::shared_ptr<BarrierTask> state);
    // 停机清理：摘除全部监听与定时任务、应答未完成的同步等待方
    void clearAll();

    // 引擎线程入口（start 的后台线程与 run 的调用线程共用）
    void runEngine();

    std::unique_ptr<boost::asio::io_context> _ioContext;  // asio 引擎上下文
    std::thread                _thread;         // Reactor 线程（start 模式）
    std::atomic<bool>          _running{false};  // 主循环运行标志
    bool                       _ownThread = false;  // start 模式标志（stop 需 join）
    std::atomic<uint64_t>      _loopThreadId{0};  // Reactor 线程标识（thread::id 哈希，0=未运行）

    std::mutex                 _barrierMutex;   // 保护同步等待登记表
    std::vector<std::shared_ptr<BarrierTask>> _barriers;  // 在途同步屏障（stop 时兜底执行）

    std::atomic<uint64_t>      _nextTimerId{1};   // 定时任务句柄发号器
    std::atomic<uint64_t>      _nextWaitSerial{1};  // 在途就绪等待序号发号器
    std::unordered_map<int, std::unique_ptr<WatchEntry>> _watches;  // fd → 监听条目（仅 Reactor 线程）
    std::unordered_map<TimerId, std::shared_ptr<TimerEntry>> _timers;  // 生效中的定时任务（仅 Reactor 线程）
};

// 客户端进程共享的默认 Reactor 单例：首次访问自动 start，进程退出随静态析构 stop
NetReactor &sharedReactor();

}  // namespace gy::net
