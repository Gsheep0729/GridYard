# GridYard 数据流图（v4.16.0 当前架构）

按 5 条独立数据流组织，每条流标注数据载体、跨层边界与协议契约。底部汇总协议错误处理流，覆盖 Stage 4 收尾引入的 ErrorCode 链路。

---

## 1. 设备发现流（UDP Hello ↔ DiscoveryService）

```mermaid
flowchart LR
    subgraph "本地启动"
        cfg1[ConfigManager<br/>deviceId / deviceName / tcpPort]
    end

    subgraph "广播"
        udp_tx[("UDP 45678<br/>广播")]
    end

    subgraph "在线节点表"
        ds[DiscoveryService<br/>QHash deviceId→PeerInfo]
    end

    subgraph "QML 表现层"
        plv[PeerListView<br/>deviceCard 列表]
    end

    cfg1 -->|每 5s 拼接 Hello JSON<br/>含 protocolVersion| udp_tx
    udp_tx -->|局域网广播| ds
    ds -->|接收对端 Hello<br/>版本兼容性检查| ds
    ds -->|15s 无心跳剔除离线| ds
    ds -->|peersChanged signal| plv
    plv -.->|用户点击设备| cfg1
```

数据契约：Hello 包 JSON 含 `deviceId`、`deviceName`、`ipAddress`、`tcpPort`、`protocolVersion`。`PeerInfo::protocolVersion` 用于版本协商：主版本号一致才接受会话，次版本差异允许安全降级。

---

## 2. 文件发送流（QML 拖拽 → 对端 TCP）

```mermaid
flowchart LR
    subgraph "QML 表现层"
        drag[DeviceCard<br/>DropArea / fileDropped signal]
        panel1[TransferPanel<br/>进度条]
    end

    subgraph "应用逻辑层"
        tsm1[TransferSessionManager<br/>SessionRecord status]
    end

    subgraph "领域层 (独立 QThread)"
        fs1[FileSenderWorker]
        dir1[DirSerializer<br/>serialize + computeSha256]
        fc1[FrameCodec<br/>encode]
    end

    subgraph "网络层"
        tcp_out[QTcpSocket<br/>8MB 分块 / 16MB 背压]
    end

    subgraph "外部"
        fsread[("FileSystem<br/>读取源文件")]
        peer[("对端 P2pServer")]
    end

    drag -->|createSendSession| tsm1
    tsm1 -->|查询 PeerInfo| tsm1
    tsm1 -->|创建 QThread + Worker| fs1
    fs1 -->|枚举文件 / 算哈希| dir1
    dir1 -->|FileItem 列表| fs1
    fs1 -->|读取 8MB chunk| fsread
    fs1 -->|encode TransferReq / DataChunk| fc1
    fc1 -->|TLV 字节流| fs1
    fs1 -->|write| tcp_out
    tcp_out -->|TCP 帧| peer

    fs1 -.->|progressChanged| tsm1
    tsm1 -.->|sessionsChanged| panel1
```

发送链路的三段协议：`kTypeTransferReq`（JSON 元数据 + SHA-256）、`kTypeDataChunk`（8MB 二进制分块）、`kTypeTransferDone`（全部发送完成）。发送方按 `kMaxQueuedBytes = 16MB` 限制 Qt socket 写队列，避免大型文件一次性堆积到内存。

---

## 3. 文件接收流（TCP 入站 → 落盘）

```mermaid
flowchart LR
    subgraph "外部"
        peer_in[("对端 FileSenderWorker")]
    end

    subgraph "领域层"
        p2p[P2pServer<br/>每连接创建 QThread]
        thr[(后台 QThread)]
        fr[FileReceiverWorker<br/>initialize 在线程内]
        fc2[FrameCodec<br/>feed + 状态机]
        hasher[QCryptographicHash<br/>增量 SHA-256]
    end

    subgraph "应用逻辑层"
        tsm2[TransferSessionManager<br/>SessionRecord]
    end

    subgraph "QML 表现层"
        accept[AcceptDialog<br/>接收确认]
        panel2[TransferPanel<br/>完成 / 打开目录]
    end

    subgraph "外部"
        cfg2[ConfigManager<br/>receivePath]
        fswrite[("FileSystem<br/>~/GridYard/document")]
    end

    peer_in -->|TCP connect| p2p
    p2p -->|moveToThread<br/>worker + socket| thr
    thr -->|started → initialize| fr
    peer_in -.->|TransferReq / DataChunk 帧| fr
    fr -->|feed| fc2
    fc2 -.->|frameReady| fr
    fr -->|校验 + 写入| fswrite
    fr -.->|增量更新| hasher

    fr -.->|transferRequestReceived| tsm2
    tsm2 -.->|receiveRequestReceived| accept
    accept -.->|acceptReceiveSession| tsm2
    tsm2 -.->|"setReceivePath (Queued)"| fr
    cfg2 -.->|receivePath| tsm2
    fr -.->|progressChanged| tsm2
    tsm2 -.->|sessionsChanged| panel2
    fr -.->|transferFinished + savedPath| tsm2
    tsm2 -.->|transferCompleted| panel2
```

