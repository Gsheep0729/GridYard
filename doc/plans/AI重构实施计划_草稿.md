# 从零重构 GridYard 实施计划

本计划根据 `doc/plans/从0重构GridYard实施计划.md` 的要求，基于 `src/old/` 中之前写过的代码进行分步复刻，将当前的 `src/` 目录结构进行全面模块化改造。

## 阶段 1：构建骨架与目录拆分（工程结构化）

### Proposed Changes
- **新建子目录**：在 `src/` 下建立 `client/`、`shared/`、`server/` 和 `tests/`，以及 `scripts/`。
- **清理根目录源码**：删除或移动根目录下现有的 `main.cpp` 和 `Main.qml`，因为它们将属于 `client/`。
- **顶层 CMake 重写**：
  #### [MODIFY] [CMakeLists.txt](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/CMakeLists.txt)
  参考 `src/old/CMakeLists.txt` 重写该文件，剥离具体的 Qt GUI 构建配置，主要管理全局编译选项（C++23、Qt 6.10 需求）并通过 `add_subdirectory` 引入 `shared` 和 `client` 模块。

## 阶段 2：建立底层核心库（Shared 模块）

### Proposed Changes
将 `src/old/shared/` 下的内容复刻到 `src/shared/`。
- #### [NEW] [CMakeLists.txt](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/shared/CMakeLists.txt)
  定义静态库 `gy_shared`。
- #### [NEW] [protocol.h](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/shared/protocol.h)
  预留命名空间与 Type 码集的基础占位。
- #### [NEW] [data_types.h](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/shared/data_types.h)
  声明基础数据结构并使用 `Q_GADGET` 暴露给 QML（如 `PeerInfo`）。
- #### [NEW] [frame_codec.h](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/shared/frame_codec.h) 及 [frame_codec.cpp](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/shared/frame_codec.cpp)
  帧编解码器核心声明及实现。

## 阶段 3：构建客户端核心逻辑（Client 模块）

### Proposed Changes
将 `src/old/client/` 结构及业务逻辑复刻到 `src/client/`。
- #### [NEW] [CMakeLists.txt](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/client/CMakeLists.txt)
  使用 `qt_add_executable` 和 `qt_add_qml_module`，链接 `gy_shared`，定义 QML 模块 URI 为 `cqnu.gridyard.client`。
- #### [NEW] [main.cpp](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/client/main.cpp)
  QGuiApplication 启动并加载自设 QML 模块的入口。
- #### [NEW] [app_controller.h](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/client/core/app_controller.h) 及 [app_controller.cpp](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/client/core/app_controller.cpp)
  在 `client/core/` 目录中建立并实现业务控制器（带有 `QML_SINGLETON` 和 `Q_OBJECT`），暴露应用版本与退出的方法给 QML。
- **预留占位目录**：创建 `src/client/network/` 与 `src/client/ui/` 等用于后续扩展。

## 阶段 4：界面集成与链路验证

### Proposed Changes
- #### [NEW] [Main.qml](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/client/Main.qml)
  从 `src/old/client/Main.qml` 拷贝或实现，必须：
  1. 根对象使用 `ApplicationWindow`，`id` 为 `tw_mainWindow`。
  2. 导入 `cqnu.gridyard.client 1.0` 并在界面中展示 C++ 暴露的版本信息，或者调用相应的 quit 方法。

## 辅助工具复原

- #### [NEW] [for_md.py](file:///run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src/scripts/for_md.py)
  复刻原先的代码归档脚本。

---

## 验证计划

在完成上述四个阶段的代码搬运及创建后，将执行完整的构建和验证测试：

### Automated Tests
```bash
cd /run/media/root/铠侠D/桌面文件/Qt6软件开发资料/考试/GridYard/GridYard/src
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

### Manual Verification
运行生成的可执行程序，并观察控制台输出：
```bash
./build/client/appGridYard
```
期望行为：
- 弹出 960x640 空白窗口，标题显示 `GridYard v0.1.0`，界面包含提示文字。
- 关闭窗口时，控制台输出 `AppController::quit invoked` 或类似的 QML→C++ 交互链路验证日志，这表明全链路打通。

## User Review Required
> [!IMPORTANT]
> 这一步骤实质上是用 `src/old/` 内的模块化代码完全替换当前 `src/` 中简单平铺的文件，请确认上述“四步走”拆分是否完美契合您的实施思路。

## Open Questions
- 是否可以为了简便起见，直接在 Bash 中使用文件系统命令（如 `cp -r`）将 `old` 下的代码平移到 `src/` 各自的结构中，还是您更期望我一步步使用编辑工具逐文件生成这些代码来模拟“从零开始写”的过程？如果能使用 `cp` 会非常迅速且保证代码和旧版本绝对一致。
