/**
* @file    Main.qml
* @date    2026-05-24
* @author  GY
* @brief   GridYard 客户端根窗口
*
* 标题通过 AppController.applicationName/Version 绑定，
* 关窗触发 AppController.quit()。
* 左侧显示在线设备列表，右侧预留传输面板区域。
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0：空白窗口框架
* [v0.2] GY   2026-06-02
* * Stage 1：添加 test 按钮验证 C++↔QML 通信
* [v0.3] GY   2026-06-02
* * Stage 2：嵌入设备列表，实现左右分栏布局
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0

ApplicationWindow {
    id: tw_mainWindow

    width:   960
    height:  640
    visible: true
    title:   "%1 v%2".arg(AppController.applicationName)
                     .arg(AppController.applicationVersion)

    onClosing: AppController.quit()

    // 左右分栏布局
    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // 左侧：设备列表
        PeerListView {
            Layout.preferredWidth: 280
            Layout.fillHeight: true

            onDeviceSelected: function(deviceId) {
                console.log("选中设备:", deviceId)
            }
        }

        // 右侧：传输面板（Stage 3 填充）
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Label {
                anchors.centerIn: parent
                text: qsTr("传输面板 — Stage 3 填充")
                font.pixelSize: 16
                color: "#888"
            }
        }
    }
}
