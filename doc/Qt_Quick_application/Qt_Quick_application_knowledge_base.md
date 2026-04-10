# 项目: Qt_Quick_application

## 项目目录结构
```text
Qt_Quick_application/
├── CMakeLists.txt
├── Main.qml
└── main.cpp
```

---

## 项目特征总结
> [!NOTE]
> 此处可填写项目架构、核心技术栈及 AI 提示词（Prompt）。




---

## 构建配置文件
---

### File: `CMakeLists.txt`
<file_block path="CMakeLists.txt" type="build">

```cmake
cmake_minimum_required(VERSION 4.2.3)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
# cmake_minimum_required(VERSION 4.3.2)
# set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD ON)

project(Qt_Quick_application VERSION 0.1 LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(appQt_Quick_application  WIN32)
qt_add_qml_module(appQt_Quick_application
    URI Qt_Quick_application
    VERSION 1.0
    SOURCES     main.cpp
    QML_FILES   Main.qml
)
target_compile_features(appQt_Quick_application PRIVATE cxx_std_23)
target_link_libraries(appQt_Quick_application
    PRIVATE Qt6::Quick
)

set(CMAKE_INSTALL_PREFIX "/opt/${PROJECT_NAME}" CACHE PATH "Install path prefix" FORCE)
install(TARGETS appQt_Quick_application
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# C++ Modules support
set(QT_QUICK_APPLICATION_MODULE_FILES
    # math.cppm                             # 模块文件
)
source_group("Qt_Quick_application" FILES ${QT_QUICK_APPLICATION_MODULE_FILES})
target_sources(appQt_Quick_application
    PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS   ${CMAKE_CURRENT_SOURCE_DIR}
        FILES  ${QT_QUICK_APPLICATION_MODULE_FILES}
)

```
</file_block>

---

## 项目源文件
---

### 2. 源文件（Source Files）
---

#### File: `main.cpp`
<file_block path="main.cpp" type="source">

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Qt_Quick_application", "Main");

    return QGuiApplication::exec();
}

```
</file_block>

---

### 4. QML/JavaScript 资源文件（QML/JS Files）
---

#### File: `Main.qml`
<file_block path="Main.qml" type="qml">

```qml
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    width: 640
    height: 480
    visible: true
    title: qsTr("hello, world")
    menuBar: MenuBar {
        Menu {
            title: qsTr("File")
            MenuItem {
                text: qsTr("&Open")
                onTriggered: console.log("Open action triggered");
            }
            MenuItem {
                text: qsTr("Exit")
                onTriggered: Qt.quit();
            }
        }
    }

    //Content Area
    TextArea {
        text: qsTr("hello, world")
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
    }
}

```
</file_block>

---

