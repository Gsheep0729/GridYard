/**
 * @file    AcceptDialog.qml
 * @version 6.6.2
 * @date    2026-06-17
 * @author  GridYard Team
 * @brief   接收确认弹窗
 *
 * 显示发送方设备名、文件名、文件大小。
 * 用户点击"接受"或"拒绝"后调用 TransferSessionManager。
 *
 * Change Log:
 * [v6.6.2] GY   2026-06-25
 * * 同步文件头版本与当前主版本
 * [v4.16.0] DuRuoxian   2026-06-18
 * * 使用 Style.js 统一样式常量
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
import "../utils/Style.js" as Style

Dialog {
    id: acceptDialog

    title: acceptDialog.isDirectory ? qsTr("接收文件夹") : qsTr("接收文件")
    modal: true
    anchors.centerIn: parent
    width: 460  // 固定宽度保证文件信息卡片不会过窄

    // 会话信息（由 TransferSessionManager 的 receiveRequestReceived 信号填充）
    property string sessionId: ""
    property string senderName: ""
    property string fileName: ""
    property real   fileSize: 0
    property int    totalFiles: 1
    property real   totalBytes: 0
    property bool   isDirectory: false  // 为 true 时展示目录预览而非单文件大小
    property var    fileList: []  // 文件夹场景下的根目录条目预览列表

    contentItem: ColumnLayout {
        spacing: Style.Space.lg

        // 发送方提示
        Label {
            text: qsTr("来自 ") + acceptDialog.senderName + qsTr(" 的传输请求")
            font.pixelSize: 16
            font.bold: true
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Style.Color.textMain
        }

        // 文件信息卡片：文件夹场景显示总文件数和总大小，单文件场景只显示文件大小
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: fileInfoLayout.implicitHeight + 24
            color: Style.Color.surfaceSoft
            radius: Style.Radius.sm
            border.color: Style.Color.border
            border.width: 1

            ColumnLayout {
                id: fileInfoLayout
                anchors.fill: parent
                anchors.margins: Style.Space.md
                spacing: Style.Space.sm

                RowLayout {
                    Label {
                        text: acceptDialog.isDirectory ? qsTr("文件夹名：") : qsTr("文件名：")
                        font.bold: true
                        color: Style.Color.textSecondary
                        font.pixelSize: 13
                    }
                    Label {
                        text: acceptDialog.fileName
                        color: Style.Color.textMain
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
                        color: Style.Color.textSecondary
                        font.pixelSize: 13
                    }
                    Label {
                        text: FormatUtils.formatBytes(acceptDialog.fileSize)
                        color: Style.Color.textMain
                        font.pixelSize: 13
                    }
                }

                RowLayout {
                    visible: acceptDialog.isDirectory
                    Label {
                        text: qsTr("总文件数：")
                        font.bold: true
                        color: Style.Color.textSecondary
                        font.pixelSize: 13
                    }
                    Label {
                        text: acceptDialog.totalFiles
                        color: Style.Color.textMain
                        font.pixelSize: 13
                    }
                }

                RowLayout {
                    visible: acceptDialog.isDirectory
                    Label {
                        text: qsTr("总大小：")
                        font.bold: true
                        color: Style.Color.textSecondary
                        font.pixelSize: 13
                    }
                    Label {
                        text: FormatUtils.formatBytes(acceptDialog.totalBytes)
                        color: Style.Color.textMain
                        font.pixelSize: 13
                    }
                }

                Label {
                    visible: acceptDialog.isDirectory
                    text: qsTr("文件夹内容：")
                    font.bold: true
                    color: Style.Color.textSecondary
                    font.pixelSize: 13
                    Layout.topMargin: Style.Space.xs
                }

                ListView {
                    Layout.fillWidth: true
                    // 限制预览高度避免弹窗过长，最多显示 180px 内的条目
                    Layout.preferredHeight: acceptDialog.isDirectory
                                            ? Math.min(contentHeight, 180) : 0
                    visible: acceptDialog.isDirectory
                    clip: true
                    spacing: Style.Space.xs
                    model: acceptDialog.fileList  // 根目录条目预览，由 TransferSessionManager 构建

                    delegate: RowLayout {
                        id: previewRow
                        required property string modelData
                        width: ListView.view.width
                        spacing: Style.Space.xs

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
                            color: Style.Color.textMuted
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
            color: Style.Color.textSecondary
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
        AppController.transferController.acceptReceiveSession(sessionId)
    }

    onRejected: {
        // 用户拒绝
        AppController.transferController.rejectReceiveSession(sessionId)
    }
}
