# GridYard 分层类图（v4.14.0 当前架构）

当前客户端以表现层、应用逻辑层、领域层和数据管理层为目标。为避免所有类挤在一张图中，本文件按职责拆成多张类图。

## 1. 应用逻辑层与上下层边界

```mermaid
classDiagram
    direction LR

    class AppController {
        <<QObject, QML_SINGLETON>>
        -ConfigManager* _config
        -DiscoveryService* _discovery
        -P2pServer* _p2pServer
        -TransferSessionManager* _transfer
        +create(engine, scriptEngine)$ AppController*
        +applicationName() QString
        +applicationVersion() QString
        +discovery() DiscoveryService*
        +transfer() TransferSessionManager*
        +quit()
    }

    class ConfigManager {
        <<QObject, QML_SINGLETON>>
        +deviceId : QString
        +deviceName : QString
        +localIp : QString
        +receivePath : QString
        +autoAcceptFiles : bool
        +tcpPort : quint16
        +setDeviceName(name)
        +setReceivePath(path)
        +setAutoAcceptFiles(enabled)
        +setTcpPort(port)
        +openFolder(path)
    }

    class DiscoveryService {
        <<QObject, QML_ANONYMOUS>>
        +peers : QVariantList
        +peerInfo(deviceId) PeerInfo
        +refresh()
        +peersChanged() signal
    }

    class P2pServer {
        <<QObject>>
        +start() bool
        +stop()
        +isListening() bool
        +transferRequestReceived(worker, senderDeviceId, senderName, fileName, fileSize, totalFiles, totalBytes) signal
    }

    class TransferSessionManager {
        <<QObject, exposed by AppController>>
        +sessions : QVariantList
        +init(config, discovery, p2pServer)
        +createSendSession(deviceId, filePath)
        +acceptReceiveSession(sessionId)
        +rejectReceiveSession(sessionId)
        +cancelSession(sessionId)
        +removeSession(sessionId)
        +removeSessionAndDeleteFile(sessionId)
        +clearFinishedSessions(deleteReceivedFiles)
        +receiveRequestReceived(sessionId, senderDeviceId, senderName, fileName, fileSize, totalFiles, totalBytes, isDirectory, fileList) signal
    }

    class MainQml {
        <<QML ApplicationWindow>>
        -_targetDeviceId : string
        -_targetDeviceName : string
        -_targetIpAddress : string
        -_targetIsOnline : bool
        +selectDevice(deviceId, deviceName, ipAddress, isOnline)
        +refreshSelectedDevice()
    }

    AppController *-- DiscoveryService : 创建并持有
    AppController *-- P2pServer : 创建并持有
    AppController *-- TransferSessionManager : 创建并持有
    AppController --> ConfigManager : 获取全局实例
    AppController --> TransferSessionManager : 注入三个依赖
    TransferSessionManager --> ConfigManager
    TransferSessionManager --> DiscoveryService
    TransferSessionManager --> P2pServer
    MainQml --> AppController : 状态与操作入口
    MainQml --> ConfigManager : 设置与打开目录
```

`AppController` 是客户端组装点。`TransferSessionManager` 不是 QML 单例，而是由 `AppController.transfer` 暴露给页面。

## 2. 领域层：共享数据与设备发现

