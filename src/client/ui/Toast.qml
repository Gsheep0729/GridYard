/**
 * @file    Toast.qml
 * @version 7.19.0
 * @date 2026-11-02
 * @author  GridYard Team
 * @brief   底部轻提示
 *
 * 错误与成功共用的非阻塞提示条，自动消失；错误停留更久。
 * 从 Main.qml 拆出，连续提示时重置计时避免被上一条的旧计时提前关掉。
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
    // 底部让位（U2-17）：会话页输入区实际高度由调用方注入，提示条不再压住
    // 输入区的 + 入口与占位文字；无会话页（主页占位态）时为 0，回落贴底原位。
    // 不叫 bottomInset：QQuickPopup 已有同名 final 成员，遮蔽会被 qmllint 拒绝
    property real bottomClearance: 0

    function show(message, isError) {
        toast.isError = isError
        toastLabel.text = message
        toast.open()
        toastTimer.restart()  // 连续提示时重置计时，避免第二条被上一条的旧计时提前关掉
    }

    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? parent.height - height - 32 - bottomClearance : 0
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
            // 0.25→0.16（U2-17）：原白圆底几乎看不见，降透明度后圆底可辨
            opacity: 0.16

            Label {
                anchors.centerIn: parent
                text: toast.isError ? "!" : "✓"
                // 深色徽标字（U2-17）：浅色即提案色 #B91C1C / #047857，深浅由
                // Style 双表派发（深色下状态前景压白圆底会糊，维持白字）
                color: toast.isError ? Style.Color.toastGlyphErr
                                     : Style.Color.toastGlyphOk
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
