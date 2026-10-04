/**
 * @file    HomePlaceholder.qml
 * @version 7.17.2
 * @date    2026-10-04
 * @author  GridYard Team
 * @brief   未选中设备的空状态占位
 *
 * 会话区没有选中设备时展示的品牌占位与引导文案。
 * 从 Main.qml 拆出，纯视觉组件。
 *
 * Change Log:
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
 * [v7.15.19] GY   2026-10-04
 * * 版本头对齐到 v7.15.19
 * [v7.15.18] GY   2026-10-04
 * * 版本头对齐到 v7.15.18
 * [v7.15.17] GY   2026-10-04
 * * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
 * [v7.15.13] GY   2026-10-04
 * * 版本头对齐到 v7.15.13
 * [v7.15.12] GY   2026-10-03
 * 版本头对齐到 v7.15.12
 * [v7.15.11] GY   2026-10-03
 * * 版本头对齐到 v7.15.11
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.15.4] GY   2026-10-03
 * * 自 Main.qml 拆出
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
