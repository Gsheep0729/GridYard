/**
 * @file    RelayConfirmDialog.qml
 * @version 7.15.10
 * @date    2026-10-03
 * @author  GridYard Team
 * @brief   中继确认弹窗
 *
 * 直连失败且策略为询问后中继时展示，用户选择只经信号回传，
 * 策略判断已在 TransferSessionManager 内完成，本组件为纯弹窗。
 * 从 Main.qml 拆出，暴露 openFor(sessionId) 与两个选择信号。
 *
 * Change Log:
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.15.4] GY   2026-10-03
 * * 自 Main.qml 拆出，暴露 openFor(sessionId)、relayChosen/cancelChosen 信号
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../utils/Style.js" as Style

Dialog {
    id: relayConfirmDialog

    title: qsTr("直连失败")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 48 : 420)
    padding: 20

    property string sessionId: ""

    // 用户选择经信号回传，由使用方调用控制器入口
    signal relayChosen(string sessionId)
    signal cancelChosen(string sessionId)

    function openFor(sessionId) {
        relayConfirmDialog.sessionId = sessionId
        relayConfirmDialog.open()
    }

    contentItem: ColumnLayout {
        spacing: 14
        anchors.fill: parent

        Label {
            text: qsTr("与目标设备直连失败，是否通过中继服务器转发本次传输？转发速度可能受限于服务器带宽。")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("暂不处理将在约 2 分钟后自动取消传输，也可以随时在任务卡片上手动取消。")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.pixelSize: 12
            color: Style.Color.textMuted
        }
    }

    footer: RowLayout {
        spacing: 10
        anchors.margins: 16

        Item {
            Layout.fillWidth: true
        }
        Button {
            text: qsTr("使用中继")
            onClicked: {
                relayConfirmDialog.relayChosen(relayConfirmDialog.sessionId)
                relayConfirmDialog.close()
            }
        }
        Button {
            text: qsTr("取消传输")
            onClicked: {
                relayConfirmDialog.cancelChosen(relayConfirmDialog.sessionId)
                relayConfirmDialog.close()
            }
        }
    }
}
