/**
* @file    net_reactor.cpp
* @version 8.1.0
* @date    2026-10-27
* @author  GridYard Team
* @brief   单线程网络事件反应器实现（Boost.Asio 引擎）
*
* 引擎线程串行运行 io_context：fd 就绪监听经 asio 描述符等待挂起，完成后
* 按监听意愿续挂下一轮；定时任务由 asio 稳定时钟定时器承载，先摘除再执行；
* 跨线程的 watch/unwatch/定时/同步任务一律投递进引擎队列，与就绪派发天然
* 串行（命令先于后续派发）。摘除监听走描述符 release：引擎注册被撤销、
* 在途等待以取消落地，原生 fd 不关闭，归属调用方自行 close——fd 复用后
* 新条目是全新描述符对象，旧对象的残留完成按序号与查表失配被丢弃。
*/

#include "core/net_reactor.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

#include <latch>

#include <algorithm>
#include <functional>
#include <utility>

namespace gy::net {

namespace {

// 线程标识折叠为 64 位整数（原子存取；碰撞概率可忽略，
// 且碰撞后果仅是把外部调用误判为线程内调用，走直接执行路径）
uint64_t threadIdValue(std::thread::id id)
{
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(id));
}

}  // namespace

// 监听条目：fd 的引擎描述符与回调（仅 Reactor 线程访问，无锁）
struct NetReactor::WatchEntry {
    explicit WatchEntry(boost::asio::io_context &context)
        : descriptor(context)
    {
    }

    uint64_t generation = 0;  // 代际计数（调用方维护，注销校验用）
    uint32_t events = 0;      // 当前监听的 Event 掩码
    EventCallback cb;         // 事件回调
    boost::asio::posix::stream_descriptor descriptor;  // 引擎侧描述符（不拥有 fd）
    uint64_t readSerial = 0;  // 在途读等待序号（0=无在途）
    uint64_t writeSerial = 0; // 在途写等待序号（0=无在途）
};

// 定时任务条目：asio 稳定时钟定时器与到期回调（仅 Reactor 线程访问）
struct NetReactor::TimerEntry {
    std::shared_ptr<boost::asio::steady_timer> timer;  // 定时器对象（取消依赖其生命周期）
    std::function<void()> callback;                    // 到期回调
};

// 同步屏障登记：unwatchSync/dispatchSync 的任务与完成栅栏，
// stop 后由清理方兜底执行，等待方永不悬死
struct NetReactor::BarrierTask {
    std::function<void()> work;        // 待执行任务
    std::shared_ptr<std::latch> done;  // 完成栅栏
    bool executed = false;             // 兜底执行防重复标志
};

// 构造函数：创建 asio 引擎上下文
NetReactor::NetReactor()
    : _ioContext{std::make_unique<boost::asio::io_context>()}
{
}

// 析构函数：确保线程退出并丢弃引擎（未执行的投递任务随上下文销毁）
NetReactor::~NetReactor()
{
    stop();
}

// 启动自有 Reactor 线程
void NetReactor::start()
{
    if (_running.load() || _ownThread) {
        return;
    }
    if (_ioContext->stopped()) {
        _ioContext->restart();
    }
    _ownThread = true;
    _running.store(true);
    _thread = std::thread([this] { runEngine(); });
}

// 停止并回收线程，清理监听与任务表
void NetReactor::stop()
{
    _running.store(false);
    if (_ioContext) {
        _ioContext->stop();
    }
    if (_ownThread && _thread.joinable()) {
        _thread.join();
    }
    _ownThread = false;
    _loopThreadId.store(0, std::memory_order_release);
    clearAll();
}

// 在当前线程运行主循环（独立服务进程模式）
void NetReactor::run()
{
    if (_running.exchange(true)) {
        return;  // 已在运行，直接返回
    }
    if (_ioContext->stopped()) {
        _ioContext->restart();
    }
    runEngine();
}

// 注册或更新 fd 监听
void NetReactor::watch(int fd, uint32_t events, uint64_t generation, EventCallback cb)
{
    if (fd < 0 || !cb) {
        return;
    }
    if (onReactorThread()) {
        applyWatch(fd, events, generation, std::move(cb));
        return;
    }
    // 跨线程投递：监听表仅引擎线程访问，投递通道自带唤醒
    boost::asio::post(*_ioContext,
                      [this, fd, events, generation, cb = std::move(cb)]() mutable {
                          applyWatch(fd, events, generation, std::move(cb));
                      });
}

// 取消 fd 监听（代际不匹配时静默忽略）
void NetReactor::unwatch(int fd, uint64_t generation)
{
    if (onReactorThread()) {
        removeWatch(fd, generation);
        return;
    }
    boost::asio::post(*_ioContext, [this, fd, generation] { removeWatch(fd, generation); });
}

// 取消监听并等待 Reactor 确认（close 前的安全摘除）
void NetReactor::unwatchSync(int fd, uint64_t generation)
{
    if (!_running.load() || onReactorThread()) {
        return;  // 未运行无需等待；Reactor 线程内调用禁止自等待
    }
    postBarrierAndWait([this, fd, generation] { removeWatch(fd, generation); });
}

