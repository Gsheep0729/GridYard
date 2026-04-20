/**
* @file    Main.qml
* @date    2026-05-24
* @author  GY
* @brief   GridYard 客户端根窗口
*
* Stage 0 仅空白 ApplicationWindow，验证 C++↔QML 路径与 AppController
* 单例可用。标题通过 AppController.applicationName/Version 绑定，
* 关窗触发 AppController.quit()——后续阶段在此处添加设备列表、
* 传输面板、聊天界面等业务组件。
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0：空白窗口框架
* [v0.2] GY   2026-06-02
* * Stage 1：添加 test 按钮验证 C++↔QML 通信
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import cqnu.gridyard.client 1.0

ApplicationWindow {
    id: tw_mainWindow

    width:   960
    height:  640
    visible: true
    title:   "%1 v%2".arg(AppController.applicationName)
                     .arg(AppController.applicationVersion)

    onClosing: AppController.quit()

    Column {
        anchors.centerIn: parent
        spacing: 20

        Label {
            text: qsTr("GridYard 骨架就绪 — Stage 1 通信层已就位")
            font.pixelSize: 16
            color: "#888"
        }

        Button {
            text: qsTr("测试 C++↔QML 通信")
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: AppController.test()
        }
    }
}