```mermaid
classDiagram
    direction LR

    class ProtocolConstants {
        <<gy::protocol>>
        +kHeaderBytes : quint32
        +kDefaultDiscoveryPort : quint16
        +kDefaultP2pPort : quint16
        +kTypeHello : quint32
        +kTypeTransferReq : quint32
        +kTypeTransferRsp : quint32
        +kTypeDataChunk : quint32
        +kTypeChunkAck : quint32
        +kTypeTransferDone : quint32
        +kTypeCancel : quint32
    }

    class PeerInfo {
        <<Q_GADGET>>
        +deviceId : QString
        +deviceName : QString
        +ipAddress : QString
        +tcpPort : quint16
        +isOnline : bool
        +lastSeen : QDateTime
        +lastSeenStr() QString
    }

    class FileEntry {
        <<Q_GADGET>>
        +relativePath : QString
        +fileSize : qint64
        +sha256 : QString
    }

    class TransferSession {
        <<Q_GADGET, shared value type>>
        +sessionId : QString
        +peerDeviceId : QString
        +fileCount : int
        +totalBytes : qint64
        +sentBytes : qint64
        +isActive : bool
    }

    class FrameCodec {
        <<QObject>>
        -State _state
        -QByteArray _buffer
        -quint32 _pendingType
        -quint32 _pendingLength
        +encode(type, payload)$ QByteArray
        +feed(data)
        +frameReady(type, payload) signal
    }

    class ConfigManager {
        <<QObject, QML_SINGLETON>>
        -QString _deviceId
        -QString _deviceName
        -QString _localIp
        -QString _receivePath
        -bool _autoAcceptFiles
        -quint16 _tcpPort
        +create(engine, scriptEngine)$ ConfigManager*
        +refreshLocalIp()
        +openFolder(path)
    }

    class DiscoveryService {
        <<QObject>>
        -ConfigManager* _config
        -QUdpSocket* _socket
        -QHash~QString, PeerInfo~ _peers
        +peers() QVariantList
        +peerInfo(deviceId) PeerInfo
        +refresh()
        -sendHelloPacket()
        -handleHelloPacket(json, sender)
        -pruneOfflineNodes()
    }

    DiscoveryService --> ConfigManager : 读取本机身份和端口
    DiscoveryService o-- PeerInfo : 维护在线设备表
    DiscoveryService ..> ProtocolConstants : Hello 协议与发现端口
    FrameCodec ..> ProtocolConstants : TLV 类型和帧头
```

`FileEntry` 和 `TransferSession` 是共享值类型。当前运行中的界面会话由 `TransferSessionManager` 内部的 `QVariantMap` 列表维护，尚未直接使用共享层 `TransferSession`。

## 3. 领域层：文件传输运行时

```mermaid
classDiagram
    direction LR

    class TransferSessionManager {
        <<QObject>>
        -QList~QVariantMap~ _sessions
        -QHash~QString, FileSenderWorker*~ _sendWorkers
        +sessions() QVariantList
        +createSendSession(deviceId, filePath)
        +acceptReceiveSession(sessionId)
        +rejectReceiveSession(sessionId)
        +cancelSession(sessionId)
        +removeSession(sessionId)
        +removeSessionAndDeleteFile(sessionId)
        +clearFinishedSessions(deleteReceivedFiles)
        -onTransferRequestReceived(worker, senderDeviceId, senderName, fileName, fileSize, totalFiles, totalBytes)
        +sessionsChanged() signal
        +transferCompleted(sessionId, fileName, filePath) signal
    }

    class SessionRecord {
        <<QVariantMap runtime record>>
        +sessionId : QString
        +type : send or receive
        +deviceId : QString
        +peerDeviceName : QString
        +fileName : QString
        +status : QString
        +progress : int
        +bytesTransferred : qint64
        +totalBytes : qint64
        +createdAt : QString
        +isDirectory : bool
        +fileList : QVariantList
        +localPath : QString
        +canDeleteLocalFile : bool
    }

    class P2pServer {
        <<QObject>>
        -ConfigManager* _config
        -QTcpServer* _server
        +start() bool
        +stop()
        -onNewConnection()
        +transferRequestReceived(worker, senderDeviceId, senderName, fileName, fileSize, totalFiles, totalBytes) signal
    }

    class FileSenderWorker {
        <<QObject, Worker-Object>>
        -QTcpSocket* _socket
        -FrameCodec* _codec
        -QString _senderDeviceId
        -QString _senderName
        -QList~FileItem~ _fileList
        -QString _rootName
        -QStringList _emptyDirectories
        -bool _isDirectory
        +startTransfer(host, port, path, senderDeviceId, senderName)
        +cancel()
        -sendTransferRequest()
        -sendNextChunk()
        +progressChanged(bytesSent, totalBytes) signal
        +transferFinished(success, errorMsg) signal
    }

    class FileReceiverWorker {
        <<QObject, Worker-Object>>
        -QTcpSocket* _socket
        -FrameCodec* _codec
        -QString _senderDeviceId
        -QString _senderName
        -QList~FileItem~ _fileList
        -QString _receivePath
        -QString _destinationRoot
        -QStringList _emptyDirectories
        -bool _isDirectory
        +acceptTransfer()
        +rejectTransfer(reason)
        +setReceivePath(path)
        +savedPath() QString
        -handleTransferRequest(payload)
        -handleDataChunk(payload)
        +transferRequestReceived(senderDeviceId, senderName, fileName, fileSize, totalFiles, totalBytes) signal
        +progressChanged(bytesReceived, totalBytes) signal
        +transferFinished(success, errorMsg) signal
    }

    class DirSerializer {
        <<gy utility>>
        +serialize(path)$ QList~FileItem~
        +computeSha256(filePath)$ QString
    }

    class FileItem {
        <<gy value type>>
        +relativePath : QString
        +sizeBytes : qint64
        +sha256 : QString
    }

    class FrameCodec {
        <<QObject>>
        +encode(type, payload)$ QByteArray
        +feed(data)
        +frameReady(type, payload) signal
    }

    TransferSessionManager o-- SessionRecord : 维护发送与接收会话
    TransferSessionManager --> FileSenderWorker : 为发送会话创建线程和 Worker
    TransferSessionManager --> FileReceiverWorker : 接受、拒绝和取消
    TransferSessionManager --> P2pServer : 接收入站请求
    P2pServer --> FileReceiverWorker : 每个入站连接创建一个
    FileSenderWorker --> DirSerializer : 枚举文件并计算摘要
    DirSerializer --> FileItem : 生成
    FileSenderWorker o-- FileItem : 发送列表
    FileReceiverWorker o-- FileItem : 接收列表
    FileSenderWorker *-- FrameCodec
    FileReceiverWorker *-- FrameCodec
```

