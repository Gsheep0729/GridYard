# GridYard 架构图

本目录包含 GridYard 项目的 UML 和架构图，使用 Mermaid 语法绘制。

## 图表索引

| 文件 | 内容 | 说明 |
|:-----|:-----|:-----|
| [class-diagram.md](class-diagram.md) | 分层类图 | 按表现层、应用逻辑层、领域层和数据管理层说明当前类关系 |
| [component-diagram.md](component-diagram.md) | 组件图 | 展示模块结构和依赖关系 |
| [sequence-diagram.md](sequence-diagram.md) | 序列图 | 展示 C++↔QML 通信流程 |
| [data-flow-diagram.md](data-flow-diagram.md) | 数据流图 | 展示数据在各层间的流动 |
| [module-dependency.md](module-dependency.md) | 模块依赖图 | 展示模块间依赖方向 |

## 图表索引（SVG 高清版）

| 文件 | 内容 | 说明 |
|:-----|:-----|:-----|
| [class-diagram-1.svg](class-diagram-1.svg) | 类图 §1 应用逻辑层 | AppController / ConfigManager / DiscoveryService / P2pServer / TransferSessionManager 组装关系 |
| [class-diagram-2.svg](class-diagram-2.svg) | 类图 §2 协议契约与共享值类型 | ProtocolConstants / ErrorCode / FrameCodec / PeerInfo / FileEntry / TransferSession |
| [class-diagram-3.svg](class-diagram-3.svg) | 类图 §3 文件传输运行时与线程模型 | Worker / P2pServer / QThread 后台线程与协议错误处理链路 |
| [class-diagram-4.svg](class-diagram-4.svg) | 类图 §4 QML 表现层组件 | Main.qml / PeerListView / DeviceSessionView / TransferPanel / Dialogs |
| [class-diagram-5.svg](class-diagram-5.svg) | 类图 §5 数据管理层与外部资源 | ConfigManager / Logger 与 QSettings / FileSystem / NetworkConnection |
| [component-diagram.svg](component-diagram.svg) | 组件图 | src/ 目录树 + 四层架构颜色对照 |
| [sequence-diagram-1.svg](sequence-diagram-1.svg) | 序列图 §1 应用启动初始化 | main → engine → AppController.create → init 三件套 |
| [sequence-diagram-2.svg](sequence-diagram-2.svg) | 序列图 §2 文件发送 | QML → TransferSessionManager → QThread + Worker → FrameCodec → TCP |
| [sequence-diagram-3.svg](sequence-diagram-3.svg) | 序列图 §3 文件接收（后台线程） | TCP 入站 → P2pServer 创建后台线程 → initialize → 弹窗确认 → 落盘 |
| [sequence-diagram-4.svg](sequence-diagram-4.svg) | 序列图 §4 取消与错误处理 | cancelSession / FrameCodec.errorOccurred / transferFinished(ErrorCode) |
| [data-flow-diagram-1.svg](data-flow-diagram-1.svg) | 数据流图 §1 设备发现流 | UDP Hello ↔ DiscoveryService |
| [data-flow-diagram-2.svg](data-flow-diagram-2.svg) | 数据流图 §2 文件发送流 | QML 拖拽 → Worker → FrameCodec → TCP |
| [data-flow-diagram-3.svg](data-flow-diagram-3.svg) | 数据流图 §3 文件接收流 | TCP → 后台 Worker → FrameCodec → FileSystem |
| [data-flow-diagram-4.svg](data-flow-diagram-4.svg) | 数据流图 §4 配置与日志流 | QSettings / 日志文件 |
| [data-flow-diagram-5.svg](data-flow-diagram-5.svg) | 数据流图 §5 协议错误流 | FrameCodec → Worker → SessionRecord.errorCode → QML |
| [module-dependency.svg](module-dependency.svg) | 模块依赖图 | 四层架构 + 具体类 #include 依赖方向 |

> class-diagram / sequence-diagram / data-flow-diagram 均按层 / 场景 / 数据流拆分为多张子图，导出后保留 `-1.svg` ~ `-N.svg` 分层归档；component-diagram 与 module-dependency 各只含 1 个 mermaid 块，导出后规整为单文件。如需在文档中嵌入，按节引用对应的 `-N.svg` 即可。

## 使用方式

1. **GitHub/GitLab**：直接查看 `.md` 文件，Mermaid 图会自动渲染
2. **本地预览**：使用 VS Code + Mermaid 插件
3. **导出 SVG**：使用 Mermaid CLI（见下方）

## SVG 导出方法

### 环境准备

```bash
# 安装 mermaid-cli（全局）
npm install -g @mermaid-js/mermaid-cli

# Linux root 用户需要 puppeteer 配置（跳过沙箱）
cat > /tmp/puppeteer-config.json << 'EOF'
{
  "args": ["--no-sandbox", "--disable-setuid-sandbox"]
}
EOF
```

