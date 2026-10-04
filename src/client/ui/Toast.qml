/**
 * @file    Toast.qml
 * @version 7.17.3
 * @date 2026-10-04
 * @author  GridYard Team
 * @brief   底部轻提示
 *
 * 错误与成功共用的非阻塞提示条，自动消失；错误停留更久。
 * 从 Main.qml 拆出，连续提示时重置计时避免被上一条的旧计时提前关掉。
 *
 * Change Log:
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
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
 * * 自 Main.qml 拆出，暴露 show(message, isError) 接口
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../utils/Style.js" as Style

Popup {
    id: toast

    // 弹窗淡入动画时长（与拆出前一致）
    readonly property int kEnterDuration: 180
    property bool isError: true

    function show(message, isError) {
        toast.isError = isError
        toastLabel.text = message
        toast.open()
        toastTimer.restart()  // 连续提示时重置计时，避免第二条被上一条的旧计时提前关掉
    }

    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? parent.height - height - 32 : 0
    width: Math.min(480, parent ? parent.width - 48 : 480)
    padding: 12
    modal: false
    closePolicy: Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation {
            property: "opacity"; from: 0; to: 1
            duration: toast.kEnterDuration
            easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"; from: 1; to: 0
            duration: Style.Motion.fast
        }
    }

    background: Rectangle {
        color: toast.isError ? Style.Color.error : Style.Color.success
        radius: Style.Radius.sm
    }

    contentItem: RowLayout {
        spacing: Style.Space.sm

        Rectangle {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            radius: 9
            color: Style.Color.textOnAccent
            opacity: 0.25

            Label {
                anchors.centerIn: parent
                text: toast.isError ? "!" : "✓"
                color: toast.isError ? Style.Color.error : Style.Color.success
                font.pixelSize: 11
                font.bold: true
            }
        }

        Label {
            id: toastLabel
            Layout.fillWidth: true
            color: Style.Color.textOnAccent
            font.pixelSize: 13
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignVCenter
        }
    }

    Timer {
        id: toastTimer
        interval: toast.isError ? 4500 : 3000
        running: toast.visible
        onTriggered: toast.close()
    }
}
