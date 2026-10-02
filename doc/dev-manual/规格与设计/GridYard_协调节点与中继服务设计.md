# GridYard 协调节点与中继服务设计

> 本文描述 `gridyard_rendezvous` 独立服务程序的运行模式、JSON 信令协议与资源防线，
> 覆盖 Stage 7.2~7.3 引入的协调与中继能力，以及 v7.9.0 起为 Relay 降级链路补齐的
> 邀请信令和同端口复用。客户端侧的降级决策流程见 README 的"校园网协调服务器"章节。

## 1. 定位与边界

- 只做**发现兜底**和**在线流式转发**：不落盘任何文件内容，不保存离线消息，不替代 P2P 直连。
- 与客户端不共享进程；客户端通过 `rendezvousEnabled / rendezvousHost / rendezvousPort` 配置接入。
- 认证（token 全链路）与注册表防投毒尚未落地，见 §6 挂起项。

## 2. 两种运行模式

| 模式 | 启动方式 | 行为 |
|:-----|:---------|:-----|
| 协调节点（默认） | `gridyard_rendezvous [--port 45679] [--token xxx]` | 同端口提供设备注册、候选拉取、中继邀请信令与内置中继转发 |
| 中继 | `gridyard_rendezvous --mode relay [--port 45679]` | 仅中继字节转发，无设备注册与邀请信令 |

协调模式按**首行 JSON 的 `type`** 分流：`register / list_peers / relay_invite / relay_poll`
走协调会话（长连接、按行分帧）；`relay_create / relay_join` 在剥离握手行后整条移交
中继管道。校园网部署只需开放一个端口。

## 3. JSON 信令协议

所有信令为一行 JSON（`\n` 分隔），可选 `token` 字段在服务端配置了口令时参与校验。

| 消息 | 方向 | 关键字段 | 说明 |
|:-----|:-----|:---------|:-----|
| `register` | 客户端→服务 | room, device_id, device_name, addresses[], tcp_port, ttl_seconds | 注册本机端点，心跳周期重发刷新 TTL |
| `register_ack` | 服务→客户端 | ttl_seconds, server_time | 注册确认，同时触发客户端立即轮询邀请 |
| `list_peers` | 客户端→服务 | room, device_id | 拉取同房间在线设备（过滤自身） |
| `peers` | 服务→客户端 | items[]{device_id, device_name, addresses[], tcp_port, ...} | 候选端点列表 |
| `relay_invite` | 发送端→服务 | room, relay_id, target_device_id, sender_device_id, file_name, total_bytes | 登记中继邀请；目标不在线直接报错 |
| `relay_invite_ack` | 服务→发送端 | relay_id | 邀请已受理 |
| `relay_poll` | 接收端→服务 | room, device_id | 轮询发给本机的邀请（随 5 秒心跳捎带） |
| `relay_invites` | 服务→接收端 | items[]{relay_id, sender_device_id, file_name, total_bytes} | 邀请一次性消费，TTL 60 秒 |
| `error` | 服务→客户端 | message | 通用错误 |

中继管道握手（连接建立后的首行）：

| 消息 | 方向 | 关键字段 | 说明 |
|:-----|:-----|:---------|:-----|
| `relay_create` | 发送端→中继 | relay_id（客户端生成 UUID） | 创建中继会话，担任发送角色 |
| `relay_join` | 接收端→中继 | relay_id | 加入会话，担任接收角色 |
| `relay_ready` | 中继→发送端 | — | 两端齐备；发送端收到后才开始 TLV 传输流 |
| `relay_error` | 中继→两端 | code, message | 对端离开/会话超时等，随后关闭连接 |

## 4. 中继转发时序（Relay 降级链路）

1. 发送端直连候选全部失败，按策略经协调服务器发送 `relay_invite`；
2. 收到 `relay_invite_ack` 后连接中继发送 `relay_create`，等待 `relay_ready`（30 秒超时）；
3. 接收端在心跳轮询中领到邀请，连接中继发送 `relay_join`；
4. 两端齐备，中继向发送端写 `relay_ready`，发送端开始标准 TLV 传输流；
5. 中继仅做双向字节转发：接收端的 `P2pServer` 首帧路由与文件接收管线零改动。

任一端断开时中继立即向另一端写 `relay_error` 并关闭其连接，传输方即时感知失败。

## 5. 资源防线（rendezvous_limits.h）

| 防线 | 值 | 说明 |
|:-----|:---|:-----|
| 控制行长度上限 | 协调 64KB / 中继握手 4KB | 超长直接断开；socket 读缓冲同步封顶 |
| 握手超时 | 协调 15s / 中继 5s | 期限内未收到首行即回收 |
| 会话空闲超时 | 协调 90s | 每行刷新；客户端 5s 心跳保活 |
| 并发上限 | 协调会话 256 / 中继会话 128 / 中继握手等待同额 | 超限拒绝新连接 |
| 中继积压上限 | 1MB | 会话未齐备前的缓存，超限拒绝接入 |
| 转发背压 | 读缓冲 4MB + 对端写队列 8MB | 超限暂停读取，`bytesWritten` 排空后续传，TCP 窗口回压发送端 |
| 未完成会话回收 | 60s | 只有一端的中继会话超时关闭 |

协调会话与中继握手共用 `LineSession` 分帧会话基类（行上限、超时、断开回收、
连接移交）。房间注册表为 `room → deviceId` 二级哈希，房间名可含任意字符。

## 6. 挂起项

- **token 全链路认证**：服务端已支持 `--token` 校验与中继握手 `join_token` 字段解析，
  但客户端不发送 token，开启后所有客户端会被拒绝。需要补配置项、设置界面与协议字段。
- **注册表防投毒**：deviceId/addresses 由客户端自报，可覆盖同房间的他人注册。
  需要认证或邀请制房间配合解决。

## Change Log

- v7.12.0（2026-10-02）：新增本文，收录 LineSession 加固、邀请信令与同端口复用设计。
- v7.9.0（2026-07-26）：Relay 降级链路落地，新增 relay_invite / relay_poll 信令与同端口内置中继。
- v7.3.0（2026-07-21）：Stage 7.2~7.3 协调节点与流式中继初版。
