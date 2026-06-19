# GridYard C++↔QML 通信序列图（v4.16.0 当前架构）

按四个核心场景拆分序列图：应用启动初始化、文件发送、文件接收（含后台线程模型）、取消与协议错误处理。每个序列图标注跨线程边界与 Q_PROPERTY / Q_INVOKABLE 调用方向。

---

## 1. 应用启动初始化

```mermaid
sequenceDiagram
    participant main as main.cpp
    participant logger as Logger
    participant engine as QQmlApplicationEngine
    participant moc as moc
    participant app as AppController
    participant config as ConfigManager
    participant discovery as DiscoveryService
    participant p2p as P2pServer
    participant session as TransferSessionManager
    participant qml as Main.qml

    Note over main: 程序启动
    main->>main: QGuiApplication 初始化<br/>setApplicationVersion("4.16.0")
    main->>main: 设置 KDE 原生文件选择器环境变量
    main->>logger: Logger::instance().init()
    logger-->>main: 日志拦截就绪

    main->>engine: loadFromModule("cqnu.gridyard.client", "Main")

    engine->>moc: 查找 QML_SINGLETON 类型
    moc-->>engine: AppController / ConfigManager 元信息

    engine->>config: ConfigManager::create()
    config->>config: 读取 QSettings<br/>ensureDeviceId()
    config-->>engine: ConfigManager 全局实例

    engine->>app: AppController::create()
    app->>config: 获取全局实例
    app->>discovery: new DiscoveryService(config)
    app->>p2p: new P2pServer(config)
    app->>p2p: p2p.start()
    p2p-->>app: TCP 监听端口就绪
    app->>session: new TransferSessionManager
    app->>session: session.init(config, discovery, p2p)
    session->>p2p: connect transferRequestReceived
    app-->>engine: AppController 实例

    engine->>qml: 加载 Main.qml
    qml->>app: 读取 applicationName / applicationVersion
    app-->>qml: "GridYard" / "4.16.0"
    qml->>discovery: 绑定 peers 属性
    qml->>session: 绑定 transfer.sessions
```

启动顺序的关键约束：`ConfigManager` 通过静态 `QPointer` 实现真单例，保证 QML 引擎工厂创建的实例与 `AppController::_config` 引用的是同一对象；`P2pServer` 必须在 `TransferSessionManager.init()` 之前 `start()`，否则 `connect` 时端口未监听。

---

## 2. 文件发送时序

```mermaid
sequenceDiagram
    participant qml as Main.qml
    participant session as TransferSessionManager
    participant discovery as DiscoveryService
    participant config as ConfigManager
    participant thread as QThread
    participant worker as FileSenderWorker
    participant codec as FrameCodec
    participant tcp as QTcpSocket
    participant peer as 接收端

    Note over qml: 用户拖拽文件到 DeviceCard
    qml->>session: createSendSession(deviceId, filePath)
    session->>discovery: peerInfo(deviceId)
    discovery-->>session: PeerInfo{ip, port}
    session->>config: 读取 deviceId / deviceName
    session->>session: 构造 SessionRecord<br/>status="connecting"
    session-->>qml: sessionsChanged()

    session->>thread: new QThread
    session->>worker: new FileSenderWorker
    session->>worker: moveToThread(thread)
    session->>thread: connect started -> startTransfer
    session->>thread: thread.start()

    thread->>worker: startTransfer(host, port, path,<br/>senderDeviceId, senderName)
    worker->>worker: DirSerializer::serialize(path)
    worker->>tcp: connectToHost(ip, port)
    tcp-->>worker: connected
    worker->>worker: sendTransferRequest()
    worker->>codec: encode(kTypeTransferReq, json)
    codec-->>worker: TLV 字节流
    worker->>tcp: write(frame)
    tcp->>peer: TransferReq 帧

    peer-->>tcp: TransferRsp(accepted)
    tcp->>worker: readyRead
    worker->>codec: feed(data)
    codec-->>worker: frameReady(kTypeTransferRsp, payload)
    worker->>worker: 解析 accepted / error_code
    opt accepted
        worker-->>worker: requestAccepted()
        loop 每个文件 + 8MB 分块
            worker->>worker: sendNextChunk()
            worker->>codec: encode(kTypeDataChunk, bytes)
            worker->>tcp: write(frame)
            worker->>worker: 进度节流（每 4 chunk）
            worker-->>session: progressChanged(bytesSent, totalBytes)
            session-->>qml: sessionsChanged()
        end
        worker->>worker: sendTransferDone()
    end

    alt 完成
        worker-->>session: transferFinished(true, Success, "")
        session->>session: status="completed", errorCode=0
        session-->>qml: messageOccurred("文件 X 发送成功")
    else 失败
        worker-->>session: transferFinished(false, errorCode, errorMsg)
        session->>session: status="failed", errorCode=N
        session-->>qml: errorOccurred("发送失败：...")
    end
    session->>thread: quit() / wait()
    session->>worker: deleteLater()
    session->>thread: deleteLater()
```

