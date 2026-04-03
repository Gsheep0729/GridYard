# 鸽邮 (GridYard) — 团队开发者手册

---

| 字段       | 内容                                          |
| :--------- | :-------------------------------------------- |
| 文档编号   | GY-DEV-001                                    |
| 文档版本   | v1.1                                          |
| 创建日期   | 2026-05-24                                    |
| 适用范围   | V1.0 主干开发期；V2.0 演进期增量更新          |
| 目标读者   | 高扬 / 杜若贤 / 冯春霖（团队成员）             |
| 配套文档   | 《V1.0 架构设计与 V2.0 演进说明书》（GY-SPEC-001）<br>《GridYard——Qt6 + QML 代码规范》（GY-CS-002）<br>《鸽邮(GridYard)——软件开发阶段计划》（GY-PLAN-001） |
| 文档定位   | 工程实践规范，不重复设计文档已有的架构推导     |

---

## 文档定位与边界

本手册回答 **"我们怎么一起把鸽邮写出来"** 这一类工程问题，**不**重复《架构设计说明书》已经讲过的"为什么这样设计"，也**不**承担"分阶段做什么"的规划职责（那是《软件开发阶段计划》的事）。如需查阅：

| 想知道                   | 看哪份文档                                |
| :----------------------- | :---------------------------------------- |
| 为什么用 P2P + C/S 混合？ | 设计说明书 §1、§10                        |
| TLV 协议帧怎么定义？     | 设计说明书 §7                             |
| Type 码完整列表？        | 设计说明书 §7.2、§10.5                    |
| Worker Object 为什么必要？ | 设计说明书 §8.3、§6.3.3 原则一            |
| 现在该做什么、做到哪一步？ | 《软件开发阶段计划》全文                  |
| 代码每一行怎么写才合规？ | 《GridYard——Qt6 + QML 代码规范》全文     |
| **怎么把环境搭起来？**   | **本手册 §2**                             |
| **怎么提交代码？**       | **本手册 §6**                             |
| **新模块从哪儿动手？**   | **本手册 §4、§5**                         |
| **怎么联调与排错？**     | **本手册 §7、§8**                         |

---

## 目录

