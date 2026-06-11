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
| [class-diagram.svg](class-diagram.svg) | 类图 | 展示类结构、属性、方法和关系 |
| [component-diagram.svg](component-diagram.svg) | 组件图 | 展示模块结构和依赖关系 |
| [sequence-diagram.svg](sequence-diagram.svg) | 序列图 | 展示 C++↔QML 通信流程 |
| [data-flow-diagram.svg](data-flow-diagram.svg) | 数据流图 | 展示数据在各层间的流动 |
| [module-dependency.svg](module-dependency.svg) | 模块依赖图 | 展示模块间依赖方向 |

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
  # mmdc 会自动加 -1 后缀，需要重命名
  mv "${f}-1.svg" "${f}.svg"
done
```

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
