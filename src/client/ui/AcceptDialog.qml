/**
 * @file    AcceptDialog.qml
 * @version 4.15.2
 * @date    2026-06-17
 * @author  GridYard Team
 * @brief   接收确认弹窗
 *
 * 显示发送方设备名、文件名、文件大小。
 * 用户点击"接受"或"拒绝"后调用 TransferSessionManager。
 *
 * Change Log:
 * [v4.15.2] DuRuoxian   2026-06-17
 * * 重构接收确认弹窗视觉层级，突出文件信息和接受操作
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

        // 发送方提示
        Label {
            text: qsTr("来自 ") + acceptDialog.senderName + qsTr(" 的传输请求")
            font.pixelSize: 16
            font.bold: true
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#111827"
        }

        // 文件信息卡片
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: fileInfoLayout.implicitHeight + 24
            color: "#F9FAFB"
            radius: 8
            border.color: "#E5E7EB"
            border.width: 1

            ColumnLayout {
                id: fileInfoLayout
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Label {
                        text: acceptDialog.isDirectory ? qsTr("文件夹名：") : qsTr("文件名：")
                        font.bold: true
                        color: "#4B5563"
                        font.pixelSize: 13
                    }
                    Label {
                        text: acceptDialog.fileName
                        color: "#111827"
                        font.pixelSize: 13
                        font.bold: true
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    visible: !acceptDialog.isDirectory
                    Label {
                        text: qsTr("大小：")
                        font.bold: true
                        color: "#4B5563"
                        font.pixelSize: 13
                    }
                    Label {
                        text: FormatUtils.formatBytes(acceptDialog.fileSize)
                        color: "#111827"
                        font.pixelSize: 13
                    }
                }

                RowLayout {
                    visible: acceptDialog.isDirectory
                    Label {
                        text: qsTr("总文件数：")
                        font.bold: true
                        color: "#4B5563"
                        font.pixelSize: 13
                    }
                    Label {
                        text: acceptDialog.totalFiles
                        color: "#111827"
                        font.pixelSize: 13
                    }
                }

                RowLayout {
                    visible: acceptDialog.isDirectory
                    Label {
                        text: qsTr("总大小：")
                        font.bold: true
                        color: "#4B5563"
                        font.pixelSize: 13
                    }
                    Label {
                        text: FormatUtils.formatBytes(acceptDialog.totalBytes)
                        color: "#111827"
                        font.pixelSize: 13
                    }
                }

                Label {
                    visible: acceptDialog.isDirectory
                    text: qsTr("文件夹内容：")
                    font.bold: true
                    color: "#4B5563"
                    font.pixelSize: 13
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
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                        }

                        Label {
                            text: previewRow.modelData
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            color: "#6B7280"
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
            color: "#374151"
            Layout.alignment: Qt.AlignHCenter
        }
    }

    footer: DialogButtonBox {
        background: Rectangle { color: "transparent" }
        alignment: Qt.AlignRight
        topPadding: 4
        bottomPadding: 16
        rightPadding: 16

        Button {
            text: qsTr("拒绝")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            flat: true
        }

        Button {
            id: acceptButton
            text: qsTr("接受")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            highlighted: true
        }
    }

    onAccepted: {
        // 用户接受
        AppController.transfer.acceptReceiveSession(sessionId)
    }

    onRejected: {
        // 用户拒绝
        AppController.transfer.rejectReceiveSession(sessionId)
    }
}
