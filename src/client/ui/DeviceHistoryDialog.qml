/**
 * @file    DeviceHistoryDialog.qml
 * @version 7.19.0
 * @date 2026-11-02
 * @author  GY
 * @brief   设备历史记录对话框
 *
 * 展示当前设备的传输历史（复用 TransferHistoryView），
 * 并提供清空该设备聊天记录的入口。
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
    // 弹窗宽度三档（U2-31）：设置/历史类归宽档 680
    width: Math.min(680, parent ? parent.width - 48 : 680)
    // 高度随内容（U2-33）：空列表时不再撑出 560px 大段空白，内容多时封顶
    // 560 走列表内部滚动；Dialog 的 implicitHeight 含页眉与内容自然高且不
    // 依赖 height，绑定无回环；窗口高度不足时仍按 parent.height - 48 收
    height: Math.min(parent ? parent.height - 48 : 560, 560, implicitHeight)
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

        // 分隔线左右内缩（U2-32）：与上下两段内容的 16px 边距对齐收线，
        // 不再满幅切进弹窗圆角
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Style.Space.lg
            Layout.rightMargin: Style.Space.lg
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
        // 弹窗宽度三档（U2-31）：确认/提示类归窄档 420
        width: 420
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