// 取消监听的即时形态（Reactor 线程内直接摘除）
void NetReactor::unwatchNow(int fd, uint64_t generation)
{
    if (onReactorThread()) {
        removeWatch(fd, generation);
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
        applyTimer(id, delayMs, std::move(callback));
        return id;
    }
    boost::asio::post(*_ioContext,
                      [this, id, delayMs, callback = std::move(callback)]() mutable {
                          applyTimer(id, delayMs, std::move(callback));
                      });
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
    boost::asio::post(*_ioContext, [this, timerId] { _timers.erase(timerId); });
}

// 在 Reactor 线程同步执行任务
void NetReactor::dispatchSync(std::function<void()> task)
{
    if (!task) {
        return;
    }
    if (!_running.load() || onReactorThread()) {
        try {
            task();
        } catch (...) {
            // 静默吞掉任务异常，保持调用方语义（泵内执行路径同样吞异常）
        }
        return;  // 泵未运行或线程内调用：直接执行
    }
    postBarrierAndWait(std::move(task));
}

// 当前调用是否发生在 Reactor 线程
bool NetReactor::onReactorThread() const
{
    const uint64_t loopId = _loopThreadId.load(std::memory_order_acquire);
    return loopId != 0 && loopId == threadIdValue(std::this_thread::get_id());
}

// 唤醒 Reactor 线程（asio 投递通道自带跨线程唤醒，保留入口兼容既有调用方）
void NetReactor::wakeup()
{
}

// 事件引擎上下文（核心套接字类与独立服务入口使用）
boost::asio::io_context &NetReactor::engineContext()
{
    return *_ioContext;
}

// 引擎线程入口：常驻工作守卫保持泵运转直到 stop
void NetReactor::runEngine()
{
    _loopThreadId.store(threadIdValue(std::this_thread::get_id()), std::memory_order_release);
    const auto guard = boost::asio::make_work_guard(*_ioContext);
    _ioContext->run();
    _loopThreadId.store(0, std::memory_order_release);
}

// 把监听条目应用到引擎：已存在则原位更新（fd 复用窗口内两次 watch
// 命中同一条目，不会产生重复注册；摘除后重新 watch 则是全新描述符对象）
void NetReactor::applyWatch(int fd, uint32_t events, uint64_t generation, EventCallback cb)
{
    auto it = _watches.find(fd);
    if (it == _watches.end()) {
        auto entry = std::make_unique<WatchEntry>(*_ioContext);
        boost::system::error_code ec;
        entry->descriptor.assign(fd, ec);
        // 接管失败（fd 已失效）时仍登记条目：等待不会触发，
        // 由调用方 close 前的摘除纪律统一回收
        it = _watches.emplace(fd, std::move(entry)).first;
    }
    WatchEntry &entry = *it->second;
    entry.generation = generation;
    entry.events = events;
    entry.cb = std::move(cb);
    refreshWatch(fd, entry);
}

// 从监听表摘除条目：release 撤销引擎注册并以取消落地在途等待，fd 不关闭
void NetReactor::removeWatch(int fd, uint64_t generation)
{
    const auto it = _watches.find(fd);
    if (it == _watches.end() || it->second->generation != generation) {
        return;  // 代际不匹配的注销请求静默忽略（已被更新的监听接管）
    }
    it->second->descriptor.release();
    _watches.erase(it);
}

// 按条目的监听意愿调整读/写两类在途等待：意愿不变时保持现有等待，
// 任一类变化则描述符级取消后按新意愿重挂（被取消的完成带旧序号落地即弃；
// 重挂经引擎重新登记，内核对就绪中的 fd 会重新交付事件）
void NetReactor::refreshWatch(int fd, WatchEntry &entry)
{
    const bool wantRead = (entry.events & static_cast<uint32_t>(Event::kReadable)) != 0;
    const bool wantWrite = (entry.events & static_cast<uint32_t>(Event::kWritable)) != 0;
    if (wantRead == (entry.readSerial != 0) && wantWrite == (entry.writeSerial != 0)) {
        return;
    }
    boost::system::error_code ec;
    entry.descriptor.cancel(ec);
    entry.readSerial = 0;
    entry.writeSerial = 0;
    if (wantRead) {
        armReadWait(fd, entry);
    }
    if (wantWrite) {
        armWriteWait(fd, entry);
    }
}

// 挂读就绪等待
void NetReactor::armReadWait(int fd, WatchEntry &entry)
{
    const uint64_t serial = _nextWaitSerial.fetch_add(1);
    entry.readSerial = serial;
    entry.descriptor.async_wait(boost::asio::posix::descriptor_base::wait_read,
                                [this, fd, serial](const boost::system::error_code &ec) {
                                    onWatchReady(fd, serial, 1, ec);
                                });
}

// 挂写就绪等待
void NetReactor::armWriteWait(int fd, WatchEntry &entry)
{
    const uint64_t serial = _nextWaitSerial.fetch_add(1);
    entry.writeSerial = serial;
    entry.descriptor.async_wait(boost::asio::posix::descriptor_base::wait_write,
                                [this, fd, serial](const boost::system::error_code &ec) {
                                    onWatchReady(fd, serial, 2, ec);
                                });
}