发送请求携带 `senderDeviceId`、当前设备别名、顶层目录名和空目录列表。接收端把 `senderDeviceId` 保存为运行时会话的 `deviceId`，并在配置的接收目录下创建唯一目标路径，因此设备改名、出现同名设备或接收重名文件夹时，任务归属和落盘结果仍然明确。

## 4. QML 表现层组件

```mermaid
classDiagram
    direction TB

    class MainQml {
        <<QML ApplicationWindow>>
        -_targetDeviceId : string
        +selectDevice(deviceId, deviceName, ipAddress, isOnline)
        +refreshSelectedDevice()
    }

    class PeerListView {
        <<QML Frame>>
        +selectedDeviceId : string
        +deviceSelected(deviceId, deviceName, ipAddress, isOnline) signal
        +fileDropped(deviceId, filePath) signal
    }

    class DeviceCard {
        <<QML ItemDelegate>>
        +deviceId : string
        +deviceName : string
        +ipAddress : string
        +isOnline : bool
        +isSelected : bool
        +cardClicked(deviceId, deviceName, ipAddress, isOnline) signal
        +fileDropped(deviceId, filePath) signal
    }

    class DeviceSessionView {
        <<QML Frame>>
        +deviceId : string
        +deviceName : string
        +ipAddress : string
        +isOnline : bool
        +filteredCount : int
        +finishedCount : int
        +expandedSessions : var
        +isSessionExpanded(sessionId) bool
        +setSessionExpanded(sessionId, expanded)
        +sendFileRequested() signal
        +sendFolderRequested() signal
        +fileDropped(filePath) signal
    }

    class TransferTaskCard {
        <<QML Frame>>
        +sessionId : string
        +taskType : string
        +taskName : string
        +status : string
        +progress : int
        +bytesTransferred : var
        +totalBytes : var
        +createdAt : string
        +peerDeviceName : string
        +isDirectory : bool
        +fileList : var
        +expanded : bool
        +canDeleteLocalFile : bool
        +expansionRequested(expanded) signal
    }

    class FileTypeIcon {
        <<QML Image>>
        +fileName : string
        +isDirectory : bool
        +iconSource : url
    }

    class FormatUtils {
        <<JavaScript .pragma library>>
        +formatBytes(bytes) string
        +formatTime(timeString) string
    }

    class SettingsDialog {
        <<QML Dialog>>
        -_tempDeviceName : string
        -_tempReceivePath : string
        -_tempAutoAcceptFiles : bool
        -_tempTcpPort : int
        -_isValid : bool
        -_isDirty : bool
    }

    class AcceptDialog {
        <<QML Dialog>>
        +sessionId : string
        +senderName : string
        +fileName : string
        +fileSize : var
        +totalFiles : int
        +totalBytes : var
        +isDirectory : bool
        +fileList : var
    }

    class AppController {
        <<C++ QML_SINGLETON>>
        +discovery : DiscoveryService*
        +transfer : TransferSessionManager*
    }

    class ConfigManager {
        <<C++ QML_SINGLETON>>
        +deviceName : QString
        +receivePath : QString
        +autoAcceptFiles : bool
        +tcpPort : quint16
    }

    MainQml *-- PeerListView
    MainQml *-- DeviceSessionView
    MainQml *-- SettingsDialog
    MainQml *-- AcceptDialog
    PeerListView *-- DeviceCard
    DeviceSessionView *-- TransferTaskCard
    TransferTaskCard *-- FileTypeIcon
    TransferTaskCard --> FormatUtils : 格式化大小和时间
    AcceptDialog --> FormatUtils : 格式化文件大小
    PeerListView --> AppController : 绑定 discovery.peers
    DeviceSessionView --> AppController : 按 deviceId 筛选 transfer.sessions
    AcceptDialog --> AppController : 接受或拒绝接收会话
    SettingsDialog --> ConfigManager : 读取并保存配置
```