1. [开发流程鸟瞰](#1-开发流程鸟瞰)
2. [开发环境搭建](#2-开发环境搭建)
3. [工程结构与构建](#3-工程结构与构建)
4. [代码规范（强约束）](#4-代码规范强约束)
5. [模块开发样板](#5-模块开发样板)
6. [Git 协作流程](#6-git-协作流程)
7. [调试与排错](#7-调试与排错)
8. [测试与验收](#8-测试与验收)
9. [发布与部署](#9-发布与部署)
10. [常见问题 FAQ](#10-常见问题-faq)

---

## 1. 开发流程鸟瞰

```
   阶段计划（已就绪）                  团队成员
        │                                  │
        ▼                                  ▼
   ┌────────────┐    认领阶段内任务    ┌────────────┐
   │ 阶段计划    │ ───────────────────► │  feature   │
   │ 必要/加分项 │                      │  分支开发   │
   └────────────┘                      └─────┬──────┘
                                              │
                                       本地构建/自测
                                              │
                                              ▼
                                       ┌────────────┐
                                       │   PR /     │
                                       │  Code Review│
                                       └─────┬──────┘
                                              │
                                       合并 dev 分支
                                              │
                                              ▼
                                       阶段验收
                                              │
                                              ▼
                                       合并 release + 打 tag
```

**协作单位**：以"阶段内的任务条目"为最小协作单位（见《软件开发阶段计划》）。每个任务对应一个 feature 分支、一个 PR、一次 Code Review。**不固定分工**——任务发布在群里，谁感兴趣谁认领，没人接手再由 Tech Lead 兜底安排。

**节奏**：保持小步快跑，单次 PR 控制在 300 行变更以内；功能不完整时也可以先提交占位空实现，让其他成员能继续依赖你的接口往下走。

---

## 2. 开发环境搭建

### 2.1 强制环境矩阵

| 项目         | 版本                                       | 备注                                         |
| :----------- | :----------------------------------------- | :------------------------------------------- |
| 操作系统     | Manjaro Linux（rolling）                   | 全员统一，避免跨平台编译差异                 |
| C++ 编译器   | GCC 15+，支持 C++23                        | `g++ --version` 验证                         |
| 构建系统     | CMake 4.2.3+                               | `cmake --version` 验证                       |
| Qt           | Qt 6.5+（Quick / QuickControls2 / Network / Sql / Test 模块） | 通过 `pamac install qt6-base qt6-tools qt6-declarative` 安装 |
| Qt Creator   | 13.x（可选 IDE）                           | UI / QML 设计必备                             |
| Git          | 2.40+                                      | —                                            |
| PostgreSQL   | 15+（**仅 V2.0 阶段需要**）                | V1.0 阶段可暂不安装                          |
| libpqxx      | 7.x（**仅 V2.0 阶段需要**）                | PostgreSQL 的 C++ 客户端库                   |

### 2.2 一次性安装命令（Manjaro）

```bash
# 基础工具链
sudo pacman -S base-devel cmake git ninja gdb

# Qt 6
sudo pacman -S qt6-base qt6-tools qt6-declarative

# Qt Creator（推荐 IDE）
sudo pacman -S qtcreator

# V2.0 阶段再装
# sudo pacman -S postgresql libpqxx
```

### 2.3 验证环境就绪

把下列命令的输出贴进群里互相确认一次，避免后续因版本差异扯皮：

```bash
g++ --version           # >= 15
cmake --version         # >= 4.2.3
qmake6 --version        # Qt 6.5+
git --version           # >= 2.40
```

### 2.4 IDE 配置建议

- **Qt Creator**：开箱即用，UI 与 QML 文件双击直接进 Designer。推荐配置：
  - `工具 -> 选项 -> C++ -> 代码风格` 导入团队代码风格（见 §4）
  - `工具 -> 选项 -> 版本控制 -> Git` 设置统一的提交模板
- **VS Code**：可选。需安装 `C/C++`、`CMake Tools`、`Qt for VS Code` 三个扩展。
- **CLion**：可选。原生支持 CMake，但需购买授权或使用学生免费版。

---

## 3. 工程结构与构建

### 3.1 目标目录结构

> **总原则**：**全部代码归位 `src/` 下**（C++ 工程通用规范），根目录只放配置 / 文档 / 脚本，提升结构鲁棒性。

```
GridYard/
├── CMakeLists.txt              # 顶层构建入口（仅 add_subdirectory(src)）
├── README.md                   # 项目入口（含构建命令与快速导航）
├── .gitignore
├── doc/                        # 文档（设计书 + 本手册 + 阶段计划 + 代码规范）
├── scripts/                    # 辅助脚本（for_md.py）
└── src/                        # 全部源代码
    ├── CMakeLists.txt          # add_subdirectory(shared) + (client) + ...
    ├── shared/                 # 客户端/服务端共用代码
    │   ├── CMakeLists.txt
    │   ├── protocol.h          # TLV 帧 + Type 码 + Payload 字段常量
    │   ├── data_types.h        # PeerInfo / FileEntry / TransferSession 等 POD
    │   └── frame_codec.{h,cpp} # TLV 帧编解码（含粘包状态机）
    ├── client/                 # 桌面客户端（QML + C++）
    │   ├── CMakeLists.txt
    │   ├── main.cpp
    │   ├── Main.qml            # 根 QML（loadFromModule 入口）
    │   ├── core/               # 业务逻辑层（C++）
    │   │   ├── app_controller.{h,cpp}            # QML_SINGLETON
    │   │   ├── config_manager.{h,cpp}
    │   │   ├── transfer_session_manager.{h,cpp}
    │   │   └── dir_serializer.{h,cpp}
    │   ├── network/            # 网络层（C++，QML_ELEMENT）
    │   │   ├── discovery_service.{h,cpp}
    │   │   ├── p2p_server.{h,cpp}
    │   │   ├── file_sender_worker.{h,cpp}
    │   │   └── file_receiver_worker.{h,cpp}
    │   └── ui/                 # QML 视图层
    │       ├── PeerListView.qml
    │       ├── DeviceCard.qml
    │       ├── TransferPanel.qml
    │       └── AcceptDialog.qml
    ├── server/                 # V2.0 才填充
    │   └── .gitkeep
    └── tests/                  # 单元测试（Qt Test）
        ├── CMakeLists.txt
        ├── test_frame_codec.cpp
        └── test_dir_serializer.cpp
```

> **现状**：Stage 0 骨架已建出（含 `src/shared/` + `src/client/` 占位文件 + `src/server/.gitkeep` + `src/tests/.gitkeep`）。后续阶段在 `src/` 下对应子目录追加文件。

### 3.2 顶层 CMakeLists.txt 范式

参照 `doc/templates/CMakeLists.qml.template`。骨架要点：

```cmake
cmake_minimum_required(VERSION 4.2.3)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
set(CMAKE_CXX_MODULE_STD ON)

project(GridYard VERSION 0.1 LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Quick Network)
qt_standard_project_setup(REQUIRES 6.10)

# 共享静态库
add_library(gy_shared STATIC
    shared/frame_codec.cpp
)
target_include_directories(gy_shared PUBLIC shared)
target_link_libraries(gy_shared PUBLIC Qt6::Core Qt6::Network)

add_subdirectory(client)
# add_subdirectory(server)   # V2.0 阶段解除注释
# add_subdirectory(tests)
```

### 3.3 构建与运行

```bash
# 首次配置（项目根目录执行）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 增量构建
cmake --build build -j

# 运行客户端（注意路径含 src/）
./build/src/client/appGridYard

# 跑全部单测
ctest --test-dir build --output-on-failure
```

> **常用别名**（写进各自的 `~/.bashrc` 或 `~/.zshrc`）：
> ```bash
> alias gyb='cmake --build build -j'
> alias gyr='./build/src/client/appGridYard'
> alias gyt='ctest --test-dir build --output-on-failure'
> ```

### 3.4 多机联调端口约定

V1.0 默认端口（写死在 `protocol.h` 或 `config_manager.cpp` 中作为缺省值）：

| 用途              | 端口  | 协议 | 说明                            |
| :---------------- | :---- | :--- | :------------------------------ |
| UDP 设备发现广播  | 45678 | UDP  | 全员一致，否则互相发现不到      |
| P2P 文件传输监听  | 35100 | TCP  | 可在配置中覆盖，仅本机生效      |
| C/S 服务端监听    | 36100 | TCP  | V2.0 才用                       |

---

## 4. 代码规范（强约束）

> **权威规范在独立文档**：所有命名、Qt6/QML 语法、C++23 约束、反模式清单的**最终权威定义**见同目录《GridYard——Qt6 + QML 代码规范.md》（文档号 GY-CS-002）。本节仅作**日常速查**与**与工程结构紧密耦合**的补充。任何冲突以独立规范为准。

### 4.1 命名规范速查（与代码规范一致，便于贴在显示器旁）

| 类别             | 规范                  | 反例（禁止）        | 正例                       |
| :--------------- | :-------------------- | :------------------ | :------------------------- |
| 类名             | PascalCase            | `file_sender`       | `FileSenderWorker`         |
| 函数/方法        | camelCase             | `Send_Hello()`      | `sendHello()`              |
| 成员变量         | `_` + camelCase       | `m_socket` / `socket_` | `_socket`                 |
| 局部变量         | camelCase             | `total_bytes`       | `totalBytes`               |
| 常量/枚举值      | `k` + PascalCase      | `CHUNK_SIZE`        | `kChunkSize`               |
| Widgets 控件 objectName | **`tw_` 前缀强制** | `btnSend`        | `tw_BtnSend`               |
| QML 顶层 id      | **`tw_` 前缀（首字母小写）** | `mainWindow` | `tw_mainWindow`            |
| 信号             | 动词过去分词/进行时   | `getProgress`       | `progressChanged(int)`     |
| 槽函数           | `on` + 动词           | `progressChanged`   | `onProgressChanged(int)`   |

### 4.2 头文件组织铁律

1. **每个 .cpp 配对一个同名 .h**，类的所有公开声明都写在 .h 中。
2. **头文件中禁止 `using namespace ...;`**，可在 .cpp 内局部 using。
3. **`#pragma once` 优先于 include guard**（团队已统一）：
   ```cpp
   #pragma once
   #include <QObject>
   ...
   ```
4. **include 顺序**（从近到远）：
   ```cpp
   #include "frame_codec.h"        // 1. 自身配对头文件
   
   #include "protocol.h"           // 2. 本项目 shared/
   #include "config_manager.h"     // 3. 本模块同层
   
   #include <QTcpSocket>           // 4. Qt 头文件
   #include <vector>               // 5. 标准库
   ```
5. **能用前置声明就不要 #include**：在 .h 中只声明指针/引用类型时，用 `class Foo;` 前置声明，把真正的 `#include "foo.h"` 放到 .cpp 里，减少编译耦合。

### 4.3 Qt 用法陷阱清单

| ❌ 反模式                                       | ✅ 应该这么写                              | 原因                                |
| :--------------------------------------------- | :----------------------------------------- | :---------------------------------- |
| `file.readAll()`                               | 循环 `file.read(kChunkSize)`               | 大文件直接 OOM                       |
| 后台线程直接调用 `_progressBar->setValue(...)`  | `emit progressChanged(percent)`            | 跨线程操作 QWidget 必崩             |
| `connect(a, SIGNAL(...), b, SLOT(...))`        | `connect(a, &A::sig, b, &B::slot)`         | 新式语法可编译期检查签名匹配         |
| 在含 Q_OBJECT 的源文件用 `import std;`         | 老老实实 `#include <vector>`               | MOC 不识别 Module                    |
| QML 用 `MouseArea`                             | 用 `TapHandler`                            | 老师禁用 MouseArea；课件中全用 TapHandler |
| `new QWidget(nullptr)` 然后忘记 delete         | `new QWidget(parent)` 让 Qt 对象树管理      | 内存泄漏                            |
| 信号槽用 `Qt::DirectConnection` 跨线程         | 默认 `Qt::AutoConnection` 即可             | Auto 会自动选择 Queued，更安全      |

### 4.4 注释原则

> 这条与团队整体精神一致：**默认不写注释**。

- 命名清晰的代码不需要注释，看不懂的命名才需要——**优先改命名**。
- 唯一需要注释的场合：**WHY 非显然**——如踩过的坑、隐藏约束、与协议规范的对应关系。
  ```cpp
  // 必须先 setSocketOption 再 bind，否则 Linux 上 SO_REUSEADDR 不生效
  socket->setSocketOption(QAbstractSocket::AddressReusable, 1);
  socket->bind(kDiscoveryPort);
  ```
- 禁止注释里写"什么时候做了什么"、"@author"、"//TODO: 张三 2025-01-01"——这些信息属于 Git 提交记录。

### 4.5 协议码的单一事实来源

`shared/protocol.h` 是**唯一**定义 Type 码的地方。其他模块**严禁**复制粘贴常量值：

```cpp
// ❌ 错误
constexpr quint32 HELLO_TYPE = 0x0001;  // 在 discovery_service.cpp 里又写一遍

// ✅ 正确
#include "protocol.h"
sendFrame(kTypeHello, payload);
```

---

## 5. 模块开发样板

每个核心模块都遵循同一套骨架，新成员对照样板填空即可。

### 5.1 Worker Object 模板（任何后台 I/O 模块的起点）

```cpp
// file_sender_worker.h
#pragma once
#include <QObject>
#include <QString>

class FileSenderWorker : public QObject {
    Q_OBJECT
public:
    explicit FileSenderWorker(QString filePath,
                              QString targetIp,
                              quint16 targetPort,
                              QObject *parent = nullptr);

public slots:
    void start();        // 由主线程通过 QueuedConnection 触发
    void cancel();       // 主线程取消传输请求

signals:
    void progressChanged(int percent);
    void transferFinished(bool ok, QString errorMsg);

private:
    QString _filePath;
    QString _targetIp;
    quint16 _targetPort;
    bool    _canceled = false;
};
```

```cpp
// 调用方（主线程）
auto *worker = new FileSenderWorker(path, ip, port);
auto *thread = new QThread(this);
worker->moveToThread(thread);

connect(thread, &QThread::started,      worker, &FileSenderWorker::start);
connect(worker, &FileSenderWorker::transferFinished,
        this,   &MainWindow::onTransferFinished);
connect(worker, &FileSenderWorker::transferFinished, thread, &QThread::quit);
connect(thread, &QThread::finished, worker, &QObject::deleteLater);
connect(thread, &QThread::finished, thread, &QObject::deleteLater);

thread->start();
```

> **检查清单**：
> - [ ] 类继承自 `QObject` 且有 `Q_OBJECT` 宏？
> - [ ] 公共入口写成 slot（不是普通方法）？
> - [ ] 所有 UI 反馈通过 signal 抛出，没有直接操作控件？
> - [ ] `cancel()` 是幂等的，多次调用不会出错？
> - [ ] 线程退出后 `worker` 和 `thread` 都被 `deleteLater`？

### 5.2 AppController（QML_SINGLETON）骨架

详细可运行示例见 `doc/templates/integration_demo/app_controller.{h,cpp}`。要点：

```cpp
// app_controller.h
#pragma once
#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;
class DiscoveryService;
class TransferSessionManager;

class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(DiscoveryService       *discovery   READ discovery   CONSTANT)
    Q_PROPERTY(TransferSessionManager *sessionMgr  READ sessionMgr  CONSTANT)
public:
    static AppController *create(QQmlEngine *, QJSEngine *);
    DiscoveryService       *discovery()  const { return _discovery; }
    TransferSessionManager *sessionMgr() const { return _sessionMgr; }
    Q_INVOKABLE void quit();
private:
    explicit AppController(QObject *parent = nullptr);
    DiscoveryService       *_discovery   = nullptr;
    TransferSessionManager *_sessionMgr  = nullptr;
};
```

**AppController 铁律**：
- 它是 QML 端唯一可直接访问 C++ 状态的入口；其他 C++ 对象都通过它的 property 暴露
- 由 QML 引擎按需 `create()` 出来，所有权归引擎；不要在 `main()` 里手动 `new`
- 它只负责"装配"和"路由"，**不写业务逻辑**

### 5.3 Manager 类骨架

`*Manager` 类是 QML 与底层网络/文件 I/O 之间的胶水层：
- 接收 QML 触发的业务命令（如"开始传输文件夹 X 到设备 Y"）
- 拉起对应的 Worker 后台线程
- 汇总多个 Worker 的进度，统一抛信号供 QML binding 消费
- 持久化关键状态（Session 列表、配置）

模板：

```cpp
// transfer_session_manager.h
#pragma once
#include <QObject>
#include <QHash>
#include <QtQml/qqmlregistration.h>

struct TransferSession;
class FileSenderWorker;

class TransferSessionManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit TransferSessionManager(QObject *parent = nullptr);

    Q_INVOKABLE QString createSendSession(const QString &targetIp, quint16 port,
                                          const QStringList &localPaths);
    Q_INVOKABLE void    cancelSession(const QString &sessionId);

signals:
    void sessionProgressChanged(const QString &sessionId, int percent);
    void sessionFinished(const QString &sessionId, bool ok);

private:
    QHash<QString, TransferSession *> _sessions;
};
```

### 5.4 协议帧使用样板

任何模块要发送一帧数据，统一通过 `FrameCodec::encode()`；任何模块要解析一帧，统一通过 `FrameCodec` 状态机。**不要自己拼字节**。

```cpp
// 发送侧
QJsonObject obj;
obj["session_id"] = sessionId;
obj["accepted"]   = true;
QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
QByteArray frame   = FrameCodec::encode(kTypeTransferRsp, payload);
socket->write(frame);

// 接收侧
auto *codec = new FrameCodec(this);
connect(socket, &QTcpSocket::readyRead, this, [=]{
    codec->feed(socket->readAll());
});
connect(codec, &FrameCodec::frameReady,
        this,  &MyClass::onFrameReady);
```

---

## 6. Git 协作流程

### 6.1 分支模型

```
main / master   ── 仅由 release 合并而来，永远可发布
    │
    ├── release  ── 准发布分支，集成测试在此进行
    │
    └── dev      ── 默认开发分支，所有 feature 合入此处
            │
            ├── feature/discovery-udp     ── UDP 发现
            ├── feature/ui-peer-list      ── 设备列表 QML
            └── feature/config-manager    ── 配置模块
```

**规则**：
- `main` 受保护，禁止任何人直接 push。
- `release` 仅在阶段完成后由 Tech Lead 从 `dev` 合并。
- `dev` 不直接写代码，所有改动走 feature 分支 + PR。
- feature 分支命名：`feature/<模块名简写>-<动词或主题>`，例：`feature/discovery-multicast-filter`。
- 修 bug 分支命名：`fix/<bug-id-或-简述>`。

### 6.2 提交信息规范（Conventional Commits）

```
<type>(<scope>): <subject>

[可选 body]
```

| type     | 用途                       |
| :------- | :------------------------- |
| feat     | 新功能                     |
| fix      | bug 修复                   |
| refactor | 重构（不改外部行为）       |
| test     | 新增/修改测试              |
| docs     | 仅文档变更                 |
| chore    | 构建脚本、CI、依赖更新等   |
| style    | 仅代码格式（空格、分号）   |

**示例**：

```
feat(discovery): 实现多网卡广播地址精确筛选

遍历 QNetworkInterface 过滤出 IsUp + CanBroadcast + 非回环网卡，
向每个网卡对应子网的 entry.broadcast() 定向发送 Hello 包，
解决双网卡机器只往默认网卡广播的问题。
```

```
fix(codec): 修复粘包场景下残留缓冲区未触发解码

feed() 末尾增加 while 循环回到 WaitingHeader 状态，
确保单次 readyRead 包含多帧时全部交付。
```

### 6.3 单次 PR 工作流（每天都在用的命令组合）

```bash
# 1. 同步最新 dev
git switch dev && git pull origin dev

# 2. 拉新分支开干
git switch -c feature/discovery-udp

# 3. 编码 + 小步提交
git add shared/protocol.h client/network/discovery_service.{h,cpp}
git commit -m "feat(discovery): 实现 DiscoveryService 骨架"

# 4. 推到远程
git push -u origin feature/discovery-udp

# 5. 在代码平台发起 PR -> dev
#    PR 描述模板见 §6.4
```

### 6.4 PR 描述模板

```markdown
## 涉及任务
对应《软件开发阶段计划》 Stage X 任务 X.X —— <一句话任务名>

## 主要变更
- 新增 xxx 类，实现 xxx
- 修改 xxx 以支持 xxx

## 自测情况
- [ ] 本机编译通过（Debug + Release）
- [ ] 关联单测全部通过 (`ctest`)
- [ ] 手工跑了 xxx 场景，结果符合预期

## 评审重点
（请评审者重点关注的地方，例如某个潜在的线程安全风险）
```

### 6.5 Code Review 节奏

- PR 发出后，**24 小时内**应有至少 1 名队友给出意见。
- 评审者关注 4 件事：**是否符合架构边界 / 是否有死锁或 UI 卡顿风险 / 命名规范 / 测试覆盖**。
- 评审者**不**关注：个人风格偏好（已被代码规范统一）、与本 PR 无关的历史问题。
- 通过标准：**1 名队友 Approve + CI 全绿** 即可合并；Tech Lead 自己的 PR 由其他两人轮流评审。

### 6.6 禁止动作

| 禁止                       | 后果                                       | 例外                          |
| :------------------------- | :----------------------------------------- | :---------------------------- |
| `git push -f` 推 main/dev  | 队友的 commit 会被覆盖丢失                 | 无                            |
| `git rebase` 已推送的分支  | 别人本地分支会乱                           | 仅限自己独占的 feature 分支   |
| `git commit -m "wip"` 推到 dev | 不可追溯，影响二分定位                     | feature 分支内部随意           |
| 跳过 Code Review 直接合并  | 团队失去同步窗口                           | hotfix 紧急修复 + 事后补 review |
| 提交未运行过的代码         | 浪费别人时间                               | 无                            |

---

## 7. 调试与排错

### 7.1 Qt 调试基础

- **Qt Creator 集成 GDB**：默认 F5 启动调试，断点、变量监视、调用栈三件套。
- **打印 Qt 对象**：用 `qDebug() << obj;` 而不是 `std::cout`，自动支持 `QString`、`QByteArray` 等。
- **关键变量启用 Qt 内置 dump**：
  ```cpp
  qDebug() << "peer table size:" << _peerTable.size()
           << "current:" << _peerTable.keys();
  ```
- **崩溃栈定位**：Debug 构建启用了 ASan + UBSan（见 §3.2 CMakeLists），堆栈越界/UAF 直接报到行号。

### 7.2 网络层抓包

| 工具       | 场景                                       | 命令示例                                          |
| :--------- | :----------------------------------------- | :------------------------------------------------ |
| Wireshark  | 看完整帧结构、字节序、丢包                 | 过滤：`udp.port == 45678 or tcp.port == 35100`   |
| tcpdump    | 服务器无 GUI 环境                          | `sudo tcpdump -i any -A 'port 45678 or 35100'`    |
| nc (netcat) | 验证 TCP 端口是否可达                     | `nc -zv 192.168.1.100 35100`                      |

**典型排错套路**：

```
现象: B 看不到 A 的设备
  ↓
①  Wireshark 在 B 上抓 UDP 45678 → 看到 A 的 Hello 包吗？
       没看到 → A 的广播地址错了（用了 255.255.255.255 而不是 192.168.1.255？）
       看到了 → ② 看 B 的解析日志，JSON 解析失败？
                       → 检查 device_id 字段是否为合法 UUID
```

### 7.3 TCP 粘包验证

`FrameCodec` 的状态机最易出 bug 的地方就是粘包/半包边界。**强制要求**为其编写下列覆盖单测（详见 §8.1）：

```cpp
// test_frame_codec.cpp
void TestFrameCodec::testGluedFrames() {
    // 把两帧合并成一个 ByteArray 喂进去，期望分两次发出 frameReady
    QByteArray frame1 = FrameCodec::encode(0x0001, "AA");
    QByteArray frame2 = FrameCodec::encode(0x0002, "BBBB");
    
    QSignalSpy spy(&codec, &FrameCodec::frameReady);
    codec.feed(frame1 + frame2);                  // 一次喂两帧
    
    QCOMPARE(spy.count(), 2);
}

void TestFrameCodec::testHalfFrame() {
    QByteArray frame = FrameCodec::encode(0x0001, "HELLO");
    QSignalSpy spy(&codec, &FrameCodec::frameReady);
    codec.feed(frame.left(4));                    // 半个帧头
    QCOMPARE(spy.count(), 0);
    codec.feed(frame.mid(4, 5));                  // 剩余帧头 + 部分载荷
    QCOMPARE(spy.count(), 0);
    codec.feed(frame.mid(9));                     // 剩余载荷
    QCOMPARE(spy.count(), 1);
}
```

### 7.4 性能 profiling

| 工具           | 用途                                          |
| :------------- | :-------------------------------------------- |
| `perf top`     | 实时看 CPU 热点函数                           |
| `perf record` + `perf report` | 离线分析 CPU 火焰图           |
| `iotop`        | 看磁盘 I/O 是否成为瓶颈                       |
| `iftop`        | 看网卡实时吞吐量是否打满                      |
| `htop`         | 看线程数、内存占用                            |

**NF-101 验收方法**：千兆交换机环境下传 5 GB 单文件，`iftop` 显示 `eth0` 吞吐量目标 ≥ 80 MB/s。若不达标，按以下顺序排查：

```
①  iotop 看磁盘读速度是否打满 → 是 → 换 SSD 或开启 mmap
②  htop 看 CPU 是否单核打满 → 是 → 检查是否 hash 在主路径上同步算
③  Wireshark 看 TCP 窗口是否被拉满 → 否 → 调大 socket buffer
④  以上都正常 → 看 chunk 大小是否过小造成 syscall 过频
```

---

## 8. 测试与验收

### 8.1 单元测试（Qt Test 框架）

每个**纯逻辑类**必须有单测。涉及 socket、文件 I/O 的类暂不强制（走集成测试）。

**强制单测覆盖清单**：

| 类                  | 关键用例                                              |
| :------------------ | :---------------------------------------------------- |
| `FrameCodec`        | 单帧 / 粘包 / 半包 / 空 payload / 超大 payload         |
| `DirSerializer`     | 单文件 / 多层目录 / 空目录 / 符号链接处理 / SHA-256 一致性 |
| `ConfigManager`     | 默认值 / 持久化往返 / device_id 生成与读回             |
| Protocol 常量       | 所有 Type 码值唯一、不冲突                            |

**Qt Test 骨架**：

```cpp
#include <QTest>
#include "frame_codec.h"

class TestFrameCodec : public QObject {
    Q_OBJECT
private slots:
    void testSingleFrame();
    void testGluedFrames();
    void testHalfFrame();
};

QTEST_MAIN(TestFrameCodec)
#include "test_frame_codec.moc"
```

**测试 CMake 集成**：

```cmake
# tests/CMakeLists.txt
find_package(Qt6 REQUIRED COMPONENTS Test)

function(add_gy_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Test gy_shared)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_gy_test(test_frame_codec)
add_gy_test(test_dir_serializer)
add_gy_test(test_config_manager)
```

### 8.2 集成测试场景清单

每个阶段完成时需要跑过对应场景（与《软件开发阶段计划》的验收标准对齐）：

| 阶段 | 必过场景                                                                                             |
| :--- | :--------------------------------------------------------------------------------------------------- |
| 2    | 局域网两机互相 15 秒内发现；任一关闭，对端 15 秒内剔除；多网卡机器双向均可见                          |
| 3A   | 单文件 100 MB 传输成功，进度条平滑刷新                                                                |
| 3B   | 多层目录（3 级，含中文文件名，含 0 字节文件）完整还原；SHA-256 全部一致                                |
| 3 全 | 1 GB+ 文件传输全程 UI 不卡顿（手动点击其他控件验证响应）                                              |
| 4    | 传输中拔网线/对端强退 → 本端 30 秒内检测到、清理资源、UI 状态正确显示"失败"                          |
| 4    | 关闭主窗口后系统托盘仍在；新传输请求弹出气泡通知                                                      |
| 4    | 重启应用，设备名、接收路径、历史记录全部恢复                                                          |

### 8.3 验收前自查表

提交答辩或交付前**全员**走一遍：

- [ ] 多机互相发现 OK
- [ ] 1 GB+ 文件传输吞吐量 ≥ 80 MB/s
- [ ] 文件夹传输目录结构完整还原 + SHA-256 一致
- [ ] 拖拽发起传输工作正常
- [ ] 传输进行中可正常取消
- [ ] 异常断网时双端均能优雅恢复
- [ ] 系统托盘最小化 + 通知气泡正常
- [ ] 配置项重启后正确恢复
- [ ] 全部单测通过（`ctest`）
- [ ] Release 构建编译无警告
- [ ] README 已更新到当前可用状态

---

## 9. 发布与部署

### 9.1 V1.0 客户端发布步骤

```bash
# 1. 切到 release 分支，拉 dev 的最新成果
git switch release
git merge dev --no-ff -m "release: v1.0.0"

# 2. 打标签
git tag -a v1.0.0 -m "Release v1.0.0 — P2P 文件传输 MVP"

# 3. 推到远程
git push origin release
git push origin v1.0.0

# 4. 本地打 Release 构建
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j

# 5. 收集产物
mkdir -p dist/GridYard-v1.0.0-linux-x86_64
cp build-release/src/client/appGridYard dist/GridYard-v1.0.0-linux-x86_64/
# 拷贝依赖的 Qt 动态库（用 linuxdeployqt 或 ldd 排查）
linuxdeployqt dist/GridYard-v1.0.0-linux-x86_64/appGridYard -appimage

# 6. 打包发布
tar -czf GridYard-v1.0.0-linux-x86_64.tar.gz -C dist GridYard-v1.0.0-linux-x86_64
```

### 9.2 V2.0 服务端部署（未来）

> V2.0 阶段补全。预占位以下要点：
> - PostgreSQL `helloword` 库初始化 SQL 脚本位置：`server/sql/init_schema.sql`
> - 服务端以 systemd 服务方式常驻：`gridyard-server.service`
> - 日志路径：`/var/log/gridyard-server/`
> - 配置文件：`/etc/gridyard/server.conf`（监听端口、数据库连接串）

---

## 10. 常见问题 FAQ

**Q1：我在 Windows 上能开发吗？**
A：不建议。团队统一 Manjaro，Qt 网络层、`<filesystem>` 在不同平台有细微差异，统一环境能避免"在我电脑上是好的"。如必须，请用 WSL2 + Manjaro 容器。

**Q2：Qt Creator 找不到 Qt 6 套件？**
A：执行 `qmake6 -query QT_INSTALL_PREFIX` 确认 Qt 安装位置，到 Creator 的"工具 → 选项 → 套件 → Qt Versions"手动添加。

**Q3：广播 Hello 包但 Wireshark 收不到？**
A：90% 是绑定到了错误的网卡。Manjaro 上同时连有线 + WiFi 时，必须显式遍历 `QNetworkInterface::allInterfaces()` 并向每个网卡的 `entry.broadcast()` 单独发包。

**Q4：TCP 传输莫名其妙断了，错误是 "RemoteHostClosedError"？**
A：通常是对端 crash 或防火墙拦截。先看对端日志；再用 `nc -zv <对端ip> 35100` 验证连通性；最后检查双端是否使用了相同的协议版本。

**Q5：QML 弹窗卡几秒才出现？**
A：检查触发链上有没有同步调用了文件 I/O 或网络等待。所有 I/O 必须走 Worker 模式（§5.1）。

**Q6：怎么验证 SHA-256 校验逻辑没写错？**
A：用命令行工具对照：`sha256sum /tmp/received_file.bin`，应当与发送端预先计算的 hash 完全一致。

**Q7：手册里没写的事情怎么办？**
A：先问群、再 Google、再问 Stack Overflow、最后翻 Qt 官方文档。形成决议后，**回头更新本手册**——文档是活的，不是写完就完事。

---

## 文档维护约定

- 本手册由全员共同维护，**任何人发现过时或错误都可直接修改提交 PR**。
- 重大修订（如新增章节、改变流程）应在群里通告。
- 每个阶段收尾时由 Tech Lead 做一次全文 review，把实际踩过的坑补进 §10 FAQ。
- 文档版本号与代码版本号无关，独立递增。

---

*v1.1 修订说明：删除 v1.0 引入的"团队成员分工总表"章节；引入《软件开发阶段计划》作为任务来源，所有 phase 维度的任务认领改由该文档驱动；交叉引用全面修正。*
