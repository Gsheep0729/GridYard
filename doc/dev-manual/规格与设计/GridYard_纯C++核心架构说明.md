# GridYard 纯 C++ 核心架构说明

| 字段 | 内容 |
| :--- | :--- |
| 文档版本 | v8.1.0 |
| 日期 | 2026-10-28 |
| 适用版本 | v8.1.0 |
| 适用范围 | 全仓架构分层、链接依赖与红线验证方法 |

## 1. 背景与目标

GridYard 的图形界面使用 Qt（QML 表现层与 QApplication 入口），其余能力——网络通信、协议编解码、配置与持久化——自 v7.26.0 起逐步迁移到自研纯 C++23 实现，v8.0.0 完成收口。当前的硬性边界是：产品构建图中 Qt 只出现在客户端目标与测试目标，协调与中继服务的二进制不链接任何 Qt 库，核心库目标零 Qt 依赖。

这条边界由三件事共同保证：源码层的目录隔离（核心目录内的头文件零 Qt 引入）、构建层的目标链接约束（核心目标不链接任何 Qt target，并显式关闭 AUTOMOC/AUTOUIC/AUTORCC）、二进制层的取证（ldd/readelf 检查）。三层各有守卫脚本或验证命令，任何人都可以按本文第四节自行复核。

## 2. 终态分层与链接矩阵

### 2.1 纯 C++ 核心层

核心层的每个目标都严禁链接任何 Qt target，头文件不得引入任何 Qt 头：

| 目标 | 位置 | 职责 |
| :--- | :--- | :--- |
| `gy_core` | `src/core/`（types/codec/config/utils 四分组） | 领域类型、TLV 帧编解码、聊天消息编解码、目录哈希（picosha2 流式）、INI 配置读写 |
| `gy_storage_core` | `src/core/storage/` | vendored SQLite3 C API 封装（连接/语句/事务 RAII）、ISO 时间互转、Schema 迁移与三个仓储的纯 C++ 实现 |
| `gy_net_core` | `src/shared/net/core/` | NetReactor 事件泵（Boost.Asio io_context 单线程引擎，描述符等待挂接 fd 就绪，代际校验由描述符对象身份承接）与 TCP/UDP 套接字核心 |
| `gy_protocol_contracts` | `src/shared/`（INTERFACE） | 协调与中继行协议的键名常量，双端单一来源 |
| `gy_rendezvous` | `src/rendezvous/` | 协调节点与流式中继的服务端实现（nlohmann JSON、NetReactor 事件引擎、asio 信号集优雅退出） |

第三方依赖全部 vendored 在 `third_party/`（nlohmann/json、picosha2、SQLite3 amalgamation），不引入系统级 Qt 外部依赖。

### 2.2 客户端桥接层

客户端目标 `appGridYard` 是唯一允许使用 Qt 的产品目标。它直接编译三部分内容：应用与界面层（`src/client/core/` 的控制器、模型与组合根，QML 模块本体）、网络适配层（`src/client/network/`，包含自 v8.0.0 归位的原生套接字 Qt 适配器与传输 Worker）、数据层代管者（`src/client/storage/` 的 LocalDataBroker 与 DatabaseWorker）。Qt 侧的类型与核心层的纯 C++ 类型在边界处做机械互转，转换件集中在 `src/client/core/codec_bridge.h`（QString 与 UTF-8 std::string、QDateTime 与 Unix 毫秒、聊天 DTO 往返）与存储任务边界（Qt 领域记录与核心记录）。跨线程向界面线程回投统一走 `post_to_ui_thread.h`（QPointer 存活检查 + QueuedConnection）。

`gridyard_rendezvous` 可执行程序只链接 `gy_rendezvous`，传递闭包为 gy_net_core、gy_protocol_contracts、nlohmann_json 与 Threads，`ldd` 输出仅剩 libstdc++/libm/libgcc/libc 四个系统库——这是服务端零 Qt 的直接证据（下文称 D7 里程碑）。

### 2.3 终态链接矩阵

