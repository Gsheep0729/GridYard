/**
 * @file    ConversationTimelineView.qml
 * @version 6.7.0
 * @date    2026-06-28
 * @author  GridYard Team
 * @brief   设备会话的统一消息时间线
 *
 * 将聊天消息和文件传输任务按时间混排展示，形成单一会话流。
 *
 * Change Log:
 * [v6.7.0] GY   2026-06-28
 * * 连续同方向聊天消息只在第一条显示头像
 * [v6.6.3] GY   2026-06-28
 * * 新增聊天消息与传输任务混排的统一会话流
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/FormatUtils.js" as FormatUtils
import "../utils/Style.js" as Style

Item {
    id: timelineView

    required property string deviceId
    property var expandedSessions: ({})  // 传输任务展开状态，由外层会话页持有
    property var timelineItems: []        // 聊天消息和传输任务合并后的展示列表
    property bool followLatest: true      // 用户在底部附近时自动跟随最新内容

    signal expansionRequested(string sessionId, bool expanded)

    // 设备切换时立即重建时间线，历史加载延迟到绑定传播完成后执行
    onDeviceIdChanged: {
        rebuildTimeline()
        Qt.callLater(loadInitialHistory)
    }

    // 组件树构建完成后触发一次历史首页加载
    Component.onCompleted: {
        rebuildTimeline()
        Qt.callLater(loadInitialHistory)
    }

    // 将 ISO 时间转为毫秒时间戳，解析失败时返回 0
    function timestampOf(value: string): real {
        const parsed = Date.parse(value)
        return isNaN(parsed) ? 0 : parsed
    }

    // 判断是否为当前设备的传输记录
    function isCurrentDeviceSession(session): bool {
        return session.deviceId === timelineView.deviceId
    }

    // 合并聊天消息和传输会话，按时间戳排序，仅影响展示顺序
    function rebuildTimeline(): void {
        const items = []
        // 从聊天控制器获取当前设备的消息列表
        const messages = AppController.chatController.messagesForDevice(deviceId)
        for (let i = 0; i < messages.length; i++) {
            const message = messages[i]
            items.push({
                kind: "message",
                stableIndex: i,
                sortTime: timestampOf(message.sentAt),
                messageId: message.messageId,
                senderName: message.senderName,
                content: message.content,
                sentAt: message.sentAt,
                isOutgoing: message.isOutgoing,
                status: message.status
            })
        }

        // 从传输控制器获取当前设备的传输会话，跳过其他设备的记录
        const sessions = AppController.transferController.sessions
        for (let j = 0; j < sessions.length; j++) {
            const session = sessions[j]
            if (!isCurrentDeviceSession(session)) {
                continue
            }

            items.push({
                kind: "transfer",
                stableIndex: messages.length + j,
                sortTime: timestampOf(session.createdAt),
                sessionId: session.sessionId,
                type: session.type,
                fileName: session.fileName,
                status: session.status,
                progress: session.progress,
                bytesTransferred: session.bytesTransferred,
                totalBytes: session.totalBytes,
                createdAt: session.createdAt,
                peerDeviceName: session.peerDeviceName,
                isDirectory: session.isDirectory,
                fileList: session.fileList,
                canDeleteLocalFile: session.canDeleteLocalFile
            })
        }

        // 按时间戳升序排列，相同时保持原模型顺序（stableIndex）
        items.sort(function(left, right) {
            if (left.sortTime === right.sortTime) {
                return left.stableIndex - right.stableIndex
            }
            return left.sortTime - right.sortTime
        })

        // 连续同方向消息只在首条显示头像，避免视觉重复
        for (let k = 0; k < items.length; k++) {
            if (items[k].kind !== "message") {
                continue
            }

            const previous = k > 0 ? items[k - 1] : null
            items[k].showAvatar = (previous === null
                    || previous.kind !== "message"
                    || previous.isOutgoing !== items[k].isOutgoing
                    || previous.senderName !== items[k].senderName)
        }

        timelineItems = items
        // 列表在底部附近时自动滚动到最新内容
        if (followLatest) {
            Qt.callLater(scrollToLatest)
        }
    }

    // 当前聊天消息为空时加载本地历史首页
    function loadInitialHistory(): void {
        if (deviceId.length === 0 || AppController.historyController.loading) {
            return
        }
        if (AppController.chatController.messagesForDevice(deviceId).length === 0) {
            AppController.historyController.loadMoreMessages(deviceId)
        }
    }

    function scrollToLatest(): void {
        if (timelineList.count > 0) {
            timelineList.positionViewAtEnd()
        }
    }

    ListView {
        id: timelineList

        anchors.fill: parent
        anchors.margins: Style.Space.lg
        clip: true
        spacing: Style.Space.md
        model: timelineView.timelineItems  // 统一时间线数据源，包含消息和传输两种 kind

        // 监听滚动位置：判断是否在底部附近，以及是否触发向上翻页
        onContentYChanged: {
            timelineView.followLatest = contentY + height >= contentHeight - Style.Space.lg
            // 滚动到顶部时加载更多聊天历史
            if (contentY <= 0 && count > 0 && !AppController.historyController.loading) {
                AppController.historyController.loadMoreMessages(timelineView.deviceId)
            }
        }

        // 内容高度变化时，若用户在底部则跟随滚动
        onContentHeightChanged: {
            if (timelineView.followLatest) {
                Qt.callLater(timelineView.scrollToLatest)
            }
        }

        delegate: Item {
            id: timelineDelegate

            required property var modelData
            readonly property bool isMessage: modelData.kind === "message"  // 区分消息和传输两种卡片
            readonly property bool isOutgoingMessage: isMessage && modelData.isOutgoing
            readonly property bool shouldShowAvatar: isMessage && modelData.showAvatar
            // 安全字段：防御 undefined/null 导致 QML 绑定崩溃
            readonly property string safeSenderName: isMessage ? String(modelData.senderName || "") : ""
            readonly property string safeContent: isMessage ? String(modelData.content || "") : ""
            readonly property string safeSentAt: isMessage ? String(modelData.sentAt || "") : ""
            readonly property int safeMessageStatus: isMessage ? Number(modelData.status || 0) : 0

            width: timelineList.width
            height: isMessage
                    ? messageColumn.implicitHeight
                    : transferWrapper.implicitHeight

            Column {
                id: messageColumn

                visible: timelineDelegate.isMessage
                width: parent.width
                spacing: Style.Space.xs

                RowLayout {
                    width: parent.width
                    spacing: Style.Space.sm

                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        Layout.alignment: Qt.AlignTop
                        radius: 16
                        color: Style.Color.primarySoft
                        opacity: !timelineDelegate.isOutgoingMessage
                                 && timelineDelegate.shouldShowAvatar ? 1 : 0

                        Label {
                            anchors.centerIn: parent
                            text: timelineDelegate.safeSenderName.length > 0
                                  ? timelineDelegate.safeSenderName.charAt(0).toUpperCase()
                                  : "?"
                            color: Style.Color.primary
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }

                    Column {
                        id: bubbleColumn

                        Layout.fillWidth: true
                        spacing: Style.Space.xs

                        Label {
                            width: parent.width
                            visible: !timelineDelegate.isOutgoingMessage
                                     && timelineDelegate.shouldShowAvatar
                            text: timelineDelegate.safeSenderName
                            color: Style.Color.textMuted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            width: Math.min(parent.width * 0.72,
                                            Math.max(96, messageText.implicitWidth + Style.Space.lg * 2))
                            height: messageText.implicitHeight + Style.Space.md * 2
                            anchors.right: timelineDelegate.isOutgoingMessage ? parent.right : undefined
                            radius: Style.Radius.sm
                            color: timelineDelegate.isOutgoingMessage ? Style.Color.primary : Style.Color.window
                            border.width: timelineDelegate.safeMessageStatus === 2 ? 1 : 0
                            border.color: Style.Color.error

                            Label {
                                id: messageText

                                anchors.fill: parent
                                anchors.margins: Style.Space.md
                                text: timelineDelegate.safeContent
                                color: timelineDelegate.isOutgoingMessage ? Style.Color.window : Style.Color.textMain
                                font.pixelSize: 14
                                wrapMode: Text.WrapAnywhere
                                textFormat: Text.PlainText
                            }
                        }

                        Row {
                            anchors.right: timelineDelegate.isOutgoingMessage ? parent.right : undefined
                            spacing: Style.Space.xs

                            Label {
                                text: FormatUtils.formatTime(timelineDelegate.safeSentAt)
                                color: Style.Color.textWeak
                                font.pixelSize: 11
                            }

                            Label {
                                visible: timelineDelegate.isOutgoingMessage
                                         && timelineDelegate.safeMessageStatus !== 1
                                text: timelineDelegate.safeMessageStatus === 2 ? qsTr("发送失败") : qsTr("发送中")
                                color: timelineDelegate.safeMessageStatus === 2 ? Style.Color.error : Style.Color.textWeak
                                font.pixelSize: 11
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        Layout.alignment: Qt.AlignTop
                        radius: 16
                        color: Style.Color.primary
                        opacity: timelineDelegate.isOutgoingMessage
                                 && timelineDelegate.shouldShowAvatar ? 1 : 0

                        Label {
                            anchors.centerIn: parent
                            text: qsTr("我")
                            color: Style.Color.window
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                }
            }

            Item {
                id: transferWrapper

                visible: !timelineDelegate.isMessage
                width: parent.width
                implicitHeight: visible ? transferCard.height : 0

                TransferTaskCard {
                    id: transferCard

                    width: Math.min(parent.width * 0.86, Math.max(280, parent.width * 0.76))
                    anchors.right: timelineDelegate.modelData.type === "send" ? parent.right : undefined
                    sessionId: String(timelineDelegate.modelData.sessionId || "")
                    taskType: String(timelineDelegate.modelData.type || "")
                    taskName: String(timelineDelegate.modelData.fileName || "")
                    status: String(timelineDelegate.modelData.status || "")
                    progress: Number(timelineDelegate.modelData.progress || 0)
                    bytesTransferred: timelineDelegate.modelData.bytesTransferred || 0
                    totalBytes: timelineDelegate.modelData.totalBytes || 0
                    createdAt: String(timelineDelegate.modelData.createdAt || "")
                    peerDeviceName: String(timelineDelegate.modelData.peerDeviceName || "")
                    isDirectory: Boolean(timelineDelegate.modelData.isDirectory)
                    fileList: timelineDelegate.modelData.fileList || []
                    canDeleteLocalFile: Boolean(timelineDelegate.modelData.canDeleteLocalFile)
                    expanded: timelineView.expandedSessions[timelineDelegate.modelData.sessionId] === true
                    onExpansionRequested: function(expanded) {
                        timelineView.expansionRequested(timelineDelegate.modelData.sessionId, expanded)
                    }
                }
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            visible: timelineList.count === 0
            spacing: Style.Space.sm

            Label {
                text: qsTr("还没有会话记录")
                color: Style.Color.textWeak
                font.pixelSize: 16
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: qsTr("发送消息或文件后将在这里显示")
                color: Style.Color.textMuted
                font.pixelSize: 13
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    Connections {
        target: AppController.chatController

        function onMessagesChanged(changedDeviceId: string): void {
            if (changedDeviceId === timelineView.deviceId) {
                timelineView.rebuildTimeline()
            }
        }
    }

    Connections {
        target: AppController.transferController

        function onSessionsChanged(): void {
            timelineView.rebuildTimeline()
        }
    }
}
