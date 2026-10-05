/**
 * @file    HomePlaceholder.qml
 * @version 7.19.0
 * @date 2026-10-05
 * @author  GridYard Team
 * @brief   未选中设备的空状态占位
 *
 * 会话区没有选中设备时展示的品牌占位与引导文案。
 * 从 Main.qml 拆出，纯视觉组件。
 */

import QtQuick
import QtQuick.Controls
import "../utils/Style.js" as Style

Item {
    id: homePlaceholder

    Column {
        anchors.centerIn: parent
        spacing: Style.Space.lg
        width: Math.min(parent.width - 80, 420)

        Label {
            text: "GridYard"
            color: Style.Color.primary
            font.pixelSize: 48
            font.bold: true
            opacity: 0.16
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Label {
            text: qsTr("选择一台设备开始会话")
            font.pixelSize: 20
            font.bold: true
            color: Style.Color.textMain
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Label {
            text: qsTr("发送文件，之后也会在这里查看聊天消息")
            color: Style.Color.textMuted
            font.pixelSize: 14
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }
    }
}