### 单文件转换

```bash
cd doc/diagrams

# 基本用法
mmdc -i class-diagram.md -o class-diagram.svg

# 推荐参数：dark 主题 + 透明背景 + 3x 缩放
PUPPETEER_EXECUTABLE_PATH=/usr/bin/google-chrome-stable \
mmdc -i class-diagram.md -o class-diagram.svg \
  -t dark -b transparent --scale 3 \
  -p /tmp/puppeteer-config.json
```

### 批量转换

```bash
cd doc/diagrams

for f in class-diagram component-diagram data-flow-diagram module-dependency sequence-diagram; do
  PUPPETEER_EXECUTABLE_PATH=/usr/bin/google-chrome-stable \
  mmdc -i "${f}.md" -o "${f}.svg" \
    -t dark -b transparent --scale 3 \
    -p /tmp/puppeteer-config.json

  # mmdc 处理含 N 个 mermaid 块的 md 时会输出 N 个文件：${f}-1.svg ... ${f}-N.svg
  # 单图 md（component / sequence / data-flow / module-dependency）只有 1 块，规整为 ${f}.svg
  # 多图 md（class-diagram 按 4 层拆 5 节）保留所有 -N.svg 分层归档
  count=$(ls "${f}-"*.svg 2>/dev/null | wc -l)
  if [ "$count" -eq 1 ]; then
    mv "${f}-1.svg" "${f}.svg"
  fi
done
```

> **注意**：`class-diagram.md` 包含 5 个 mermaid 块（§1 ~ §5），导出后保留为 `class-diagram-1.svg` ~ `class-diagram-5.svg`，对应 README "图表索引（SVG 高清版）" 表中的分层映射；其他 4 份 md 各只含 1 个 mermaid 块，导出后会规整为单文件 `${f}.svg`。

### 参数说明

| 参数 | 作用 |
|:-----|:-----|
| `-i` | 输入文件（`.md` 或 `.mmd`） |
| `-o` | 输出文件名 |
| `-t dark` | 暗色主题 |
| `-b transparent` | 透明背景 |
| `--scale 3` | 3 倍缩放（高清） |
| `-p` | puppeteer 配置文件路径 |
| `PUPPETEER_EXECUTABLE_PATH` | 指定 Chrome/Chromium 路径 |

> **注意**：mmdc 从 Markdown 读取时会自动给输出文件名加 `-1` 后缀，批量脚本中已处理重命名。

## 更新说明

- Stage 0（2026-05-24）：初始版本，展示工程骨架
- Stage 2（2026-06-02）：更新类图，新增 ConfigManager / DiscoveryService / TransferSessionManager
- 2026-06-03：导出全部 SVG 高清版（dark 主题、3x 缩放）
- v4.9（2026-06-13）：类图按层拆分，补充设备会话页、运行时会话记录和 senderDeviceId 链路
- v4.11（2026-06-13）：类图补充自动接收配置、文件夹根目录和空目录传输状态
- v4.12（2026-06-14）：类图补充共享展示格式化工具和表现层动画职责
- v4.13.1 ~ v4.13.3（2026-06-15）：类图记录文件夹标记、根目录预览与会话展开状态
- v4.14.0（2026-06-15）：类图补充单条与批量清理会话记录、删除已接收本地文件字段
- v4.15.0 ~ v4.15.2（2026-06-17）：类图新增 ErrorCode 枚举、协议版本字段、分级 Payload 上限与 maxPayloadForType()；FrameCodec 补 errorOccurred 信号；Worker transferFinished 改三参数并新增 requestAccepted/requestRejected/initialize；SessionRecord 补 errorCode/errorMsg；P2pServer 后台线程模型（每连接独立 QThread，worker+socket moveToThread，children() 统一收尾）；标题同步到 v4.15.2
- 2026-06-18：按 v4.15.2 重写后的 class-diagram.md（含 5 个 mermaid 块）重新批量导出 SVG；class-diagram 改为分层归档（-1 ~ -5），同步更新 README 索引与批量导出脚本以正确处理多 mermaid 块场景
- 2026-06-18：补齐 component-diagram / sequence-diagram / data-flow-diagram / module-dependency 四张图——之前停留在 Stage 0 状态严重过时；按 v4.16.0 实际目录树、C++↔QML 通信、跨层数据流与 #include 依赖重写；sequence 与 data-flow 各含 4 ~ 5 个 mermaid 块，按层归档为 `-N.svg`
- 2026-06-18：审核修正——版本号统一为 v4.16.0（main.cpp / CMakeLists.txt / README.md 三处一致），新增 `utils/Style.js` 样式常量系统到 class-diagram / component-diagram / module-dependency；class-diagram SessionRecord 补 filePath / fileSize / totalFiles / senderName 4 个字段；TransferPanel 修正不存在的属性，补 isSessionExpanded / setSessionExpanded JS 函数