发送侧线程模型：`TransferSessionManager` 在主线程构造 `SessionRecord`，但 `FileSenderWorker` 与 `QTcpSocket` 全部 `moveToThread` 到独立 `QThread`。`transferFinished` 通过 Queued Connection 回到主线程更新模型，避免 QML 绑定与 worker 线程竞争。

---

## 3. 文件接收时序（含后台线程模型）

```mermaid
sequenceDiagram
    participant peer as 发送端
    participant tcp as QTcpServer
    participant p2p as P2pServer
    participant thread as QThread (后台)
    participant worker as FileReceiverWorker
    participant codec as FrameCodec
    participant session as TransferSessionManager
    participant config as ConfigManager
    participant qml as AcceptDialog

    Note over peer: 发送端发起 TCP 连接
    peer->>tcp: SYN
    tcp->>p2p: newConnection
    p2p->>p2p: nextPendingConnection()
    p2p->>p2p: setSendBufferSize / setReceiveBufferSize (4MB)
    p2p->>thread: new QThread(this)
    p2p->>worker: new FileReceiverWorker(socket)
    p2p->>worker: moveToThread(thread)
    p2p->>thread: connect started -> worker.initialize
    p2p->>thread: connect transferFinished -> thread.quit
    p2p->>thread: connect finished -> deleteLater (worker + thread)
    p2p->>thread: thread.start()

    thread->>worker: initialize()
    Note over worker: 在后台线程内创建 QTimer<br/>连接 socket 信号到 worker 槽
    worker->>worker: onReadyRead / onDisconnected<br/>onFrameReady / onTimeout

    peer->>tcp: TransferReq 帧
    tcp->>worker: readyRead
    worker->>codec: feed(data)
    codec-->>worker: frameReady(kTypeTransferReq, json)
    worker->>worker: handleTransferRequest<br/>校验 JSON / 路径穿越 / 文件数
    worker-->>p2p: transferRequestReceived(...)
    p2p-->>session: onTransferRequestReceived(worker, ...)

    session->>config: 读取 receivePath
    session->>worker: setReceivePath (QueuedConnection)
    session->>session: 构造 SessionRecord<br/>status="waiting_confirm"

    alt 配置 autoAcceptFiles
        session->>session: acceptReceiveSession(sessionId)
    else 需要弹窗
        session-->>qml: receiveRequestReceived(...)
        qml->>qml: 显示文件名 / 大小 / 根目录预览
        qml->>session: acceptReceiveSession / rejectReceiveSession
    end

    alt 用户接受
        session->>worker: acceptTransfer (QueuedConnection)
        worker->>worker: sendTransferResponse(true, Success)
        worker->>codec: encode(kTypeTransferRsp, json)
        worker->>peer: TransferRsp(accepted)
        loop 接收分块
            peer->>tcp: DataChunk 帧
            tcp->>worker: readyRead
            worker->>codec: feed(data)
            codec-->>worker: frameReady(kTypeDataChunk, bytes)
            worker->>worker: handleDataChunk<br/>校验 index / offset / length
            worker->>worker: 写入文件 + 增量 SHA-256
            worker->>worker: 进度节流（_receiveChunkCount）
            worker-->>session: progressChanged(bytesReceived, totalBytes)
        end
        peer->>tcp: TransferDone
        worker->>worker: handleTransferDone
        worker->>worker: 计算 SHA-256 与对端对比
        worker->>worker: sendChunkAck(verified, errorCode)
        worker-->>p2p: transferFinished(true, Success, "")
    else 用户拒绝
        session->>worker: rejectTransfer(reason)
        worker->>worker: sendTransferResponse(false, UserRejected)
        worker-->>p2p: transferFinished(false, UserRejected, reason)
    end

    p2p->>thread: QMetaObject::invokeMethod thread.quit (Queued)
    thread->>thread: quit()
    thread-->>p2p: finished
    p2p->>worker: deleteLater
    p2p->>thread: deleteLater

    session->>session: 更新 SessionRecord<br/>errorCode / localPath / canDeleteLocalFile
    session-->>qml: sessionsChanged()
```