| 消费目标 | 链接内容 | Qt 链接 |
| :--- | :--- | :--- |
| `appGridYard` | Qt6 Quick/QuickControls2/QuickDialogs2/Widgets + gy_shared + gy_net_core + gy_storage_core + gy_core | 仅 GUI 家族与 Core |
| `gridyard_rendezvous` | gy_rendezvous（→ gy_net_core + gy_protocol_contracts + nlohmann_json + Threads） | 无 |
| `gy_core` / `gy_storage_core` / `gy_net_core` / `gy_shared` / `gy_protocol_contracts` / `gy_rendezvous` | 见 2.1/2.2 | 无 |
| 各测试目标 | 按需直编客户端源文件 + 核心库 | 允许（Qt Test） |

历史上的 `gy_net`、`gy_domain`、`gy_storage` 三个目标已退役：套接字适配器归位 `src/client/network/` 随客户端与测试直编，领域记录头归属客户端源清单，旧 Qt Sql 持久化实现删除（调用点自 Phase 4 起全部走 gy_storage_core）。隔离防线脚本的基线矩阵保留了这三个名字作兜底——任何目标以 Qt 链接重新出现即按新违规计。

## 3. 关键机制

网络事件源的换心发生在 v7.26.0：`PosixTcpSocket`/`PosixTcpServer`/`PosixUdpSocket` 保持类接口不变，内部的事件监听从 QSocketNotifier 换成共享的 NetReactor（回调经 QueuedConnection 投回套接字归属线程执行，线程亲和与阻塞等待语义与旧实现一致）。服务端则彻底不经 Qt：LineSession、RelayServer、RendezvousServer 直接以回调风格挂接事件泵。v8.1.0 起 NetReactor 的引擎线程由手写 poll 循环换为 Boost.Asio io_context（vendored，纯头库，单线程运转保持命令先于派发与注销屏障语义），fd 就绪监听经 asio 描述符等待承载，核心 TCP 套接字与监听器改为 asio 套接字对象并各配 strand 串行关闭与取消，fd 复用后的陈旧事件由描述符对象身份结构性隔离，不再依赖手工代际比对。

协议编解码的现役实现是 `src/core/codec/` 的双生版（FrameCodecCore/ChatMessageCore），客户端经 `codec_bridge.h` 调用，golden 基线套件（test_golden_baseline）以冻结字面量对拍新旧实现的输出字节，是线格式不变的最高裁判。

配置读写（ConfigStore，INI 语义与 QSettings 逐字符对拍固化）、目录序列化（std::filesystem + picosha2）、SQLite 访问（vendored sqlite3 C API，SQL 文本与旧 Qt Sql 版逐字一致）同样由 golden 用例与存储测试锁定行为等价。

### 3.1 停机次序契约

进程退出按固定次序收敛：先停网络，再停存储，最后销毁其余 Qt 对象。停网络指停止监听与发现广播、排空事件泵与传输 worker，并让所有套接字关闭动作在各自主体的串行域内完成；停存储指 DatabaseWorker 在归属线程内删除并回收其内部任务线程；此后才允许销毁 QML 引擎、窗口与托盘等 Qt 对象。组合根的 QObject 子对象按创建序析构，网络成员先于存储成员创建，这一顺序就是契约的代码承载；调整成员顺序或倒置网络与存储的关闭关系都必须重新评审本节。

SIGTERM/SIGINT 走同一条排空链：信号处理器只向自管管道写一个字节，主线程的事件通知器读空管道后调用与托盘退出相同的请求入口，不弹确认框、不经过 QML。请求入口先关闭存储受理并等待队列排空（带三秒兜底），再退出事件循环；网络对象在此后的析构阶段停止，存储 Worker 的删除发生在网络停止之后、其余 Qt 对象销毁之前。

Worker 的唯一正确形态是在归属线程内删除：deleteLater 由归属线程的事件循环执行，或在停机路径用阻塞投递让归属线程同步执行删除。禁止在 UI 线程直接 delete，禁止依赖线程退出时 DeferredDelete 的冲刷时序，也不得让无 parent 的 Worker 悬空。删除动作必须发生在其内部任务线程 join 之前，保证队尾任务已经排空。

