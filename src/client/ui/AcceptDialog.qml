/**
 * @file    AcceptDialog.qml
 * @date    2026-06-02
 * @author  GY
 * @brief   接收确认弹窗
 *
 * 显示发送方设备名、文件名、文件大小。
 * 用户点击"接受"或"拒绝"后调用 TransferSessionManager。
 *
 * Change Log:
 * [v0.1] GY   2026-06-02
 * * Stage 3：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0

Dialog {
    id: tw_acceptDialog

    title: qsTr("接收文件")
    modal: true
    anchors.centerIn: parent
    width: 400

    // 会话信息
    property string sessionId: ""
    property string senderName: ""
    property string fileName: ""
    property int    fileSize: 0

    contentItem: ColumnLayout {
        spacing: 16

        // 发送方信息
        GroupBox {
            title: qsTr("发送方")
            Layout.fillWidth: true

            Label {
                text: tw_acceptDialog.senderName
                font.pixelSize: 14
                font.bold: true
            }
        }

        // 文件信息
        GroupBox {
            title: qsTr("文件信息")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Label { text: qsTr("文件名："); font.bold: true }
                    Label { text: tw_acceptDialog.fileName }
                }

                RowLayout {
                    Label { text: qsTr("大小："); font.bold: true }
                    Label { text: formatFileSize(tw_acceptDialog.fileSize) }
                }
            }
        }

        // 提示信息
        Label {
            text: qsTr("是否接受此文件？")
            font.pixelSize: 14
            Layout.alignment: Qt.AlignHCenter
        }
    }

    // 底部按钮
    standardButtons: Dialog.Yes | Dialog.No

    onAccepted: {
        // 用户接受
        TransferSessionManager.acceptReceiveSession(sessionId)
    }

    onRejected: {
        // 用户拒绝
        TransferSessionManager.rejectReceiveSession(sessionId)
    }

    // 格式化文件大小
    function formatFileSize(bytes) {
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GB"
    }
}