// 就绪等待完成入口：陈旧完成按序号与查表失配丢弃
void NetReactor::onWatchReady(int fd, uint64_t serial, int waitType,
                              const boost::system::error_code &ec)
{
    auto it = _watches.find(fd);
    if (it == _watches.end() || !it->second) {
        return;  // 已摘除，陈旧完成直接丢弃
    }
    WatchEntry &entry = *it->second;
    if (waitType == 1) {
        if (entry.readSerial != serial) {
            return;  // 已被更新的读等待取代
        }
        entry.readSerial = 0;
    } else {
        if (entry.writeSerial != serial) {
            return;  // 已被更新的写等待取代
        }
        entry.writeSerial = 0;
    }
    if (ec == boost::asio::error::operation_aborted) {
        return;  // 取消路径：不再续挂，由意愿刷新方接管
    }

    uint32_t mask = 0;
    if (ec) {
        mask = static_cast<uint32_t>(Event::kError);
    } else {
        mask = waitType == 1 ? static_cast<uint32_t>(Event::kReadable)
                             : static_cast<uint32_t>(Event::kWritable);
    }
    // 拷贝出表再调用：回调内自改表（watch/unwatch）不影响本次派发，
    // 异常被吞以保证引擎线程存活（核心层无日志设施）
    EventCallback cb = entry.cb;
    if (cb) {
        try {
            cb(fd, mask);
        } catch (...) {
        }
    }
    // 回调可能已改表：重新定位后再续挂下一轮等待
    it = _watches.find(fd);
    if (it != _watches.end() && it->second) {
        refreshWatch(fd, *it->second);
    }
}

// 创建定时任务（仅 Reactor 线程调用）
void NetReactor::applyTimer(TimerId id, int delayMs, std::function<void()> callback)
{
    auto entry = std::make_shared<TimerEntry>();
    entry->timer = std::make_shared<boost::asio::steady_timer>(*_ioContext);
    entry->timer->expires_after(std::chrono::milliseconds(std::max(0, delayMs)));
    entry->callback = std::move(callback);
    entry->timer->async_wait([this, id](const boost::system::error_code &ec) {
        onTimerFired(id, ec);
    });
    _timers[id] = std::move(entry);
}

// 定时任务到期入口：先摘除再执行，回调内自取消与重复触发均不可能
void NetReactor::onTimerFired(TimerId id, const boost::system::error_code &ec)
{
    const auto it = _timers.find(id);
    if (it == _timers.end()) {
        return;  // 已取消或已执行
    }
    if (ec == boost::asio::error::operation_aborted) {
        return;  // 取消路径：句柄查表即弃
    }
    std::function<void()> callback = std::move(it->second->callback);
    _timers.erase(it);
    if (callback) {
        try {
            callback();
        } catch (...) {
            // 静默吞掉回调异常，保持引擎线程存活
        }
    }
}

// 登记同步屏障并投递执行，阻塞等待完成（unwatchSync/dispatchSync 共用）
void NetReactor::postBarrierAndWait(std::function<void()> work)
{
    auto state = std::make_shared<BarrierTask>();
    state->work = std::move(work);
    state->done = std::make_shared<std::latch>(1);
    {
        std::lock_guard<std::mutex> lock(_barrierMutex);
        _barriers.push_back(state);
    }
    boost::asio::post(*_ioContext, [this, state] { runBarrier(state); });
    state->done->wait();
}

// 执行同步屏障任务并应答等待方
void NetReactor::runBarrier(std::shared_ptr<BarrierTask> state)
{
    {
        std::lock_guard<std::mutex> lock(_barrierMutex);
        if (state->executed) {
            return;  // stop 兜底已执行
        }
        state->executed = true;
        const auto it = std::find(_barriers.begin(), _barriers.end(), state);
        if (it != _barriers.end()) {
            _barriers.erase(it);
        }
    }
    if (state->work) {
        try {
            state->work();
        } catch (...) {
            // 静默吞掉任务异常，保持引擎线程存活
        }
    }
    state->done->count_down();
}

// 停机清理：引擎已停，此刻是监听表与任务表的唯一访问者
void NetReactor::clearAll()
{
    // 监听条目逐个 release：撤销引擎注册且不关闭原生 fd（fd 归调用方 close）
    for (auto &entryPair : _watches) {
        if (entryPair.second) {
            entryPair.second->descriptor.release();
        }
    }
    _watches.clear();
    _timers.clear();

    // 未完成的同步任务兜底执行并应答，等待方永不悬死
    std::vector<std::shared_ptr<BarrierTask>> pending;
    {
        std::lock_guard<std::mutex> lock(_barrierMutex);
        pending.swap(_barriers);
    }
    for (auto &state : pending) {
        if (!state->executed) {
            state->executed = true;
            if (state->work) {
                try {
                    state->work();
                } catch (...) {
                }
            }
            state->done->count_down();
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
