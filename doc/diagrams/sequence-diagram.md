# GridYard C++↔QML 通信序列图

```mermaid
sequenceDiagram
    participant main as main.cpp
    participant engine as QQmlApplicationEngine
    participant app as AppController
    participant qml as Main.qml

    Note over main: 程序启动
    main->>main: QGuiApplication 初始化
    main->>main: qRegisterMetaType PeerInfo
    main->>engine: loadFromModule("cqnu.gridyard.client", "Main")

    engine->>moc: 查找 QML_SINGLETON 类型
    moc-->>engine: AppController 元信息
    engine->>app: create() 工厂方法
    app-->>engine: AppController 实例

    engine->>qml: 加载 Main.qml
    qml->>engine: 请求 AppController
    engine-->>qml: 返回单例实例

    Note over qml: QML 绑定属性
    qml->>app: 读取 applicationName
    app-->>qml: "GridYard"
    qml->>app: 读取 applicationVersion
    app-->>qml: "0.1.0"

    Note over qml: 用户关闭窗口
    qml->>app: quit()
    app->>main: QCoreApplication::quit()
```

## 通信方式

| 方向 | 方式 | 示例 |
|:-----|:-----|:-----|
| C++ → QML | Q_PROPERTY 属性 | AppController.applicationName |
| QML → C++ | Q_INVOKABLE 方法 | AppController.quit() |
| C++ → QML | 信号（signals） | appReady() 信号 |
