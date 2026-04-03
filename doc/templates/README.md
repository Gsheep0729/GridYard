# GridYard 模板与示例

本目录提供可直接复制使用的工程脚手架与最小可运行示例，配合
《GridYard——Qt6 + QML 代码规范》（GY-CS-002）的强制条款落地。

| 文件 / 目录                       | 用途                                                              |
| :-------------------------------- | :---------------------------------------------------------------- |
| `CMakeLists.widgets.template`     | **Widgets** 项目的 CMake 样板（团队作业风格，含 deploy 脚本）       |
| `CMakeLists.qml.template`         | **Qt Quick / QML** 项目的 CMake 样板（团队作业风格，含 qt_add_qml_module） |
| `qmldir.template`                 | 手写 `qmldir` 参考（仅纯 QML 模块需要；走 `qt_add_qml_module()` 时自动生成） |
| `integration_demo/`               | C++ ↔ QML 集成的最小可运行示例，覆盖规范 §七 全部要求            |

## 与团队既有项目的对齐

本目录的所有模板都与团队 `作业/Qt-Software-Group7/` 下的实际代码风格保持一致：

- `cmake_minimum_required(VERSION 4.2.3)` + `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`
- Widgets 项目使用 `_qt_add_executable` 包装函数与 `qt_generate_deploy_app_script`
- QML 项目使用 `qt_add_executable(app<name> WIN32)` 与 `qt_add_qml_module(... URI "..." VERSION 1.0)`
- 安装前缀统一为 `/opt/${PROJECT_NAME}`
- 顶部预留 `FILE_SET cxx_modules` 给未来纯逻辑模块迁移使用

如果你正在做的项目不属于上述两类（如混合 Widgets + QML、含自定义子模块），
请基于这两份模板修改，或在群里讨论后扩展本目录。

## 模板使用流程

1. 复制目标模板到对应目录后**重命名去掉 `.template` 后缀**：
   ```bash
   cp doc/templates/CMakeLists.qml.template CMakeLists.txt
   ```
2. 把所有 `<<PLACEHOLDER>>` 字符串替换为项目实际值。
3. 提交 PR 前对照规范 §十四 自检清单逐项过一遍。

## 示例工程的角色

`integration_demo/` 不是 GridYard 的一部分，仅作 **教学** 与 **回归验证** 用途：

- 新成员入坑：先 build & run 这个 demo，看 C++/QML 是怎么对话的
- 规范条款变更：先在 demo 上验证新规则能跑通，再写进规范
- 答辩演示：可以作为最小可对外讲解的 Qt6/QML 集成范本

它本身遵守全部代码规范（红线 + 强约束 + 反模式禁止），可以作为
正向参考——你的真实代码与它对照，越像越好。
