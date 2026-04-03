# integration_demo — C++ ↔ QML 集成最小示例

本目录是配合《GridYard——Qt6 + QML 代码规范》§七 的可编译运行示例。
代码体量刻意保持最小，但覆盖了规范要求的全部红线模式。

## 演示了什么

| 规范条款           | 在哪里看                                                                |
| :----------------- | :---------------------------------------------------------------------- |
| §七.1 QML_ELEMENT  | `core/discovery_stub.h` 第一段宏（`Q_OBJECT` + `QML_ELEMENT`）          |
| §七.1 Q_PROPERTY 三件套 | `core/discovery_stub.h` 的 `peers` property（READ + NOTIFY）        |
| §七.1 Q_INVOKABLE  | `core/discovery_stub.h` 的 `addPeer()` / `removeAllPeers()`            |
| §七.1 Q_GADGET     | `core/peer_info.h` 整文件（值类型，可以塞进 QVariantList）              |
| §七.3 QML_SINGLETON | `core/app_controller.h` + `Main.qml` 顶部 import 后直接用 `AppController.xxx` |
| §七.3 禁止 setContextProperty | `main.cpp` 全程没出现一次 setContextProperty                  |
| §七.4 数据流单向   | QML 通过 `discovery.addPeer(...)` 调 C++；C++ 通过 signal/property 反向通知 |
| §二.3 qt_add_qml_module | `core/CMakeLists.txt`、`ui/CMakeLists.txt` 各一个               |
| §三.1 三段式结构   | `Main.qml` / `DeviceCard.qml`                                           |
| §四.4 命名契约     | 顶层 `tw_mainWindow` / `tw_deviceCard` id                              |
| §五.2 required property | `DeviceCard.qml` 全部 4 个 property                              |
| §五.4 binding 优先 | `Main.qml` 的 ListView model、Text color 等都用 binding                |
| §六.5 method 显式类型 | `Main.qml` 的 `function makeFakePeer()`                              |
| §十二 Doxygen 文件头 | 每个 `.h` / `.cpp` / `.qml` 文件最顶部                                |
| §十.7 RAII 锁     | `DiscoveryStub::addPeer()` 内部使用 `QMutexLocker`                     |

## 构建与运行（Manjaro Linux / GCC 15 / Qt 6.5+）

```bash
# 在本目录下
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/bin/integration_demo
```

预期看到：

- 标题为 **GridYard Integration Demo v1.0** 的窗口
- 中间一个空列表 + 底部两个按钮 `添加伪造设备` / `清空全部`
- 点击 `添加伪造设备` 后列表追加一张设备卡片（C++ 端生成 PeerInfo，QML 端 binding 自动刷新）
- 卡片点击会在终端 `qDebug` 输出 `cardClicked: <id>`（QML→C++ 路径）
- 关闭窗口会触发 `AppController.quit()`（QML→C++ 单例方法调用）

## 文件结构

```
integration_demo/
├── README.md
├── CMakeLists.txt              # 根，含 qt_standard_project_setup
├── main.cpp                    # 入口；engine.loadFromModule()
├── core/                       # C++ 业务层
│   ├── CMakeLists.txt          # qt_add_qml_module（仅 C++）
│   ├── app_controller.{h,cpp}  # QML_SINGLETON
│   ├── peer_info.h             # Q_GADGET 值类型
│   └── discovery_stub.{h,cpp}  # QML_ELEMENT
└── ui/                         # QML 视图层
    ├── CMakeLists.txt          # qt_add_qml_module（仅 .qml）
    ├── Main.qml                # 根 window，tw_mainWindow
    └── DeviceCard.qml          # 列表项 delegate，全 required property
```

## 学习路径

第一次阅读，按以下顺序看一遍即可建立完整心智模型：

1. `main.cpp`：看程序怎么起来
2. `core/app_controller.h`：看单例长什么样
3. `core/peer_info.h`：看一个值类型的最小骨架
4. `core/discovery_stub.h`：看一个普通可创建类型，含信号 / 槽 / property
5. `ui/Main.qml`：看 QML 怎么用上面三种 C++ 类型
6. `ui/DeviceCard.qml`：看一个标准 delegate 的写法
7. `core/CMakeLists.txt` + `ui/CMakeLists.txt`：看模块怎么注册

每个文件不超过 80 行；总代码量 ~400 行，可一次性读完。
