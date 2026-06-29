# release 分支发布整理指南

本文档记录 GridYard 最终发布分支的定位、内容边界和整理流程。`release` 分支面向课程验收和最终交付，老师或验收人员拉取该分支后，应能直接看到正式源码、说明文档、设计材料和演示交付材料入口。

## 1. 分支定位

`release` 分支不是日常开发分支，也不是自动化测试归档分支。它的目标是：

1. 保留最终可构建、可运行的正式应用源码。
2. 保留 README、设计文档、开发心得、API/UML 文档和打包说明。
3. 提供 PPT、演示视频、发布包说明等课程交付材料的固定入口。
4. 移除不面向验收阅读的测试代码、测试过程指南和开发期临时文件。

日常开发、自动化测试和详细测试过程应保留在 `dev` 或 `gy` 等开发分支中。

## 2. release 分支应保留的内容

建议保留：

| 内容 | 原因 |
|:-----|:-----|
| `README.md` | 项目入口，说明功能、构建运行、数据目录、最终验证结论 |
| `LICENSE` | 版权和使用边界 |
| `src/client/` | 正式客户端源码 |
| `src/shared/` | 协议、数据结构、编解码等正式共用源码 |
| `src/server/` | V2 演进规划目录，可作为架构扩展说明保留 |
| `src/scripts/` | 与文档归档、发布整理相关的辅助脚本 |
| `src/CMakeLists.txt` | 发布构建入口，只默认构建正式客户端 |
| `doc/api-docs/` | 模块 API 文档 |
| `doc/diagrams/` | UML、组件图、时序图等设计材料 |
| `doc/dev-manual/规格与设计/` | 需求、架构、数据层设计等正式设计文档 |
| `doc/dev-manual/开发心得/` | 开发过程、踩坑记录、最终路径问题排查和解决方案 |
| `doc/dev-manual/测试与部署/打包指南_Linux_AppImage.md` | 发布包构建与运行说明 |
| `doc/delivery/` | PPT、演示视频、发布包说明等交付材料入口 |
| `doc/团队分工说明.md` | 课程验收常用材料 |
| `doc/CQNU.png` | 项目或课程展示材料可能需要的静态资源 |

## 3. release 分支不建议保留的内容

建议移除：

| 内容 | 原因 |
|:-----|:-----|
| `src/tests/` | 自动化测试代码属于开发验证资产，发布分支默认不构建测试目标 |
| `doc/dev-manual/测试与部署/单机测试指南.md` | 详细测试流程面向开发验收，不适合作为最终发布分支主材料 |
| `doc/dev-manual/测试与部署/文件传输测试流程.md` | 属于测试过程文档，应留在开发分支 |
| `.local.md`、`*.local.*` | 本地临时笔记，不应进入发布分支 |
| `.antigravitycli/`、`CLAUDE.md`、AI 工作流配置 | 本地开发环境或助手上下文，不属于交付材料 |
| 构建目录、缓存、日志、AppImage 展开目录 | 可再生成，不应提交到 Git |
| 大体积视频原文件 | 如仓库未启用 Git LFS，建议放校内网盘或课程平台，并在 `doc/delivery/README.md` 中记录链接 |

## 4. 整理流程

### 4.1 确认最终开发分支

先确认最终版代码所在分支，例如 `gy` 或 `dev`：

```bash
git switch gy
git status --short
git log --oneline -5
```

确认最终提交、版本号、README 和 tag 都已经同步。

### 4.2 备份旧 release 分支

发布分支可能和开发分支长期分叉。整理前必须备份：

```bash
git branch backup-release-before-v6.8.1-20260629 release
```

备份分支用于保留旧发布分支现场，避免发布分支改写后无法追溯。

### 4.3 对齐最终版代码基线

切换到 `release`，将其对齐到最终开发分支：

```bash
git switch release
git reset --hard gy
```

如果团队要求保留完整 merge 历史，也可以使用 `git merge gy`。但当 `release` 分支已经明显落后且分叉较多时，先备份再重置到最终版基线更适合作为课程发布分支。

### 4.4 移除测试代码和测试指南

```bash
git rm -r src/tests
git rm doc/dev-manual/测试与部署/单机测试指南.md
```

如果仓库中仍有文件传输测试流程文档，也应从发布分支移除：

```bash
git rm doc/dev-manual/测试与部署/文件传输测试流程.md
```

### 4.5 调整发布构建入口

编辑 `src/CMakeLists.txt`，移除：

```cmake
enable_testing()
add_subdirectory(tests)
```

发布分支的顶层 CMake 应只默认构建正式客户端相关 target，例如：

```cmake
add_subdirectory(shared)
add_subdirectory(client)
```

### 4.6 更新 README

README 需要体现发布分支定位：

1. 保留构建与运行命令。
2. 保留 AppImage/压缩包运行时数据目录说明。
3. 保留 v6.8.1 最终发布验证结论。
4. 删除自动化测试运行命令。
5. 删除 `src/tests/` 目录结构说明。
6. 增加 `doc/delivery/` 和打包指南入口。

### 4.7 准备交付材料入口

在 `doc/delivery/README.md` 中说明 PPT、演示视频和发布包说明的放置规则：

```text
doc/delivery/
├── README.md
├── GridYard-v6.8.1-demo.pptx
└── GridYard-v6.8.1-demo.mp4
```

如果视频过大，不建议直接提交到普通 Git 仓库。可使用 Git LFS，或在 `doc/delivery/README.md` 中记录课程平台、校内网盘链接和文件校验信息。

### 4.8 构建验证

用独立构建目录验证发布分支不依赖测试 target：

```bash
cmake -S src -B /tmp/gridyard-release-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/gridyard-release-build -j
```

验证重点：

1. CMake 配置通过。
2. 不再生成测试 target。
3. `client/appGridYard` 能成功链接。

## 5. 提交和推送

确认只暂存发布分支需要的文件：

```bash
git status --short
git add -A README.md src/CMakeLists.txt src/tests doc/dev-manual/测试与部署/单机测试指南.md doc/delivery
git commit -m "整理最终发布分支内容"
```

如果 `release` 是通过重置方式对齐到最终开发分支，推送时通常需要：

```bash
git push --force-with-lease origin release
```

使用 `--force-with-lease` 而不是普通 `--force`，可以避免误覆盖远端其他人刚推送的新提交。

## 6. 最终发布分支检查清单

- `README.md` 能让老师快速理解项目功能、运行方式和最终验证结果。
- `src/CMakeLists.txt` 不再引用不存在的 `tests` 子目录。
- `src/tests/` 不在发布分支。
- 单机测试指南、文件传输测试流程等测试过程文档不在发布分支。
- `doc/dev-manual/开发心得/` 保留。
- `doc/dev-manual/测试与部署/打包指南_Linux_AppImage.md` 保留。
- `doc/delivery/README.md` 存在，用于后续放 PPT、演示视频和发布包说明。
- 独立 Release 构建通过。
- 推送前已备份旧 `release` 分支。

## 7. 本次 v6.8.1 整理结论

本次整理采用“备份旧发布分支，再将 `release` 对齐最终版并裁剪发布内容”的方式。这样可以保证老师拉取 `release` 后看到的是清晰的最终交付版本，而不是混杂测试代码、测试流程和开发期临时文件的工作分支。

最终发布分支应以“可阅读、可构建、可演示、可追溯”为标准：正式源码和关键文档保留，测试资产和本地开发痕迹留在开发分支。
