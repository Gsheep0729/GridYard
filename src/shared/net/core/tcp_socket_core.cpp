/**
* @file    tcp_socket_core.cpp
* @version 8.1.0
* @date    2026-10-27
* @author  GridYard Team
* @brief   纯 POSIX TCP 连接核心实现（Boost.Asio 引擎）
*
* 读写经异步操作驱动：读完成把数据搬入用户缓冲并按块通知，缓冲饱和即
* 停挂读形成 TCP 背压，消费后恢复；写完成推进用户态写队列，余量续传，
* 写错误与 EOF 分开归因。全部异步处理器绑定本套接字的 strand，关闭与
* 取消在引擎线程内串行执行；取消完成的处理器只做零成员访问的短路返回，
* 存活哨兵保证已析构对象的迟到完成安全丢弃。
*/

#include "core/tcp_socket_core.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/post.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <latch>
#include <utility>

namespace gy::net {

namespace {

constexpr size_t kReceiveChunkBytes = 64 * 1024;  // 单次读取块大小

// 把套接字地址格式化为文本：IPv4 映射的 IPv6 地址归一化为点分十进制
std::string formatPeerAddress(const sockaddr *address, socklen_t length)
{
    if (!address || length < sizeof(sockaddr)) {
        return {};
    }
    char host[INET6_ADDRSTRLEN] = {0};
    if (address->sa_family == AF_INET) {
        const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
        ::inet_ntop(AF_INET, &ipv4->sin_addr, host, sizeof(host));
    } else if (address->sa_family == AF_INET6) {
        const auto *ipv6 = reinterpret_cast<const sockaddr_in6 *>(address);
        ::inet_ntop(AF_INET6, &ipv6->sin6_addr, host, sizeof(host));
    } else {
        return {};
    }
    // 双栈监听接入的 IPv4 客户端呈现为 ::ffff:a.b.c.d，剥掉映射前缀
    std::string text = host;
    constexpr const char *kV4MappedPrefix = "::ffff:";
    if (text.starts_with(kV4MappedPrefix)) {
        const std::string dotted = text.substr(std::strlen(kV4MappedPrefix));
        if (dotted.find(':') == std::string::npos) {
            return dotted;
        }
    }
    return text;
}

}  // namespace

// 构造函数
TcpSocketCore::TcpSocketCore(NetReactor &reactor)
    : _reactor{reactor}
    , _socket{reactor.engineContext()}
    , _strand{reactor.engineContext().get_executor()}
    , _alive{std::make_shared<std::atomic_bool>(true)}
    , _readChunk(kReceiveChunkBytes)
{
}

// 析构函数：关闭描述符并让迟到完成经存活哨兵安全丢弃
TcpSocketCore::~TcpSocketCore()
{
    _alive->store(false);
    if (_fd < 0) {
        return;
    }
    if (_reactor.onReactorThread() || !_reactor.isRunning()) {
        // 引擎线程内或引擎未运行：无并发派发，直接关闭
        boost::system::error_code ec;
        _socket.close(ec);
        _fd = -1;
        return;
    }
    // 引擎运行中的外线程析构：投递到 strand 关闭并等待生效，
    // 返回后不再有本对象的派发在途（对齐旧注销屏障语义）
    std::latch done(1);
    boost::asio::post(_strand, [this, &done] {
        boost::system::error_code ec;
        _socket.close(ec);
        done.count_down();
    });
    done.wait();
    _fd = -1;
}

// 接管已连接描述符
bool TcpSocketCore::adopt(int descriptor)
{
    if (descriptor < 0) {
        return false;
    }
    if (_reactor.onReactorThread() || !_reactor.isRunning()) {
        // 引擎线程内或引擎未运行（无并发派发）：直接执行
        return adoptOnEngine(descriptor);
    }
    // 外线程接管：投递到 strand 执行并等待生效，保证接管期间无并发事件
    std::latch done(1);
    std::atomic_bool ok{false};
    boost::asio::post(_strand, [this, descriptor, &done, &ok] {
        ok.store(adoptOnEngine(descriptor));
        done.count_down();
    });
    done.wait();
    return ok.load();
}

// 引擎线程内的接管主体
bool TcpSocketCore::adoptOnEngine(int descriptor)
{
    if (_fd >= 0) {
        boost::system::error_code ec;
        _socket.close(ec);
        _fd = -1;
    }
    boost::system::error_code ec;
    _socket.assign(boost::asio::ip::tcp::v4(), descriptor, ec);
    if (ec) {
        // IPv4 协议标注不被接受时按 IPv6 再试（双栈监听接入的描述符）
        ec.clear();
        _socket.assign(boost::asio::ip::tcp::v6(), descriptor, ec);
    }
    if (ec) {
        ::close(descriptor);
        return false;
    }

    _fd = descriptor;
    // TCP_NODELAY 尽力开启，失败（如测试用 socketpair）不中断接管
    boost::system::error_code optionEc;
    _socket.set_option(boost::asio::ip::tcp::no_delay(true), optionEc);

    _rxBuf.clear();
    _rxOffset = 0;
    _txBuf.clear();
    _txOffset = 0;
    _gracefulClosePending = false;
    _readArmed = false;
    _writeArmed = false;
    armRead();
    return true;
}

// 设置读缓冲上限
void TcpSocketCore::setReadBufferLimit(int64_t bytes)
{
    _readBufferLimit = std::max<int64_t>(0, bytes);
    if (_fd >= 0) {
        armRead();  // 上限收紧由饱和判定立即停读，放宽则在此续挂恢复
    }
}

// 可读字节数（用户缓冲与内核待读之和）
int64_t TcpSocketCore::bytesAvailable() const
{
    int64_t total = bufferedBytes();
    if (_fd >= 0) {
        int pending = 0;
        if (::ioctl(_fd, FIONREAD, &pending) == 0 && pending > 0) {
            total += pending;
        }
    }
    return total;
}

// 待写字节数
int64_t TcpSocketCore::bytesToWrite() const
{
    if (_txOffset >= _txBuf.size()) {
        return 0;
    }
    return static_cast<int64_t>(_txBuf.size() - _txOffset);
}

// 取走用户缓冲全部数据
std::vector<uint8_t> TcpSocketCore::readAll()
{
    if (bufferedBytes() <= 0) {
        _rxBuf.clear();
        _rxOffset = 0;
        return {};
    }
    std::vector<uint8_t> data(_rxBuf.begin() + static_cast<ptrdiff_t>(_rxOffset), _rxBuf.end());
    _rxBuf.clear();
    _rxOffset = 0;
    if (_fd >= 0) {
        armRead();  // 消费后解除背压，恢复挂读
    }
    return data;
}

// 追加写数据（向量形式）
void TcpSocketCore::write(const std::vector<uint8_t> &data)
{
    if (data.empty()) {
        return;
    }
    write(data.data(), data.size());
}

// 追加写数据：入队后经引擎异步送入内核
void TcpSocketCore::write(const uint8_t *data, size_t length)
{
    if (!data || length == 0 || _fd < 0) {
        return;
    }
    _txBuf.insert(_txBuf.end(), data, data + length);
    armWrite();
}

// 优雅关闭：写队列排空后关闭
void TcpSocketCore::disconnectFromHost()
{
    if (_fd < 0) {
        return;
    }
    _gracefulClosePending = true;
    if (bytesToWrite() == 0) {
        finishClose(SocketCloseReason::kLocalClose);
        return;
    }
    armWrite();
}

// 立即关闭
void TcpSocketCore::abort()
{
    if (_fd < 0) {
        return;
    }
    if (_reactor.onReactorThread() || !_reactor.isRunning()) {
        finishClose(SocketCloseReason::kLocalClose);
        return;
    }
    // 外线程调用：投递到 strand 关闭并等待生效（close/cancel 的引擎串行化契约）
    std::latch done(1);
    boost::asio::post(_strand, [this, &done] {
        finishClose(SocketCloseReason::kLocalClose);
        done.count_down();
    });
    done.wait();
}

// 对端地址文本
std::string TcpSocketCore::peerAddressText() const
{
    if (_fd < 0) {
        return {};
    }
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (::getpeername(_fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        return {};
    }
    return formatPeerAddress(reinterpret_cast<const sockaddr *>(&address), length);
}

// 异步读完成入口
void TcpSocketCore::onReadCompleted(const boost::system::error_code &ec, size_t amount)
{
    if (ec == boost::asio::error::eof) {
        finishClose(SocketCloseReason::kPeerClosed);
        return;
    }
    if (ec) {
        ErrorCallback onError = _onError;
        if (onError) {
            onError(std::string("recv 失败: ") + ec.message());
        }
        finishClose(SocketCloseReason::kError);
        return;
    }

    _rxBuf.insert(_rxBuf.end(), _readChunk.begin(), _readChunk.begin() + static_cast<ptrdiff_t>(amount));
    ReadCallback onRead = _onRead;
    if (onRead) {
        _inCallback = true;
        onRead();
        _inCallback = false;
    }
    if (_fd >= 0) {
        armRead();  // 未饱和继续挂读；饱和由消费方触发恢复
    }
}

// 异步写完成入口
void TcpSocketCore::onWriteCompleted(const boost::system::error_code &ec, size_t amount)
{
    if (ec) {
        ErrorCallback onError = _onError;
        if (onError) {
            onError(std::string("send 失败: ") + ec.message());
        }
        finishClose(SocketCloseReason::kError);
        return;
    }

    _txOffset += amount;
    if (_txOffset >= _txBuf.size()) {
        _txBuf.clear();
        _txOffset = 0;
    }
    if (amount > 0) {
        WrittenCallback onWritten = _onWritten;
        if (onWritten) {
            _inCallback = true;
            onWritten(static_cast<int64_t>(amount));
            _inCallback = false;
        }
    }
    if (_fd < 0) {
        return;
    }
    if (_gracefulClosePending && bytesToWrite() == 0) {
        // 写队列排空且挂起了优雅关闭：此刻完成关闭
        finishClose(SocketCloseReason::kLocalClose);
        return;
    }
    armWrite();
}

// 按当前状态续挂异步读
void TcpSocketCore::armRead()
{
    if (_fd < 0 || _readArmed || readSaturated()) {
        return;
    }
    _readArmed = true;
    auto alive = _alive;
    _socket.async_read_some(boost::asio::buffer(_readChunk.data(), _readChunk.size()),
                            boost::asio::bind_executor(_strand, [this, alive](
                                const boost::system::error_code &ec, size_t amount) {
                                // 取消路径零成员访问；存活哨兵拦截已析构对象的迟到完成
                                if (ec == boost::asio::error::operation_aborted || !alive->load()) {
                                    return;
                                }
                                _readArmed = false;
                                onReadCompleted(ec, amount);
                            }));
}

// 按当前状态续挂异步写
void TcpSocketCore::armWrite()
{
    if (_fd < 0 || _writeArmed || _txOffset >= _txBuf.size()) {
        return;
    }
    _writeArmed = true;
    auto alive = _alive;
    _socket.async_write_some(boost::asio::buffer(_txBuf.data() + _txOffset,
                                                 _txBuf.size() - _txOffset),
                             boost::asio::bind_executor(_strand, [this, alive](
                                 const boost::system::error_code &ec, size_t amount) {
                                 if (ec == boost::asio::error::operation_aborted || !alive->load()) {
                                     return;
                                 }
                                 _writeArmed = false;
                                 onWriteCompleted(ec, amount);
                             }));
}

// 读缓冲是否已达上限（背压判定）
bool TcpSocketCore::readSaturated() const
{
    return _readBufferLimit > 0 && bufferedBytes() >= _readBufferLimit;
}

// 用户缓冲未读字节数
int64_t TcpSocketCore::bufferedBytes() const
{
    if (_rxOffset >= _rxBuf.size()) {
        return 0;
    }
    return static_cast<int64_t>(_rxBuf.size() - _rxOffset);
}

// 统一收尾：关闭描述符并通知终结（仅引擎线程内执行）
void TcpSocketCore::finishClose(SocketCloseReason reason)
{
    if (_fd < 0) {
        return;
    }
    _fd = -1;
    _gracefulClosePending = false;
    _readArmed = false;
    _writeArmed = false;
    _rxBuf.clear();
    _rxOffset = 0;
    _txBuf.clear();
    _txOffset = 0;
    // 真关描述符：挂起的读写操作以取消落地，其处理器只做短路返回
    boost::system::error_code ec;
    _socket.close(ec);

    // 回调最后触发且回调后零成员访问：持有方允许在回调内立即析构本对象
    CloseCallback onClose = std::move(_onClose);
    if (onClose) {
        onClose(reason);
    }
}

}  // namespace gy::net