停机角案：`isRunning()==false` 的直通前提是 `stop()` 已完全返回（引擎线程 join 完毕）。外线程观察到泵未运行就直通析构，可能与另一线程尚未完成的 stop 排空并发触碰同一引擎对象；停机期间不得从其他线程析构套接字对象。这条约束与“关闭与取消一律投递到引擎串行域”的纪律共同构成退出路径的安全边界。

## 4. 依赖红线与验证指引

产品代码零 QtNetwork 是第一条红线：全仓源码不引用任何 Qt 网络类，CMake 不出现 Qt6::Network。复核命令：

```bash
grep -rn 'QTcpSocket\|QUdpSocket\|QTcpServer\|QHostAddress\|QNetworkInterface' src/
grep -rn 'Qt6::Network\|Qt6::Sql' src/ --include=CMakeLists.txt   # 两项都应为空
```

需要注意一个容易误判的点：客户端进程的运行时镜像里能看到 libQt6Network.so，这是 Qt Quick 框架自身的传递依赖——readelf 实测 libQt6Qml、libQt6Quick、libQt6QuickControls2、libQt6QuickTemplates2、libQt6QuickDialogs2、libQt6QmlMeta、libQt6QmlModels、libQt6QmlWorkerScript 八个库的 NEEDED 表都直接列出 libQt6Network（Qml 本体即依赖它）。只要继续使用 Qt Quick，这条传递依赖无法从镜像中消除。正确的取证口径是看产品二进制自己的直接依赖：

```bash
readelf -d build-ninja/client/appGridYard | grep NEEDED
# 预期：libQt6QuickControls2 / libQt6Widgets / libQt6Qml / libQt6Gui / libQt6Core，
# 外加 libGLX/libOpenGL 等系统库；不出现 Qt6Network 与 Qt6Sql
```

服务端零 Qt（D7 里程碑）的取证：

```bash
ldd build-ninja/rendezvous/gridyard_rendezvous | grep -i 'Qt\|libQ'   # 预期空输出
readelf -d build-ninja/rendezvous/gridyard_rendezvous | grep NEEDED
# 预期：libstdc++ / libm / libgcc_s / libc 四个系统库
nm -C build-ninja/rendezvous/gridyard_rendezvous | grep -c QMetaObject   # 预期 0
```

核心目录的源码级隔离由守卫脚本检查，`--strict` 模式在验收与日常提交前都应零违规：

```bash
python3 src/scripts/check_core_isolation.py --core-dirs src/core src/shared/net/core --strict
# 退出码 0；防线一扫 Qt 头引入、防线二扫 Qt 符号、防线四静态解析核心目标的 Qt 链接
```

配套的版本头与排版守卫：

```bash
python3 src/scripts/check_versions.py        # 版本三处一致与文件头零漂移
python3 src/scripts/check_code_style.py      # 静态规范（含 Qt 网络符号阻断）
```

## 5. 既定裁决

三条容易复议的边界在此落文，后续修改需重新评审：

其一，编解码桥接对孤立代理项的处理。QString 转 UTF-8 std::string 时孤立代理项（如 U+D800）会被静默丢弃，而旧的 QJsonDocument 链路会把它转义进 JSON 文本。实测该差异只在程序化构造 QString 时可达，键盘与剪贴板输入恒为合法 UTF-16；对端伪造携带 \ud800 转义的消息在新链路按解码失败拒绝，属收紧而非回归。结论：维持现状，不接受孤立代理项。

其二，套接字适配器的形态。核心层 TcpSocketCore 只提供接入侧 adopt（无 connectTo），客户端出站连接继续使用 `src/client/network/` 的 PosixTcpSocket 适配器——其事件源在换心后已是纯 POSIX（NetReactor），不迁移 connectTo 意味着不动最稳定的传输主链路。适配器归位客户端目录是终案，不在共享层恢复 Qt 桥接目标。

其三，目录序列化的并列次序契约。DirSerializer 的条目排序为"名称按小写折叠的字典序，折叠相等时按原始字节序决出"。实测依据：QCollator（忽略大小写）对折叠相等的名字恒返回相等（无决出能力），QDir::LocaleAware 枚举的并列次序随目录内部顺序漂移，而 QDir::Name 默认枚举恒为字节序且与本实现一致。因此并列次序以原始字节序为契约，QCollator 无稳定承诺、不作为依据。
