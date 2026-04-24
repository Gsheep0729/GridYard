# GridYard 类图（Stage 1 当前状态）

```mermaid
classDiagram
    %% 共享层（shared/）
    class ProtocolConstants {
        <<gy::protocol>>
        +kHeaderBytes : quint32 = 8
        +kDefaultDiscoveryPort : quint16 = 45678
        +kDefaultP2pPort : quint16 = 35100
        +kTypeHello : quint32 = 0x0001
        +kTypeTransferReq : quint32 = 0x0101
        +kTypeTransferRsp : quint32 = 0x0102
        +kTypeDataChunk : quint32 = 0x0201
        +kTypeChunkAck : quint32 = 0x0301
        +kTypeTransferDone : quint32 = 0x0302
        +kTypeCancel : quint32 = 0x0401
    }

    class PeerInfo {
        <<Q_GADGET>>
        +deviceId : QString
        +deviceName : QString
        +ipAddress : QString
        +tcpPort : quint16
        +isOnline : bool
        +lastSeen : QDateTime
        +lastSeenStr() : QString
    }

    class FileEntry {
        <<Q_GADGET>>
        +relativePath : QString
        +fileSize : qint64
        +sha256 : QString
    }

    class TransferSession {
        <<Q_GADGET>>
        +sessionId : QString
        +peerDeviceId : QString
        +fileCount : int
        +totalBytes : qint64
        +sentBytes : qint64
        +isActive : bool
    }

    class FrameCodec {
        <<QObject>>
        -_state : State
        -_buffer : QByteArray
        -_pendingType : quint32
        -_pendingLength : quint32
        +encode(type: quint32, payload: QByteArray)$ QByteArray
        +feed(data: QByteArray) : void
        +frameReady(type: quint32, payload: QByteArray) signal
    }

    %% 客户端层（client/）
    class AppController {
        <<QObject, QML_SINGLETON>>
        +applicationName : QString
        +applicationVersion : QString
        +create(engine: QQmlEngine, scriptEngine: QJSEngine)$ AppController*
        +quit() : void
        +test() : void
        +appReady() signal
    }

    class Main {
        <<QML>>
        +tw_mainWindow : ApplicationWindow
    }

    %% 关系
    FrameCodec ..> ProtocolConstants : 使用常量
    Main --> AppController : 访问属性/调用方法
```

## 说明

- **ProtocolConstants**：`gy::protocol` 命名空间中的协议常量（7 个 Type 码 + 端口 + 帧头长度）
- **PeerInfo**：Q_GADGET 值类型，局域网在线节点描述
- **FileEntry**：Q_GADGET 值类型，文件元数据（Stage 1 新增）
- **TransferSession**：Q_GADGET 值类型，传输会话状态（Stage 1 新增）
- **FrameCodec**：TLV 帧编解码器，含粘包/半包状态机（Stage 1 实现）
- **AppController**：QML_SINGLETON 单例，QML 与 C++ 通信的桥梁
- **Main.qml**：根窗口，通过 AppController 访问 C++ 功能