主窗口只保存当前选中设备的信息。左侧 `PeerListView` 负责选择设备，右侧 `DeviceSessionView` 根据稳定的 `deviceId` 筛选会话记录。`FileTypeIcon` 根据文件名和目录标记选择项目内置 SVG，`FormatUtils` 只提供无状态展示格式化，动画继续由 QML 声明。

## 5. 数据管理层现状

```mermaid
classDiagram
    direction LR

    class ConfigManager {
        <<current data management>>
        -QString _deviceId
        -QString _deviceName
        -QString _receivePath
        -bool _autoAcceptFiles
        -quint16 _tcpPort
        +setDeviceName(name)
        +setReceivePath(path)
        +setAutoAcceptFiles(enabled)
        +setTcpPort(port)
        +openFolder(path)
    }

    class Logger {
        <<current data management>>
        -QFile _logFile
        -QTextStream _stream
        -QString _logDir
        +instance()$ Logger*
        +init(logDir)
        +logFilePath() QString
        -writeLog(type, formatted)
    }

    class QSettings {
        <<Qt persistence>>
    }

    class FileSystem {
        <<external storage>>
    }

    class NetworkConnection {
        <<external data source>>
    }

    class FileSenderWorker {
        <<domain service with I/O>>
    }

    class FileReceiverWorker {
        <<domain service with I/O>>
    }

    ConfigManager --> QSettings : 读写配置
    ConfigManager --> FileSystem : 创建和打开接收目录
    Logger --> FileSystem : 写入日志
    FileSenderWorker --> FileSystem : 读取发送文件
    FileReceiverWorker --> FileSystem : 写入接收文件
    FileSenderWorker --> NetworkConnection : 发送数据
    FileReceiverWorker --> NetworkConnection : 接收数据
```

当前数据管理职责已经存在，但没有形成独立的 `data/` 模块。配置和日志相对独立，文件与网络访问仍和传输领域过程混在 Worker 中，因此只能算基本符合四层要求。

## 6. 分层关系小结

```text
表现层
  -> 应用逻辑层
  -> 领域层
  -> 数据管理层与外部资源
```

- QML 负责页面、交互和状态展示，不直接操作网络与文件系统。
- `AppController` 负责组装后端对象，并作为 QML 访问主要业务能力的入口。
- `TransferSessionManager` 负责会话生命周期和界面状态，不直接执行文件收发。
- Worker 负责 TCP 收发、文件读写、超时和摘要校验。
- `FrameCodec` 与协议常量位于共享层，可供后续服务端复用。
- 数据管理层尚未完全独立，后续持久化功能应通过仓储接口接入应用逻辑层。
