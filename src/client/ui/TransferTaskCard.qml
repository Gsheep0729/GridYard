/**
 * @file    TransferTaskCard.qml
 * @version 4.10.0
 * @date    2026-06-13
 * @author  GridYard Team
 * @brief   传输任务卡片
 *
 * 显示单个传输任务的进度、状态、取消按钮。
 *
 * Change Log:
 * [v4.10.0] DuRuoxian   2026-06-13
 * * 改进布局：添加方向标识、时间信息、文件大小、移除记录
 * [v0.2.0] DuRuoxian   2026-06-02
 * * Stage 3：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0

Frame {
    id: taskCard

    required property string sessionId
    required property string taskType
    required property string taskName
    required property string status
    required property int    progress
    required property int    bytesTransferred
    required property int    totalBytes
    property string createdAt: ""
    property string peerDeviceName: ""

    // 状态颜色
    readonly property color kRunningColor: "#2196F3"
    readonly property color kSuccessColor: "#4CAF50"
    readonly property color kFailedColor:  "#F44336"
    readonly property color kWaitingColor: "#FF9800"
    readonly property color kSendBgColor:  "#E3F2FD"
    readonly property color kRecvBgColor:  "#F3E5F5"

    Layout.fillWidth: true
    height: 100

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

    // 格式化字节数
    function formatBytes(bytes) {
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GB"
    }

    // 格式化时间
    function formatTime(timeStr) {
        if (!timeStr) return ""
        const date = new Date(timeStr)
        return date.toLocaleTimeString(Qt.locale(), "HH:mm")
    }

    background: Rectangle {
        radius: 8
        color: taskType === "send" ? kSendBgColor : kRecvBgColor
        border.color: statusColor()
        border.width: 1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6

        // 第一行：方向标识 + 文件名 + 状态
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // 方向标识（更清晰）
            Rectangle {
                width: 24
                height: 24
                radius: 12
                color: taskType === "send" ? "#2196F3" : "#9C27B0"

                Label {
                    anchors.centerIn: parent
                    text: taskType === "send" ? "↑" : "↓"
                    color: "#FFFFFF"
                    font.pixelSize: 14
                    font.bold: true
                }
            }

            // 文件名
            Label {
                text: taskName
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            // 状态标签
            Rectangle {
                width: statusLabel.implicitWidth + 12
                height: statusLabel.implicitHeight + 6
                radius: 4
                color: statusColor()

                Label {
                    id: statusLabel
                    anchors.centerIn: parent
                    text: statusText()
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
                text: taskType === "send"
                      ? qsTr("发送给 %1").arg(peerDeviceName || qsTr("未知设备"))
                      : qsTr("来自 %1").arg(peerDeviceName || qsTr("未知设备"))
                font.pixelSize: 12
                color: "#666666"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: formatTime(createdAt)
                font.pixelSize: 12
                color: "#888888"
                visible: createdAt.length > 0
            }

            Label {
                text: formatBytes(totalBytes)
                font.pixelSize: 12
                color: "#666666"
                visible: totalBytes > 0
            }
        }

        // 第三行：进度条（仅在传输中或完成时显示）
        ProgressBar {
            from: 0
            to: 100
            value: progress
            Layout.fillWidth: true
            visible: status === "transferring" || status === "completed"
        }

        // 第四行：传输信息 + 操作按钮
        RowLayout {
            Layout.fillWidth: true
            visible: status === "transferring" || status === "waiting_confirm"

            Label {
                text: formatBytes(bytesTransferred) + " / " + formatBytes(totalBytes)
                font.pixelSize: 12
                color: "#666"
                visible: status === "transferring"
            }

            Item { Layout.fillWidth: true }

            // 进度百分比
            Label {
                text: progress + "%"
                font.pixelSize: 12
                font.bold: true
                color: kRunningColor
                visible: status === "transferring"
            }

            // 取消按钮
            Button {
                text: qsTr("取消")
                flat: true
                visible: status === "transferring" || status === "waiting_confirm"
                onClicked: AppController.transfer.cancelSession(sessionId)
            }
        }

        // 完成/失败状态的操作按钮
        RowLayout {
            Layout.fillWidth: true
            visible: status === "completed" || status === "failed" || status === "rejected" || status === "cancelled"

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("移除记录")
                flat: true
                visible: status !== "transferring"
                onClicked: AppController.transfer.removeSession(sessionId)
            }
        }
    }
}
