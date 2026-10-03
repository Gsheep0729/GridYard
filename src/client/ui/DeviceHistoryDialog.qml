/**
 * @file    DeviceHistoryDialog.qml
 * @version 7.15.15
 * @date    2026-10-04
 * @author  GY
 * @brief   设备历史记录对话框
 *
 * 展示当前设备的传输历史（复用 TransferHistoryView），
 * 并提供清空该设备聊天记录的入口。
 *
 * Change Log:
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
 * [v7.15.3] GY   2026-10-03
* * 页眉改用 DialogHeader 组件
* [v7.11.0] GY   2026-10-02
 * * 初始版本，为跨重启的聊天与传输历史提供查看和清理入口
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Dialog {
    id: deviceHistoryDialog

    required property string deviceId
    required property string deviceName

    title: qsTr("与 %1 的历史记录").arg(deviceName)
    modal: true
    anchors.centerIn: parent
    width: Math.min(600, parent ? parent.width - 48 : 600)
    height: Math.min(560, parent ? parent.height - 48 : 560)
    padding: 0

    background: Rectangle {
        color: Style.Color.window
        radius: Style.Radius.md
        border.color: Style.Color.border
    }

    header: DialogHeader {
        title: deviceHistoryDialog.title
        subtitle: qsTr("保存在本机的历史，跨重启保留；删除记录不影响任何文件。")
        onCloseClicked: deviceHistoryDialog.close()
    }

    contentItem: ColumnLayout {
        spacing: 0

        // 聊天记录管理行
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Style.Space.lg
            spacing: Style.Space.md

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    text: qsTr("聊天记录")
                    font.pixelSize: 14
                    font.bold: true
                    color: Style.Color.textMain
                }

                Label {
                    text: qsTr("聊天内容已在会话页显示；清空后无法恢复。")
                    font.pixelSize: 12
                    color: Style.Color.textMuted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            Button {
                text: qsTr("清空聊天记录")
                flat: true
                onClicked: clearChatConfirmDialog.open()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Style.Color.border
        }

        // 传输历史列表
        TransferHistoryView {
            id: historyView
            Layout.fillWidth: true
            Layout.fillHeight: true
            peerDeviceId: deviceHistoryDialog.deviceId
        }
    }

    // 清空聊天记录确认
    Dialog {
        id: clearChatConfirmDialog
        modal: true
        anchors.centerIn: parent
        width: 380
        title: qsTr("清空聊天记录")

        Label {
            text: qsTr("确定清空与 %1 的全部聊天记录吗？此操作只删除本机记录，无法撤销。")
                      .arg(deviceHistoryDialog.deviceName)
            wrapMode: Text.Wrap
            font.pixelSize: 13
            color: Style.Color.textSecondary
        }

        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: AppController.historyController.deleteConversation(deviceHistoryDialog.deviceId)
    }
}