接收侧关键边界：所有 TCP I/O、文件写入、SHA-256 计算都在后台线程执行；`TransferSessionManager` 通过 `QMetaObject::invokeMethod(worker, ..., Qt::QueuedConnection)` 跨线程调用 `acceptTransfer / rejectTransfer / setReceivePath`，避免直接操作 worker 内部状态。

---

## 4. 配置与日志流

```mermaid
flowchart LR
    subgraph "QML 表现层"
        sd[SettingsDialog<br/>deviceName / receivePath / tcpPort / autoAccept]
    end

    subgraph "应用逻辑层"
        cm[ConfigManager<br/>QML_SINGLETON]
        lg[Logger<br/>单例]
    end

    subgraph "数据管理层"
        qs[("QSettings<br/>~/.config/GridYard.conf")]
        logfile[("日志文件<br/>~/GridYard/logs/gridyard_yyyyMMdd.log")]
    end

    sd -->|setDeviceName / setReceivePath| cm
    cm -->|NOTIFY signal| sd
    cm -->|首次启动生成 UUID<br/>ensureDeviceId| qs
    qs -->|读取持久化配置| cm

    subgraph "Qt 全局日志"
        qwarn[qDebug / qWarning / qCritical]
    end

    qwarn -->|qInstallMessageHandler| lg
    lg -->|QMutex 线程安全<br/>按日期切换| logfile
    lg -.->|控制台同步输出| console[("stderr")]
```

`ConfigManager` 通过 `s_instance` 静态 `QPointer` 保证 QML 单例与 C++ 内部引用同一对象，避免出现两个实例导致信号失效。`Logger` 使用 `qInstallMessageHandler` 拦截所有 Qt 日志输出，业务代码用 `qDebug() << "[模块]" ...` 即可同时输出到控制台与按日期命名的日志文件。

---

## 5. 协议错误处理流（Stage 4 收尾引入）

```mermaid
flowchart LR
    subgraph "协议层"
        fc3[FrameCodec<br/>feed 状态机]
        proto[maxPayloadForType<br/>控制帧 1MB / 数据帧 256MB]
    end

    subgraph "领域层"
        wkr[FileSender / ReceiverWorker]
        cleanup[cleanup<br/>关 socket / 删半文件]
    end

    subgraph "应用逻辑层"
        tsm3[TransferSessionManager]
        rec[SessionRecord<br/>errorCode / errorMsg]
    end

    subgraph "QML 表现层"
        errdlg[ErrorDialog<br/>针对性提示]
    end

    fc3 -->|按 Type 校验 Payload 上限| proto
    proto -.->|超限 / 格式错误| fc3
    fc3 -.->|errorOccurred ErrorCode msg| wkr
    wkr -->|cleanup| cleanup
    wkr -.->|transferFinished false errorCode msg| tsm3
    tsm3 -->|errorCode = static_cast quint16| rec
    tsm3 -.->|errorOccurred msg| errdlg

    errdlg2[针对性提示表]
    errdlg -.->|UserCancelled 5002| errdlg2
    errdlg -.->|Sha256Mismatch 4003| errdlg2
    errdlg -.->|FrameTooLarge 2002| errdlg2
    errdlg -.->|ProtocolMismatch 3001| errdlg2
```

错误码分级（V1.0 协议错误码体系）：

| 段位 | 类别 | 典型错误 |
|:---|:---|:---|
| 1xxx | 连接错误 | 1001 ConnectionTimeout / 1002 ConnectionLost / 1003 TransferTimeout |
| 2xxx | 帧格式错误 | 2001 InvalidFrame / 2002 FrameTooLarge / 2003 InvalidPayload |
| 3xxx | 协议契约错误 | 3001 ProtocolMismatch / 3002 InvalidFileName / 3003 InvalidFilePath / 3004 FileListMismatch |
| 4xxx | 磁盘 I/O 错误 | 4001 DiskWriteFailed / 4002 DiskSpaceInsufficient / 4003 Sha256Mismatch |
| 5xxx | 用户行为 | 5001 UserRejected / 5002 UserCancelled |
| 9xxx | 兜底 | 9999 UnknownError |
