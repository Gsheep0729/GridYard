/**
 * @file    TransferTaskCard.qml
 * @version 4.15.2
 * @date    2026-06-17
 * @author  GridYard Team
 * @brief   传输任务卡片
 *
 * 显示单个传输任务的进度、状态、取消按钮。
 *
 * Change Log:
 * [v4.15.2] DuRuoxian   2026-06-17
 * * 优化任务卡片配色、方向标识和长文件名展示
 * [v4.14.2] GY   2026-06-16
 * * 优化删除本地文件确认样式，完成的文件夹任务增加内容下拉行
 * [v4.14.0] GY   2026-06-15
 * * 移除记录操作增加删除已接收本地文件选项
 * [v4.13.3] GY   2026-06-15
 * * 移除进度更新时重复触发的入场动画，由外部持久化文件夹展开状态
 * [v4.13.2] DuRuoxian   2026-06-15
 * * 文件夹下拉栏改为根目录预览
 * [v4.13.1] DuRuoxian   2026-06-15
 * * 修复文件夹显示问题，添加图标区分和展开功能
 * [v4.12.0] DuRuoxian   2026-06-14
 * * 使用共享格式化工具，增加进度、状态和进入过渡
 * [v4.10.0] DuRuoxian   2026-06-13
 * * 改进布局：添加方向标识、时间信息、文件大小、移除记录
 * [v0.2.0] DuRuoxian   2026-06-02
 * * Stage 3：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/FormatUtils.js" as FormatUtils

Frame {
    id: taskCard

    required property string sessionId
    required property string taskType
    required property string taskName
    required property string status
    required property int    progress
    required property var    bytesTransferred
    required property var    totalBytes
    required property bool   isDirectory
    required property var    fileList
    required property bool   canDeleteLocalFile
    property string createdAt: ""
    property string peerDeviceName: ""

    // 展开状态
    property bool expanded: false
    signal expansionRequested(bool expanded)

    // 状态颜色
    readonly property color kRunningColor: "#3B82F6"
    readonly property color kSuccessColor: "#10B981"
    readonly property color kFailedColor:  "#EF4444"
    readonly property color kWaitingColor: "#F59E0B"
    readonly property color kFileBgColor:   "#FFFFFF"
    readonly property color kFolderBgColor: "#F8FAFC"
    readonly property color kFailedBgColor: "#FEF2F2"
    readonly property int kColorDuration: 160
    readonly property int kProgressDuration: 180
    readonly property bool canShowFolderPreview: isDirectory && fileList.length > 0
    readonly property bool isFinished: status === "completed" || status === "failed"
                                       || status === "rejected" || status === "cancelled"

    Layout.fillWidth: true
    implicitHeight: contentColumn.implicitHeight + 20
    height: implicitHeight
    opacity: 1

    // 状态映射
    function statusText(): string {
        switch (status) {
        case "connecting":      return qsTr("连接中...")
        case "waiting_confirm": return qsTr("等待确认")
        case "transferring":    return qsTr("传输中")
        case "completed":       return qsTr("完成")
        case "failed":          return qsTr("失败")
        case "rejected":        return qsTr("已拒绝")
        case "cancelled":       return qsTr("已取消")
        default:                return status
        }
    }

    function statusColor(): color {
        switch (status) {
        case "connecting":
        case "waiting_confirm": return kWaitingColor
        case "transferring":    return kRunningColor
        case "completed":       return kSuccessColor
        case "failed":
        case "rejected":
        case "cancelled":       return kFailedColor
        default:                return "#9CA3AF"
        }
    }

    function backgroundColor(): color {
        if (status === "failed" || status === "rejected" || status === "cancelled") {
            return kFailedBgColor
        }
        return isDirectory ? kFolderBgColor : kFileBgColor
    }

    background: Rectangle {
        radius: 8
        color: taskCard.backgroundColor()
        border.color: taskCard.isFinished ? "#E5E7EB" : taskCard.statusColor()
        border.width: 1

        Behavior on border.color {
            ColorAnimation {
                duration: taskCard.kColorDuration
                easing.type: Easing.OutCubic
            }
        }
    }

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        // 第一行：方向标识 + 文件名 + 状态
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // 方向标识（精简）
            Label {
                text: taskCard.taskType === "send" ? "↑" : "↓"
                color: taskCard.taskType === "send" ? "#3B82F6" : "#8B5CF6"
                font.pixelSize: 16
                font.bold: true
                Layout.alignment: Qt.AlignVCenter
            }

            FileTypeIcon {
                fileName: taskCard.taskName
                isDirectory: taskCard.isDirectory
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }

            // 文件名
            Label {
                text: taskCard.taskName
                font.pixelSize: 14
                font.bold: true
                color: "#111827"
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            // 展开/收起按钮（仅文件夹显示）
            Button {
                icon.name: taskCard.expanded ? "go-down" : "go-next"
                flat: true
                visible: taskCard.canShowFolderPreview && !taskCard.isFinished
                onClicked: taskCard.expansionRequested(!taskCard.expanded)
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                padding: 0
            }

            // 状态标签
            Rectangle {
                Layout.preferredWidth: statusLabel.implicitWidth + 12
                Layout.preferredHeight: statusLabel.implicitHeight + 4
                radius: 4
                color: taskCard.statusColor()

                Behavior on color {
                    ColorAnimation {
                        duration: taskCard.kColorDuration
                        easing.type: Easing.OutCubic
                    }
                }

                Label {
                    id: statusLabel
                    anchors.centerIn: parent
                    text: taskCard.statusText()
                    font.pixelSize: 11
                    color: "#FFFFFF"
                    font.bold: true
                }
            }
        }

        // 第二行：时间 + 文件大小
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                text: taskCard.taskType === "send"
                      ? qsTr("发送给 %1").arg(taskCard.peerDeviceName || qsTr("未知设备"))
                      : qsTr("来自 %1").arg(taskCard.peerDeviceName || qsTr("未知设备"))
                font.pixelSize: 12
                color: "#6B7280"
                elide: Text.ElideRight
            }

            Item { Layout.fillWidth: true }

            Label {
                text: FormatUtils.formatTime(taskCard.createdAt)
                font.pixelSize: 12
                color: "#9CA3AF"
                visible: taskCard.createdAt.length > 0
            }

            Label {
                text: FormatUtils.formatBytes(taskCard.totalBytes)
                font.pixelSize: 12
                color: "#6B7280"
                visible: taskCard.totalBytes > 0
            }
        }

        // 第三行：进度条（仅在传输中或完成时显示）
        ProgressBar {
            from: 0
            to: 100
            value: taskCard.progress
            Layout.fillWidth: true
            visible: taskCard.status === "transferring" || taskCard.status === "completed"

            Behavior on value {
                SmoothedAnimation {
                    duration: taskCard.kProgressDuration
                }
            }
        }

        // 第四行：传输信息 + 操作按钮
        RowLayout {
            Layout.fillWidth: true
            visible: taskCard.status === "transferring" || taskCard.status === "waiting_confirm"

            Label {
                text: FormatUtils.formatBytes(taskCard.bytesTransferred)
                      + " / " + FormatUtils.formatBytes(taskCard.totalBytes)
                font.pixelSize: 12
                color: "#666"
                visible: taskCard.status === "transferring"
            }

            Item { Layout.fillWidth: true }

            // 进度百分比
            Label {
                text: taskCard.progress + "%"
                font.pixelSize: 12
                font.bold: true
                color: taskCard.kRunningColor
                visible: taskCard.status === "transferring"
            }

            // 取消按钮
            Button {
                text: qsTr("取消")
                flat: true
                visible: taskCard.status === "transferring" || taskCard.status === "waiting_confirm"
                onClicked: AppController.transfer.cancelSession(taskCard.sessionId)
            }
        }

        // 完成/失败状态的操作按钮
        RowLayout {
            Layout.fillWidth: true
            visible: taskCard.isFinished

            Item { Layout.fillWidth: true }

            RowLayout {
                spacing: 4

                Button {
                    text: qsTr("移除记录")
                    flat: true
                    onClicked: AppController.transfer.removeSession(taskCard.sessionId)
                }

                ToolButton {
                    icon.name: "view-more-symbolic"
                    display: AbstractButton.IconOnly
                    padding: 4
                    ToolTip.visible: hovered
                    ToolTip.text: enabled ? qsTr("更多移除选项") : qsTr("发送记录或未完成接收记录不能删除本地文件")
                    onClicked: removeMenu.open()

                    Menu {
                        id: removeMenu
                        y: parent.height
                        implicitWidth: 176

                        MenuItem {
                            id: deleteLocalFileMenuItem
                            text: qsTr("删除本地文件")
                            enabled: taskCard.canDeleteLocalFile
                            onTriggered: deleteConfirmDialog.open()

                            contentItem: Label {
                                text: deleteLocalFileMenuItem.text
                                font.pixelSize: 12
                                color: deleteLocalFileMenuItem.enabled ? "#1F2937" : "#9CA3AF"
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 30
            visible: taskCard.canShowFolderPreview && taskCard.isFinished
            color: taskCard.expanded ? "#FFFFFF" : "transparent"
            radius: 6
            border.color: taskCard.expanded ? "#D8DEE8" : "#00000000"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                Label {
                    text: taskCard.expanded ? "⌄" : "›"
                    font.pixelSize: 14
                    color: "#5F6B7A"
                    Layout.preferredWidth: 12
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    text: qsTr("文件夹内容")
                    font.pixelSize: 12
                    font.bold: true
                    color: "#374151"
                }

                Label {
                    text: qsTr("%1 项").arg(taskCard.fileList.length)
                    font.pixelSize: 11
                    color: "#6B7280"
                }

                Item { Layout.fillWidth: true }
            }

            TapHandler {
                onTapped: taskCard.expansionRequested(!taskCard.expanded)
            }
        }

        // 文件列表（展开时显示）
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: taskCard.expanded ? Math.min(contentHeight, 150) : 0
            clip: true
            visible: taskCard.expanded && taskCard.isDirectory

            model: taskCard.fileList

            delegate: RowLayout {
                id: fileRow
                required property string modelData
                width: ListView.view.width
                spacing: 6

                FileTypeIcon {
                    fileName: fileRow.modelData
                    isDirectory: fileRow.modelData.endsWith("/")
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                }

                Label {
                    text: fileRow.modelData
                    font.pixelSize: 11
                    color: "#666666"
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }
    }

    Dialog {
        id: deleteConfirmDialog
        title: qsTr("删除确认")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(360, parent.width - 24)
        padding: 12
        standardButtons: Dialog.No

        background: Rectangle {
            color: "#FFFFFF"
            radius: 8
            border.color: "#D1D5DB"
            border.width: 1
        }

        header: Label {
            text: deleteConfirmDialog.title
            font.pixelSize: 14
            font.bold: true
            color: "#111827"
            padding: 12
            bottomPadding: 0
        }

        ColumnLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: taskCard.isDirectory
                      ? qsTr("确定移除该传输记录，并删除已接收的本地文件夹吗？")
                      : qsTr("确定移除该传输记录，并删除已接收的本地文件吗？")
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                lineHeight: 1.25
                color: "#374151"
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: fileInfoRow.implicitHeight + 16
                color: "#F9FAFB"
                radius: 6
                border.color: "#F3F4F6"

                RowLayout {
                    id: fileInfoRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    FileTypeIcon {
                        fileName: taskCard.taskName
                        isDirectory: taskCard.isDirectory
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                    }

                    Label {
                        text: taskCard.taskName
                        font.pixelSize: 12
                        font.bold: true
                        color: "#1F2937"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "!"
                    font.pixelSize: 11
                    font.bold: true
                    color: "#EF4444"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Layout.preferredWidth: 16
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("此操作将永久删除本地文件，无法撤销。")
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: "#EF4444"
                    font.bold: true
                }
            }
        }

        footer: DialogButtonBox {
            background: Rectangle { color: "transparent" }
            alignment: Qt.AlignRight
            topPadding: 4
            bottomPadding: 8
            rightPadding: 12

            Button {
                text: qsTr("取消")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                flat: true
            }

            Button {
                id: confirmDeleteButton
                text: qsTr("确认删除")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole

                contentItem: Label {
                    text: confirmDeleteButton.text
                    font: confirmDeleteButton.font
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitWidth: 76
                    implicitHeight: 28
                    color: confirmDeleteButton.down ? "#B91C1C" : (confirmDeleteButton.hovered ? "#DC2626" : "#EF4444")
                    radius: 4
                }
            }
        }

        onAccepted: AppController.transfer.removeSessionAndDeleteFile(taskCard.sessionId)
    }

}
