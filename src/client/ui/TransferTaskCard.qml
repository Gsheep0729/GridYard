/**
 * @file    TransferTaskCard.qml
 * @version 4.13.3
 * @date    2026-06-15
 * @author  GridYard Team
 * @brief   传输任务卡片
 *
 * 显示单个传输任务的进度、状态、取消按钮。
 *
 * Change Log:
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
    property string createdAt: ""
    property string peerDeviceName: ""

    // 展开状态
    property bool expanded: false
    signal expansionRequested(bool expanded)

    // 状态颜色
    readonly property color kRunningColor: "#2196F3"
    readonly property color kSuccessColor: "#4CAF50"
    readonly property color kFailedColor:  "#F44336"
    readonly property color kWaitingColor: "#FF9800"
    readonly property color kFileBgColor:   "#EAF4FF"
    readonly property color kFolderBgColor: "#FFF6DD"
    readonly property color kFailedBgColor: "#FFEBEE"
    readonly property int kColorDuration: 160
    readonly property int kProgressDuration: 180

    Layout.fillWidth: true
    implicitHeight: contentColumn.implicitHeight + 24
    height: implicitHeight
    opacity: 1

    // 状态映射
    function statusText() {
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

    function statusColor() {
        switch (status) {
        case "connecting":
        case "waiting_confirm": return kWaitingColor
        case "transferring":    return kRunningColor
        case "completed":       return kSuccessColor
        case "failed":
        case "rejected":
        case "cancelled":       return kFailedColor
        default:                return "#999"
        }
    }

    function backgroundColor() {
        if (status === "failed" || status === "rejected" || status === "cancelled") {
            return kFailedBgColor
        }
        return isDirectory ? kFolderBgColor : kFileBgColor
    }

    background: Rectangle {
        radius: 8
        color: taskCard.backgroundColor()
        border.color: taskCard.statusColor()
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
        anchors.margins: 12
        spacing: 6

        // 第一行：方向标识 + 文件名 + 状态
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // 方向标识（更清晰）
            Rectangle {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                radius: 12
                color: taskCard.taskType === "send" ? "#2196F3" : "#9C27B0"

                Label {
                    anchors.centerIn: parent
                    text: taskCard.taskType === "send" ? "↑" : "↓"
                    color: "#FFFFFF"
                    font.pixelSize: 14
                    font.bold: true
                }
            }

            FileTypeIcon {
                fileName: taskCard.taskName
                isDirectory: taskCard.isDirectory
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
            }

            // 文件名
            Label {
                text: taskCard.taskName
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            // 展开/收起按钮（仅文件夹显示）
            Button {
                icon.name: taskCard.expanded ? "go-down" : "go-next"
                flat: true
                visible: taskCard.isDirectory && taskCard.fileList.length > 0
                onClicked: taskCard.expansionRequested(!taskCard.expanded)
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                padding: 0
            }

            // 状态标签
            Rectangle {
                Layout.preferredWidth: statusLabel.implicitWidth + 12
                Layout.preferredHeight: statusLabel.implicitHeight + 6
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
                color: "#666666"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: FormatUtils.formatTime(taskCard.createdAt)
                font.pixelSize: 12
                color: "#888888"
                visible: taskCard.createdAt.length > 0
            }

            Label {
                text: FormatUtils.formatBytes(taskCard.totalBytes)
                font.pixelSize: 12
                color: "#666666"
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
            visible: taskCard.status === "completed" || taskCard.status === "failed"
                     || taskCard.status === "rejected" || taskCard.status === "cancelled"

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("移除记录")
                flat: true
                visible: taskCard.status !== "transferring"
                onClicked: AppController.transfer.removeSession(taskCard.sessionId)
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

}
