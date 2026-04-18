# GridYard 数据流图（Stage 0 当前状态）

```mermaid
flowchart LR
    subgraph "QML 层"
        UI["Main.qml<br/>用户界面"]
    end

    subgraph "C++ 业务层"
        AC["AppController<br/>全局控制器"]
    end

    subgraph "C++ 网络层"
        FC["FrameCodec<br/>TLV 编解码器"]
        NET["网络模块<br/>(Stage 2/3)"]
    end

    subgraph "共享层"
        PT["protocol.h<br/>协议常量"]
        DT["data_types.h<br/>数据结构"]
    end

    UI <-->|Q_PROPERTY / Q_INVOKABLE| AC
    AC <-->|调用| FC
    FC <-->|编码/解码| NET
    FC -->|使用| PT
    AC -->|使用| DT
```

## 数据流向说明

1. **QML ↔ AppController**：通过 Q_PROPERTY 读属性、Q_INVOKABLE 调方法
2. **AppController ↔ FrameCodec**：业务层调用编解码器
3. **FrameCodec ↔ 网络模块**：编码后发送、接收后解码（Stage 2/3 实现）
4. **FrameCodec → protocol.h**：使用协议常量（Type 码等）
5. **AppController → data_types.h**：使用数据结构（PeerInfo 等）
