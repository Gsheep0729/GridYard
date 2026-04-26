# GridYard 类图（Stage 0 当前状态）

```mermaid
classDiagram
    %% 共享层（shared/）
    class ProtocolConstants {
        <<gy::protocol>>
        +kHeaderBytes : quint32 = 8
        +kDefaultDiscoveryPort : quint16 = 45678
        +kDefaultP2pPort : quint16 = 35100
    }

    class PeerInfo {
        <<Q_GADGET>>
        +deviceId : QString
        +deviceName : QString
        +ipAddress : QString
        +tcpPort : quint16
    }

    class FrameCodec {
        <<QObject>>
        -_buffer : QByteArray
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

- **ProtocolConstants**：`gy::protocol` 命名空间中的协议常量（kHeaderBytes、端口等）
- **PeerInfo**：Q_GADGET 值类型，用于 QML 端通过 Q_PROPERTY MEMBER 访问
- **FrameCodec**：TLV 帧编解码器，Stage 0 为空实现
- **AppController**：QML_SINGLETON 单例，QML 与 C++ 通信的桥梁
- **Main.qml**：根窗口，通过 AppController 访问 C++ 功能