接收侧后台线程模型的关键点：

1. `P2pServer::onNewConnection()` 在主线程触发，但所有重活都委派给后台线程。
2. `FileReceiverWorker::initialize()` 必须在 worker 所在线程内执行——`QTimer` 与 socket 信号连接属于线程亲和性资源，在主线程创建会跨线程触发槽函数导致崩溃。
3. worker + socket 一起 `moveToThread`，确保所有 TCP I/O 与文件写入都不阻塞 UI。
4. `QThread` 父对象设为 `P2pServer`，析构时通过 `children()` 统一 `quit() / wait()`。

---

## 4. 取消与协议错误处理

```mermaid
sequenceDiagram
    participant qml as Main.qml
    participant session as TransferSessionManager
    participant worker as Worker (任意一侧)
    participant codec as FrameCodec
    participant tcp as QTcpSocket

    Note over qml: 用户点击取消按钮
    qml->>session: cancelSession(sessionId)

    alt 发送方
        session->>worker: QMetaObject::invokeMethod "cancel"
        worker->>worker: sendCancel(reason)
        worker->>codec: encode(kTypeCancel, reason)
        worker->>tcp: write(frame)
        worker->>worker: cleanup()
        worker-->>session: transferFinished(false, UserCancelled, reason)
    else 接收方
        session->>worker: rejectTransfer("用户取消") (QueuedConnection)
        worker->>worker: sendTransferResponse(false, UserCancelled)
        worker->>worker: cleanup()
        worker-->>session: transferFinished(false, UserCancelled, reason)
    end

    session->>session: status="cancelled", errorCode=5002
    session-->>qml: sessionsChanged()

    Note over codec,tcp: 协议错误场景：帧长超限
    tcp->>codec: feed(畸形数据)
    codec->>codec: maxPayloadForType 校验失败<br/>或帧头格式错误
    codec-->>worker: errorOccurred(FrameTooLarge, msg)
    worker->>worker: cleanup()<br/>关闭 socket / 删除不完整文件
    worker-->>session: transferFinished(false, FrameTooLarge, msg)
    session->>session: status="failed", errorCode=2002
    session-->>qml: errorOccurred("接收失败：帧载荷超限")

    Note over codec,tcp: 协议错误场景：版本不兼容
    codec-->>worker: frameReady(kTypeHello, json)
    worker->>worker: 解析 protocolVersion
    alt 主版本不一致
        worker->>worker: 拒绝会话
        worker-->>session: transferFinished(false, ProtocolMismatch, ...)
    else 次版本差异
        worker->>worker: 安全降级，继续传输
    end
```

错误码统一通过 `transferFinished(bool, ErrorCode, QString)` 三参数信号上报，`SessionRecord` 持久化 `errorCode` 字段供 QML 区分场景：

| ErrorCode | 含义 | 触发场景 |
|:---|:---|:---|
| 1002 ConnectionLost | 连接断开 | TCP 连接被对端关闭或网络中断 |
| 1003 TransferTimeout | 传输超时 | 30s 内无任何数据收发 |
| 2002 FrameTooLarge | 帧载荷超限 | 控制帧超过 1MB，或 DataChunk 超过 256MB |
| 3001 ProtocolMismatch | 协议版本不匹配 | Hello 包主版本号与本地不一致 |
| 4003 Sha256Mismatch | 校验失败 | 接收完成后哈希与发送端不一致 |
| 5001 UserRejected | 用户拒绝 | 接收确认弹窗点击「拒绝」 |
| 5002 UserCancelled | 用户取消 | 发送 / 接收方主动点击「取消」 |
