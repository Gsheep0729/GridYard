# 从 0 开始重构 GridYard (Stage 0) 实施计划

我们的目标是将刚刚新建的空白 Qt Quick 模板项目（`GridYard/` 目录）重构为符合规范的多模块项目结构（即您在 `src/` 目录下看到的 Stage 0 形态）。通过这个过程，我们将理清大型 Qt6 工程的模块化拆分、CMake 构建逻辑以及 C++ 与 QML 的正确集成。

## 实施思路与步骤

我们将分为四大阶段，逐步将这 3 个文件的雏形扩张为工程结构：

### 阶段 1：构建骨架与目录拆分（工程结构化）
目前模板的所有文件都在同一个目录下。这不适合后续扩展服务端和通用库。
1. **新建子目录**：在项目下创建 `client/`、`shared/`、`server/` 和 `tests/`。
2. **迁移入口点**：将默认的 `main.cpp` 和 `Main.qml` 移动到 `client/` 中。
3. **顶层 CMake 重写**：保留原 `CMakeLists.txt` 为顶层，但剥离其中具体的 Qt GUI 构建代码，改为管理全局编译选项（如 C++23、Qt 6.10 需求）和子目录引入（`add_subdirectory`）。

### 阶段 2：建立底层核心库（Shared 模块）
我们将构建供未来客户端和服务端复用的底层数据结构和通信协议。
1. **创建基础文件**：在 `shared/` 建立 `data_types.h`、`protocol.h` 和 `frame_codec.h/.cpp` 占位文件。
2. **配置 CMake**：在 `shared/` 创建 `CMakeLists.txt`，将其编译为一个静态库 `gy_shared`。
3. **集成 Qt 元对象**：在 `data_types.h` 中使用 `Q_GADGET` 和 `Q_PROPERTY` 宏注册基础的传输对象（如 `PeerInfo`）。

### 阶段 3：构建客户端核心逻辑（Client 模块）
搭建 C++ 业务层并将其暴露给 QML。
1. **创建控制器**：在 `client/core/` 建立 `app_controller.h/.cpp`。
2. **QML 集成准备**：在 `app_controller.h` 使用 `Q_OBJECT` 和 `QML_SINGLETON` 宏，向 QML 提供如版本号、退出应用等核心能力。
3. **配置 CMake**：在 `client/` 创建 `CMakeLists.txt`，通过 `qt_add_executable` 编译客户端进程，利用 `qt_add_qml_module` 注册 `cqnu.gridyard.client` QML 模块，并链接刚刚写好的 `gy_shared` 库。

### 阶段 4：界面集成与链路验证
重构用户界面并连通所有模块。
1. **改造 Main.qml**：将空白页面的默认控件替换，导入自定义模块 `import cqnu.gridyard.client 1.0`。
2. **规范化 QML**：落实规范要求，将顶层根对象 id 改为 `tw_mainWindow`。并添加提示文字与利用 C++ 后端的方法。
3. **验证链路**：编译运行，确保控制台成功输出 C++ 与 QML 交互的日志信息（如 `AppController::quit invoked`）。
