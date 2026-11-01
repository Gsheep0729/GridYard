/**
 * @file    TransferTaskCard.qml
 * @version 8.2.1
 * @date 2026-11-01
 * @author  GridYard Team
 * @brief   传输任务卡片
 *
 * 显示单个传输任务的进度、状态、取消按钮。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/FormatUtils.js" as FormatUtils
import "../utils/Style.js" as Style

Frame {
    id: taskCard

    required property string sessionId
    required property string taskType      // "send" 或 "receive"
    required property string taskName      // 显示的文件/文件夹名
    required property string status        // connecting/waiting_confirm/awaiting_relay/transferring/completed/failed/rejected/cancelled
    required property int    progress      // 0-100 百分比
    required property var    bytesTransferred
    required property var    totalBytes
    required property bool   isDirectory
    required property var    fileList       // 文件夹场景下的根目录预览列表
    required property bool   canDeleteLocalFile  // 仅接收成功时为 true
    property string createdAt: ""
    property string peerDeviceName: ""
    property string errorMsg: ""  // 失败/取消时的具体原因，由会话数据透传
    // 目录体量统计在途（后台统计，O-12）：卡片先以"统计中…"出现，字节数后补；
    // 普通文件与历史恢复行无此标志，默认 false 不影响既有展示
    property bool statsPending: false

    property bool expanded: false  // 文件夹内容是否展开
    signal expansionRequested(bool expanded)
    // 重新唤起接收确认弹窗：确认弹窗被直接关闭后请求从任务卡找回入口
    signal confirmRequested(string sessionId)

    // 状态颜色映射：不同阶段使用不同底色引导用户注意力
    readonly property color kRunningColor: Style.Color.primary
    readonly property color kSuccessColor: Style.Color.success
    readonly property color kFailedColor:  Style.Color.error
    readonly property color kWaitingColor: Style.Color.warning
    readonly property color kFileBgColor:   Style.Color.window        // 普通文件卡片底色：比时间线底亮一档
    readonly property color kFolderBgColor: Style.Color.surfaceSoft   // 文件夹卡片底色，略有区分
    readonly property color kFailedBgColor: Style.Color.errorSoft     // 失败/取消/拒绝卡片底色
    readonly property int kColorDuration: Style.Motion.base           // 状态颜色过渡动画时长
    readonly property int kProgressDuration: Style.Motion.slow        // 进度条平滑动画时长
    readonly property bool canShowFolderPreview: isDirectory && fileList.length > 0
    readonly property bool isFinished: status === "completed" || status === "failed"
                                       || status === "rejected" || status === "cancelled"

    implicitHeight: contentColumn.implicitHeight + Style.Space.md * 2
    height: implicitHeight

    // 状态文案委托给 FormatUtils 唯一词表，与历史页保持一致
    function statusText(): string {
        return FormatUtils.transferStatusText(status)
    }

    function statusColor(): color {
        switch (status) {
        case "connecting":
        case "waiting_confirm":
        case "awaiting_relay":  return kWaitingColor
        case "transferring":    return kRunningColor
        case "completed":       return kSuccessColor
        case "failed":
        case "rejected":
        case "cancelled":       return kFailedColor
        default:                return "#9CA3AF"
        }
    }

    // 卡片背景色：失败/取消/拒绝用警告色底色，文件夹用浅灰底色区分
    function backgroundColor(): color {
        if (status === "failed" || status === "rejected" || status === "cancelled") {
            return kFailedBgColor
        }
        return isDirectory ? kFolderBgColor : kFileBgColor
    }

    // 卡片描边：传输中随状态色引导注意力；已结束的普通文件卡改用更浅的细边收口白底
    function borderColor(): color {
        if (!isFinished) {
            return statusColor()
        }
        if (!isDirectory && status === "completed") {
            return Style.Color.borderSoft
        }
        return Style.Color.border
    }

    // 方向标识颜色：失败族随状态转红，其余保持发送蓝/接收紫
    function directionColor(): color {
        if (status === "failed" || status === "rejected" || status === "cancelled") {
            return kFailedColor
        }
        return taskType === "send" ? Style.Color.primary : Style.Color.receiveAccent
    }

    // 状态徽标底色：浅底配深字，避免白字压实色低于 AA 对比度
    function statusBg(): color {
        switch (status) {
        case "completed":       return Style.Color.statusOkBg
        case "failed":
        case "rejected":
        case "cancelled":       return Style.Color.statusErrBg
        case "transferring":    return Style.Color.primarySoft
        default:                return Style.Color.statusWarnBg
        }
    }

    // 状态徽标字色：与底色成对，实测对比度 4.84 / 5.21 / 5.91 / 6.16
    function statusFg(): color {
        switch (status) {
        case "completed":       return Style.Color.statusOkFg
        case "failed":
        case "rejected":
        case "cancelled":       return Style.Color.statusErrFg
        case "transferring":    return Style.Color.primaryStrong
        default:                return Style.Color.statusWarnFg
        }
    }

    background: Rectangle {
        radius: Style.Radius.sm
        color: taskCard.backgroundColor()
        border.color: taskCard.borderColor()
        border.width: 1

        // 状态切换时边框颜色带过渡动画，避免视觉跳变
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
        anchors.margins: Style.Space.md
        spacing: Style.Space.xs

        // 第一行：方向标识 + 文件名 + 状态
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.Space.sm

            // 方向标识：发送用上箭头，接收用下箭头，颜色区分方向；失败族随状态转红
            Label {
                text: taskCard.taskType === "send" ? "↑" : "↓"
                color: taskCard.directionColor()
                font.pixelSize: 16
                font.bold: true
                Layout.alignment: Qt.AlignVCenter
            }

            // 文件类型图标：根据扩展名自动选择
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
                color: Style.Color.textMain
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            // 展开/收起按钮：仅传输中的文件夹任务显示，已完成的文件夹用底部行展开
            // 箭头用文字符号，与底部折叠行保持一致，不依赖系统图标主题
            Button {
                flat: true
                visible: taskCard.canShowFolderPreview && !taskCard.isFinished
                onClicked: taskCard.expansionRequested(!taskCard.expanded)
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                padding: 0
                text: taskCard.expanded ? "⌄" : "›"
                font.pixelSize: 15
                font.bold: true
            }

            // 状态标签：浅底深字，颜色随状态变化
            Rectangle {
                objectName: "statusBadge"
                Layout.preferredWidth: statusLabel.implicitWidth + 12
                Layout.preferredHeight: statusLabel.implicitHeight + 4
                radius: Style.Radius.xs
                color: taskCard.statusBg()

                // 读屏语义：自绘徽标以 Indicator 角色播报，名字随状态词表更新；
                // 内部同文文本置 ignored，读屏只播徽标名一次
                Accessible.role: Accessible.Indicator
                Accessible.name: qsTr("状态：") + taskCard.statusText()

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
                    color: taskCard.statusFg()
                    font.bold: true
                    Accessible.ignored: true
                }
            }
        }

        // 第二行：时间 + 文件大小
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.Space.md

            Label {
                text: taskCard.taskType === "send"
                      ? qsTr("发送给 %1").arg(taskCard.peerDeviceName || qsTr("未知设备"))
                      : qsTr("来自 %1").arg(taskCard.peerDeviceName || qsTr("未知设备"))
                font.pixelSize: 12
                color: Style.Color.textMuted
                elide: Text.ElideRight
            }

            Item { Layout.fillWidth: true }

            Label {
                text: FormatUtils.formatTime(taskCard.createdAt)
                font.pixelSize: 12
                color: Style.Color.textWeak
                visible: taskCard.createdAt.length > 0
            }

            Label {
                text: FormatUtils.formatBytes(taskCard.totalBytes)
                font.pixelSize: 12
                color: Style.Color.textMuted
                // 统计在途期间体量还是占位值：只在统计回填（或 worker 进度先行
                // 报出真实总量）后显示，避免"0 B"闪现
                visible: taskCard.totalBytes > 0 && !taskCard.statsPending
            }

            Label {
                text: qsTr("统计中…")
                font.pixelSize: 12
                color: Style.Color.textMuted
                // 后台统计完成前体量未知：占位提示代替字节数，卡片本身即时出现
                visible: taskCard.statsPending && taskCard.totalBytes <= 0
            }
        }

        // 第三行：进度条（仅在传输中或完成时显示，完成时固定 100%）
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
                     || taskCard.status === "awaiting_relay"

            Label {
                text: taskCard.status === "awaiting_relay"
                      ? qsTr("直连失败，可选择经服务器中继")
                      : FormatUtils.formatBytes(taskCard.bytesTransferred)
                        + " / " + FormatUtils.formatBytes(taskCard.totalBytes)
                font.pixelSize: 12
                color: Style.Color.textSecondary
                visible: taskCard.status === "transferring" || taskCard.status === "awaiting_relay"
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

            // 确认入口：等待确认的接收卡可重新唤起确认弹窗（弹窗被 Esc 等
            // 直接关闭后请求不消失，任务卡是找回它的入口）
            Button {
                text: qsTr("去确认")
                flat: true
                visible: taskCard.status === "waiting_confirm" && taskCard.taskType === "receive"
                onClicked: taskCard.confirmRequested(taskCard.sessionId)
            }

            // 取消按钮：传输中、等待确认或等待中继决策时可用
            Button {
                text: qsTr("取消")
                flat: true
                visible: taskCard.status === "transferring" || taskCard.status === "waiting_confirm"
                         || taskCard.status === "awaiting_relay"
                onClicked: AppController.transferController.cancelSession(taskCard.sessionId)
            }
        }

        // 失败/取消/拒绝的原因行：让用户知道为什么没有成功
        RowLayout {
            Layout.fillWidth: true
            visible: taskCard.isFinished && taskCard.errorMsg.length > 0
            spacing: Style.Space.xs

            Label {
                text: "!"
                font.pixelSize: 11
                font.bold: true
                color: Style.Color.error
                Layout.preferredWidth: 14
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                Layout.fillWidth: true
                text: taskCard.errorMsg
                color: Style.Color.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WrapAnywhere
            }
        }

        // 已结束状态的操作按钮：移除记录和更多选项（删除本地文件）
        RowLayout {
            Layout.fillWidth: true
            visible: taskCard.isFinished

            Item { Layout.fillWidth: true }

            RowLayout {
                spacing: 4

                Button {
                    text: qsTr("移除记录")
                    flat: true
                    onClicked: AppController.transferController.removeSession(taskCard.sessionId)
                }

                ToolButton {
                    text: "⋯"
                    font.pixelSize: 16
                    font.bold: true
                    padding: 4
                    ToolTip.delay: 500
                    ToolTip.visible: hovered
                    ToolTip.text: enabled ? qsTr("更多移除选项") : qsTr("发送记录或未完成接收记录不能删除本地文件")
                    onClicked: removeMenu.open()

                    Menu {
                        id: removeMenu
                        // 向上展开：传输卡常聚在时间线底部，向下弹会被窗口下缘裁切
                        // （F14 第 28 章 f-3、7-V V-09）；设备卡菜单维持 Qt popup 自带翻转不动
                        y: -implicitHeight
                        implicitWidth: 176

                        MenuItem {
                            id: deleteLocalFileMenuItem
                            text: qsTr("删除本地文件")
                            enabled: taskCard.canDeleteLocalFile
                            onTriggered: deleteConfirmDialog.open()

                            contentItem: Label {
                                text: deleteLocalFileMenuItem.text
                                font.pixelSize: 12
                                color: deleteLocalFileMenuItem.enabled ? Style.Color.textMain : Style.Color.textWeak
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }

        // 文件夹内容折叠行：已完成的文件夹任务显示，点击展开/收起
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 30
            visible: taskCard.canShowFolderPreview && taskCard.isFinished
            color: taskCard.expanded ? Style.Color.surface : "transparent"
            radius: Style.Radius.sm
            border.color: taskCard.expanded ? Style.Color.border : "#00000000"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Style.Space.sm
                anchors.rightMargin: Style.Space.sm
                spacing: Style.Space.xs

                // 展开/收起指示箭头
                Label {
                    text: taskCard.expanded ? "⌄" : "›"
                    font.pixelSize: 14
                    color: Style.Color.textSecondary
                    Layout.preferredWidth: 12
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    text: qsTr("文件夹内容")
                    font.pixelSize: 12
                    font.bold: true
                    color: Style.Color.textSecondary
                }

                // 条目数量提示
                Label {
                    text: qsTr("%1 项").arg(taskCard.fileList.length)
                    font.pixelSize: 11
                    color: Style.Color.textMuted
                }

                Item { Layout.fillWidth: true }
            }

            TapHandler {
                onTapped: taskCard.expansionRequested(!taskCard.expanded)
            }
        }

        // 文件列表（展开时显示）：限制最大高度避免卡片过长
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
                    color: Style.Color.textMuted
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }
    }

    // 删除本地文件确认弹窗：警告用户操作不可撤销
    Dialog {
        id: deleteConfirmDialog
        title: qsTr("删除确认")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(360, parent.width - 24)
        padding: 12

        background: Rectangle {
            color: Style.Color.surface
            radius: Style.Radius.sm
            border.color: Style.Color.border
            border.width: 1
        }

        header: Label {
            text: deleteConfirmDialog.title
            font.pixelSize: 14
            font.bold: true
            color: Style.Color.textMain
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
                color: Style.Color.textSecondary
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: fileInfoRow.implicitHeight + 16
                color: Style.Color.surfaceSoft
                radius: Style.Radius.sm
                border.color: Style.Color.borderSoft

                RowLayout {
                    id: fileInfoRow
                    anchors.fill: parent
                    anchors.margins: Style.Space.md
                    spacing: Style.Space.sm

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
                        color: Style.Color.textMain
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.Space.sm

                Label {
                    text: "!"
                    font.pixelSize: 11
                    font.bold: true
                    color: Style.Color.error
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Layout.preferredWidth: 16
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("此操作将永久删除本地文件，无法撤销。")
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: Style.Color.error
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

            DangerButton {
                id: confirmDeleteButton
                implicitWidth: 76
                implicitHeight: 28
                text: qsTr("确认删除")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }

        onAccepted: AppController.transferController.removeSessionAndDeleteFile(taskCard.sessionId)
    }

}
