/**
 * @file    TransferTaskCard.qml
 * @version 4.12.0
 * @date    2026-06-14
 * @author  GridYard Team
 * @brief   传输任务卡片
 *
 * 显示单个传输任务的进度、状态、取消按钮。
 *
 * Change Log:
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
    required property int    bytesTransferred
    required property int    totalBytes
    property bool   isDirectory: false
    property var    fileList: []
    property string createdAt: ""
    property string peerDeviceName: ""

    // 展开状态
    property bool expanded: false

    // 状态颜色
    readonly property color kRunningColor: "#2196F3"
    readonly property color kSuccessColor: "#4CAF50"
    readonly property color kFailedColor:  "#F44336"
    readonly property color kWaitingColor: "#FF9800"
    readonly property color kSendBgColor:  "#E3F2FD"
    readonly property color kRecvBgColor:  "#F3E5F5"
    readonly property int kColorDuration: 160
    readonly property int kEnterDuration: 200
    readonly property int kProgressDuration: 180

    Layout.fillWidth: true
    height: 120
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

    background: Rectangle {
        radius: 8
        color: taskCard.taskType === "send" ? taskCard.kSendBgColor : taskCard.kRecvBgColor
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

            // 文件/文件夹图标
            Label {
                text: isDirectory ? "📁" : "📄"
                font.pixelSize: 16
            }

            // 文件名
            Label {
                text: taskName
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            // 展开/收起按钮（仅文件夹显示）
            Button {
                text: expanded ? "▼" : "▶"
                flat: true
                visible: isDirectory && fileList.length > 0
                onClicked: expanded = !expanded
                width: 24
                height: 24
                padding: 0
            }

            // 状态标签
            Rectangle {
                width: statusLabel.implicitWidth + 12
                height: statusLabel.implicitHeight + 6
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
                text: FormatUtils.formatTime(taskCard.createdAt)
                font.pixelSize: 12
                color: "#888888"
                visible: createdAt.length > 0
            }

            Label {
                text: FormatUtils.formatBytes(taskCard.totalBytes)
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

            Behavior on value {
                SmoothedAnimation {
                    duration: taskCard.kProgressDuration
                }
            }
        }

        // 第四行：传输信息 + 操作按钮
        RowLayout {
            Layout.fillWidth: true
            visible: status === "transferring" || status === "waiting_confirm"

            Label {
                text: FormatUtils.formatBytes(taskCard.bytesTransferred)
                      + " / " + FormatUtils.formatBytes(taskCard.totalBytes)
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

        // 文件列表（展开时显示）
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: expanded ? Math.min(contentHeight, 150) : 0
            clip: true
            visible: expanded && isDirectory

            model: fileList

            delegate: Label {
                required property string modelData
                text: "  " + modelData
                font.pixelSize: 11
                color: "#666666"
                elide: Text.ElideRight
                width: ListView.view.width
            }
        }

        // 底部填充，确保按钮不被遮挡
        Item {
            Layout.fillHeight: true
        }
    }

    NumberAnimation {
        id: enterAnimation
        target: taskCard
        property: "opacity"
        from: 0
        to: 1
        duration: taskCard.kEnterDuration
        easing.type: Easing.OutCubic
    }

    Component.onCompleted: enterAnimation.restart()
}
