/**
 * @file    TransferTaskCard.qml
 * @date    2026-06-02
 * @author  GY
 * @brief   传输任务卡片
 *
 * 显示单个传输任务的进度、状态、取消按钮。
 *
 * Change Log:
 * [v0.1] GY   2026-06-02
 * * Stage 3：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0

Frame {
    id: tw_taskCard

    required property string sessionId
    required property string taskType
    required property string taskName
    required property string status
    required property int    progress
    required property int    bytesTransferred
    required property int    totalBytes

    // 状态颜色
    readonly property color kRunningColor: "#2196F3"
    readonly property color kSuccessColor: "#4CAF50"
    readonly property color kFailedColor:  "#F44336"
    readonly property color kWaitingColor: "#FF9800"

    Layout.fillWidth: true
    height: 80

    // 状态映射
    function statusText() {
        switch (status) {
        case "connecting":     return qsTr("连接中...")
        case "waiting_confirm": return qsTr("等待确认")
        case "transferring":   return qsTr("传输中")
        case "completed":      return qsTr("完成")
        case "failed":         return qsTr("失败")
        case "rejected":       return qsTr("已拒绝")
        case "cancelled":      return qsTr("已取消")
        default:               return status
        }
    }

    function statusColor() {
        switch (status) {
        case "connecting":
        case "waiting_confirm": return kWaitingColor
        case "transferring":   return kRunningColor
        case "completed":      return kSuccessColor
        case "failed":
        case "rejected":
        case "cancelled":      return kFailedColor
        default:               return "#999"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // 任务名称和状态
        RowLayout {
            Layout.fillWidth: true

            // 任务类型图标
            Label {
                text: taskType === "send" ? qsTr("↑") : qsTr("↓")
                font.pixelSize: 16
                font.bold: true
                color: statusColor()
            }

            // 任务名称
            Label {
                text: taskName
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            // 状态标签
            Label {
                text: statusText()
                font.pixelSize: 12
                color: statusColor()
            }

            // 取消按钮（仅在传输中显示）
            Button {
                text: qsTr("取消")
                visible: status === "transferring"
                onClicked: AppController.transfer.cancelSession(sessionId)
            }
        }

        // 进度条
        ProgressBar {
            from: 0
            to: 100
            value: progress
            Layout.fillWidth: true
            visible: status === "transferring" || status === "completed"
        }

        // 传输信息
        RowLayout {
            Layout.fillWidth: true
            visible: status === "transferring"

            Label {
                text: formatBytes(bytesTransferred) + " / " + formatBytes(totalBytes)
                font.pixelSize: 12
                color: "#666"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: progress + "%"
                font.pixelSize: 12
                font.bold: true
                color: kRunningColor
            }
        }
    }

    // 格式化字节数
    function formatBytes(bytes) {
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GB"
    }
}
