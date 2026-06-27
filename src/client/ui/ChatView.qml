/**
 * @file    ChatView.qml
 * @version 6.7.0
 * @date    2026-06-27
 * @author  FCL
 * @brief   统一对话历史时间线
 *
 * 将聊天消息和传输历史合并为按时间排序的统一时间线数组，
 * 使用 Flickable + Repeater 渲染，单个 delegate 内按 itemType
 * 条件显示聊天气泡或传输卡片，支持上划加载更早历史。
 *
 * Change Log:
 * [v6.7.0] FCL   2026-06-27
 * * 重写为统一时间线，传输历史与聊天消息按时间自然混排
 * * 新增活跃传输进度区，显示传输进度条和取消操作
 * * 文件夹传输支持展开预览内容列表
 * * 传输完成后自动刷新历史记录
 * [v6.6.2] GY   2026-06-25
 * * 选中设备且消息模型为空时主动加载本地历史第一页
 * [v5.2.0] GY   2026-06-24
 * * 新增 Stage 5 聊天消息气泡视图
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/FormatUtils.js" as FormatUtils
import "../utils/Style.js" as Style

Item {
    id: chatView

    required property string deviceId

    property var messageModel: deviceId.length > 0
                               ? AppController.chat.messageModelForDevice(deviceId)
                               : null

    property var timelineItems: []

    property var expandedSessions: ({})

    readonly property var activeSessions: {
        let result = []
        const sessions = AppController.transfer.sessions || []
        for (let i = 0; i < sessions.length; i++) {
            let s = sessions[i]
            if (s.deviceId === chatView.deviceId
                    && s.status !== "completed" && s.status !== "failed"
                    && s.status !== "cancelled" && s.status !== "rejected") {
                result.push(s)
            }
        }
        return result
    }

    readonly property bool isEmpty: timelineItems.length === 0
                                    && activeSessions.length === 0

    // 重建统一时间线，合并聊天消息和传输历史并按时间排序
    function rebuildTimeline(): void {
        let items = []

        // 聊天消息
        const msgs = AppController.chat.messagesForDevice(chatView.deviceId)
        for (let i = 0; i < msgs.length; i++) {
            let m = Object.assign({}, msgs[i])
            m.itemType = "chat"
            items.push(m)
        }

        // 传输历史
        const transfers = AppController.history.transfers || []
        for (let i = 0; i < transfers.length; i++) {
            let t = transfers[i]
            if (t.peerDeviceId === chatView.deviceId) {
                let copy = Object.assign({}, t)
                copy.itemType = "transfer"
                items.push(copy)
            }
        }

        // 按时间排序
        items.sort(function(a, b) {
            let ta = a.itemType === "chat" ? (a.sentAt || "") : (a.startedAt || "")
            let tb = b.itemType === "chat" ? (b.sentAt || "") : (b.startedAt || "")
            return ta.localeCompare(tb)
        })

        chatView.timelineItems = items
    }

    onDeviceIdChanged: {
        Qt.callLater(function() {
            chatView.loadInitialHistory()
        })
    }
    onMessageModelChanged: Qt.callLater(chatView.loadInitialHistory)
    Component.onCompleted: Qt.callLater(chatView.loadInitialHistory)

    function loadInitialHistory(): void {
        if (deviceId.length === 0) {
            return
        }
        AppController.history.queryTransfers({ peerDeviceId: deviceId })
        if (!AppController.history.loading) {
            const hasMsgs = messageModel && messageModel.count > 0
            if (!hasMsgs) {
                AppController.history.loadMoreMessages(deviceId)
            }
        }
        Qt.callLater(chatView.rebuildTimeline)
        Qt.callLater(chatView.scrollToLatest)
    }

    function scrollToLatest(): void {
        const maxY = timelineFlickable.contentHeight - timelineFlickable.height
        if (maxY > 0) {
            timelineFlickable.contentY = maxY
        }
    }

    Flickable {
        id: timelineFlickable
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, timelineColumn.height + Style.Space.md * 2)

        property bool _followLatest: true

        onContentYChanged: {
            _followLatest = contentY + height >= contentHeight - Style.Space.lg * 2
            if (contentY <= 5 && !AppController.history.loading
                    && chatView.timelineItems.length > 0) {
                AppController.history.loadMoreMessages(chatView.deviceId)
            }
        }

        onContentHeightChanged: {
            if (_followLatest) {
                Qt.callLater(chatView.scrollToLatest)
            }
        }

        Column {
            id: timelineColumn
            width: parent.width - Style.Space.lg * 2
            x: Style.Space.lg
            y: Style.Space.md
            spacing: Style.Space.md

            Repeater {
                id: activeRepeater
                model: chatView.activeSessions

                delegate: Rectangle {
                    required property var modelData

                    width: timelineColumn.width
                    height: activeCardHeight
                    radius: Style.Radius.sm
                    color: Style.Color.window
                    border.color: modelData.status === "waiting_confirm"
                                 ? Style.Color.warning : Style.Color.primary

                    readonly property real activeCardHeight:
                        activeRow.implicitHeight + progressRow.implicitHeight
                        + (isExpanded && modelData.isDirectory ? folderList.height : 0)
                        + Style.Space.sm * 3
                    readonly property bool isExpanded:
                        chatView.expandedSessions[modelData.sessionId] === true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Style.Space.sm
                        spacing: Style.Space.xs

                        RowLayout {
                            id: activeRow
                            Layout.fillWidth: true
                            spacing: Style.Space.sm

                            FileTypeIcon {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                fileName: modelData.fileName || ""
                                isDirectory: modelData.isDirectory || false
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                RowLayout {
                                    spacing: Style.Space.xs

                                    Label {
                                        text: modelData.type === "send"
                                              ? qsTr("发送") : qsTr("接收")
                                        font.pixelSize: 10
                                        font.bold: true
                                        color: modelData.type === "send"
                                               ? Style.Color.primary : "#8B5CF6"
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.fileName || ""
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: Style.Color.textMain
                                        elide: Text.ElideMiddle
                                    }
                                }

                                Label {
                                    visible: modelData.status === "waiting_confirm"
                                    text: qsTr("等待确认...")
                                    color: Style.Color.warning
                                    font.pixelSize: 11
                                }
                            }

                            ToolButton {
                                visible: modelData.isDirectory
                                text: activeCard.isExpanded ? "⌃" : "⌄"
                                ToolTip.text: activeCard.isExpanded
                                              ? qsTr("收起文件夹内容")
                                              : qsTr("展开文件夹内容")
                                onClicked: {
                                    let next = Object.assign({}, chatView.expandedSessions)
                                    next[modelData.sessionId] = !activeCard.isExpanded
                                    chatView.expandedSessions = next
                                }
                            }

                            ToolButton {
                                text: qsTr("取消")
                                enabled: modelData.status !== "waiting_confirm"
                                onClicked: AppController.transfer.cancelSession(
                                               modelData.sessionId)
                            }
                        }

                        RowLayout {
                            id: progressRow
                            Layout.fillWidth: true
                            visible: modelData.status === "transferring"
                            spacing: Style.Space.xs

                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: modelData.progress || 0

                                Behavior on value {
                                    SmoothedAnimation {
                                        duration: Style.Motion.slow
                                        velocity: 200
                                    }
                                }
                            }

                            Label {
                                text: (modelData.progress || 0) + "%"
                                font.pixelSize: 11
                                color: Style.Color.textSecondary
                            }
                        }

                        RowLayout {
                            visible: modelData.status === "transferring"
                                     && (modelData.totalBytes || 0) > 0
                            Layout.fillWidth: true
                            spacing: Style.Space.sm

                            Label {
                                text: FormatUtils.formatBytes(modelData.bytesTransferred || 0)
                                      + " / " + FormatUtils.formatBytes(modelData.totalBytes || 0)
                                font.pixelSize: 11
                                color: Style.Color.textWeak
                            }

                            Item { Layout.fillWidth: true }
                        }

                        ListView {
                            id: folderList
                            Layout.fillWidth: true
                            visible: activeCard.isExpanded && modelData.isDirectory
                            height: Math.min(contentHeight, 150)
                            clip: true
                            model: modelData.fileList || []
                            spacing: 1

                            delegate: RowLayout {
                                width: folderList.width
                                spacing: Style.Space.xs

                                FileTypeIcon {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    fileName: modelData || ""
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData || ""
                                    font.pixelSize: 11
                                    color: Style.Color.textMuted
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Style.Color.border
                visible: activeRepeater.count > 0
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: AppController.history.loading
                visible: running
            }

            Repeater {
                id: timelineRepeater
                model: chatView.timelineItems

                delegate: Item {
                    id: timelineDelegate

                    required property var modelData

                    width: timelineColumn.width
                    height: modelData.itemType === "chat"
                            ? chatBubble.implicitHeight
                            : transferCard.implicitHeight

                    // 聊天气泡
                    Column {
                        id: chatBubble
                        width: parent.width
                        visible: modelData.itemType === "chat"
                        spacing: Style.Space.xs

                        Label {
                            width: parent.width
                            visible: !modelData.isOutgoing
                            text: modelData.senderName || ""
                            color: Style.Color.textMuted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            width: Math.min(parent.width * 0.72,
                                    Math.max(96, msgText.implicitWidth + Style.Space.lg * 2))
                            height: msgText.implicitHeight + Style.Space.md * 2
                            anchors.right: modelData.isOutgoing ? parent.right : undefined
                            radius: Style.Radius.sm
                            color: modelData.isOutgoing ? Style.Color.primary : Style.Color.window
                            border.width: (modelData.status || 0) === 2 ? 1 : 0
                            border.color: Style.Color.error

                            Label {
                                id: msgText
                                anchors.fill: parent
                                anchors.margins: Style.Space.md
                                text: modelData.content || ""
                                color: modelData.isOutgoing ? Style.Color.window : Style.Color.textMain
                                font.pixelSize: 14
                                wrapMode: Text.WrapAnywhere
                                textFormat: Text.PlainText
                            }
                        }

                        Row {
                            anchors.right: modelData.isOutgoing ? parent.right : undefined
                            spacing: Style.Space.xs

                            Label {
                                text: FormatUtils.formatTime(modelData.sentAt)
                                color: Style.Color.textWeak
                                font.pixelSize: 11
                            }

                            Label {
                                visible: modelData.isOutgoing && (modelData.status || 0) !== 1
                                text: (modelData.status || 0) === 2 ? qsTr("发送失败") : qsTr("发送中")
                                color: (modelData.status || 0) === 2 ? Style.Color.error : Style.Color.textWeak
                                font.pixelSize: 11
                            }
                        }
                    }

                    // 传输历史卡片
                    Rectangle {
                        id: transferCard
                        width: parent.width
                        visible: modelData.itemType === "transfer"
                        implicitHeight: transferInner.implicitHeight + Style.Space.sm * 2
                        radius: Style.Radius.sm
                        color: Style.Color.window
                        border.color: Style.Color.border

                        ColumnLayout {
                            id: transferInner
                            anchors.fill: parent
                            anchors.margins: Style.Space.sm
                            spacing: Style.Space.xs

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Style.Space.sm

                                FileTypeIcon {
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    fileName: modelData.displayName || ""
                                    isDirectory: modelData.isDirectory || false
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.displayName || ""
                                        font.pixelSize: 14
                                        font.bold: true
                                        color: Style.Color.textMain
                                        elide: Text.ElideMiddle
                                    }

                                    Label {
                                        text: "%1 · %2 · %3 · %4"
                                            .arg((modelData.direction || 0) === 1 ? qsTr("发送") : qsTr("接收"))
                                            .arg(FormatUtils.formatBytes(modelData.totalBytes || 0))
                                            .arg(modelData.status || "")
                                            .arg(FormatUtils.formatTime(modelData.startedAt))
                                        color: Style.Color.textMuted
                                        font.pixelSize: 12
                                    }

                                    Label {
                                        visible: (modelData.fileCount || 0) > 1
                                        text: qsTr("%1 个文件").arg(modelData.fileCount || 0)
                                        color: Style.Color.textMuted
                                        font.pixelSize: 12
                                    }

                                    Label {
                                        visible: (modelData.errorMessage || "").length > 0
                                        Layout.fillWidth: true
                                        text: modelData.errorMessage || ""
                                        color: Style.Color.error
                                        font.pixelSize: 12
                                        wrapMode: Text.WrapAnywhere
                                    }
                                }

                                ToolButton {
                                    text: qsTr("删除")
                                    onClicked: AppController.history.deleteTransfer(
                                                   modelData.recordId)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                width: parent.width
                height: emptyCol.implicitHeight + Style.Space.xl * 2
                visible: chatView.isEmpty && !AppController.history.loading

                Column {
                    id: emptyCol
                    anchors.centerIn: parent
                    width: parent.width - Style.Space.lg * 2
                    spacing: Style.Space.sm

                    Label {
                        width: parent.width
                        text: qsTr("还没有会话内容")
                        color: Style.Color.textWeak
                        font.pixelSize: 16
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Label {
                        width: parent.width
                        text: qsTr("发送消息或文件，记录会保留在本地历史中")
                        color: Style.Color.textMuted
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    // 消息模型变化时重建时间线
    Connections {
        target: chatView.messageModel

        function onCountChanged(): void { chatView.rebuildTimeline() }
        function onModelReset(): void  { chatView.rebuildTimeline() }
    }

    // 传输历史查询完成时重建时间线
    Connections {
        target: AppController.history

        function onTransfersChanged(): void { chatView.rebuildTimeline() }
    }
}
