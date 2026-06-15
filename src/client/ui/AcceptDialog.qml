/**
 * @file    AcceptDialog.qml
 * @version 4.13.2
 * @date    2026-06-15
 * @author  GridYard Team
 * @brief   接收确认弹窗
 *
 * 显示发送方设备名、文件名、文件大小。
 * 用户点击"接受"或"拒绝"后调用 TransferSessionManager。
 *
 * Change Log:
 * [v4.13.2] DuRuoxian   2026-06-15
 * * 文件夹确认信息改为文件夹名、总大小和目录预览
 * [v4.12.0] DuRuoxian   2026-06-14
 * * 使用共享文件大小格式化工具
 * [v4.3.4] DuRuoxian   2026-06-04
 * * 支持多文件信息显示（总文件数、总大小）
 * [v0.2.0] DuRuoxian   2026-06-02
 * * Stage 3：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/FormatUtils.js" as FormatUtils

Dialog {
    id: acceptDialog

    title: acceptDialog.isDirectory ? qsTr("接收文件夹") : qsTr("接收文件")
    modal: true
    anchors.centerIn: parent
    width: 460

    // 会话信息
    property string sessionId: ""
    property string senderName: ""
    property string fileName: ""
    property var    fileSize: 0
    property int    totalFiles: 1
    property var    totalBytes: 0
    property bool   isDirectory: false
    property var    fileList: []

    contentItem: ColumnLayout {
        spacing: 16

        // 发送方信息
        GroupBox {
            title: qsTr("发送方")
            Layout.fillWidth: true

            Label {
                text: acceptDialog.senderName
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
                    Label {
                        text: acceptDialog.isDirectory ? qsTr("文件夹名：") : qsTr("文件名：")
                        font.bold: true
                    }
                    Label { text: acceptDialog.fileName }
                }

                RowLayout {
                    visible: !acceptDialog.isDirectory
                    Label { text: qsTr("大小："); font.bold: true }
                    Label { text: FormatUtils.formatBytes(acceptDialog.fileSize) }
                }

                RowLayout {
                    visible: acceptDialog.isDirectory
                    Label { text: qsTr("总文件数："); font.bold: true }
                    Label { text: acceptDialog.totalFiles }
                }

                RowLayout {
                    visible: acceptDialog.isDirectory
                    Label { text: qsTr("总大小："); font.bold: true }
                    Label { text: FormatUtils.formatBytes(acceptDialog.totalBytes) }
                }

                Label {
                    visible: acceptDialog.isDirectory
                    text: qsTr("文件夹内容：")
                    font.bold: true
                    Layout.topMargin: 4
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: acceptDialog.isDirectory
                                            ? Math.min(contentHeight, 180) : 0
                    visible: acceptDialog.isDirectory
                    clip: true
                    spacing: 4
                    model: acceptDialog.fileList

                    delegate: RowLayout {
                        id: previewRow
                        required property string modelData
                        width: ListView.view.width
                        spacing: 6

                        FileTypeIcon {
                            fileName: previewRow.modelData
                            isDirectory: previewRow.modelData.endsWith("/")
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                        }

                        Label {
                            text: previewRow.modelData
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            color: "#555555"
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        // 提示信息
        Label {
            text: acceptDialog.isDirectory
                  ? qsTr("是否接受此文件夹？")
                  : qsTr("是否接受此文件？")
            font.pixelSize: 14
            Layout.alignment: Qt.AlignHCenter
        }
    }

    // 底部按钮
    standardButtons: Dialog.Yes | Dialog.No

    onAccepted: {
        // 用户接受
        AppController.transfer.acceptReceiveSession(sessionId)
    }

    onRejected: {
        // 用户拒绝
        AppController.transfer.rejectReceiveSession(sessionId)
    }
}
