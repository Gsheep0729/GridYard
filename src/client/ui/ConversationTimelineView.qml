/**
 * @file    ConversationTimelineView.qml
 * @version 7.17.3
 * @date 2026-10-04
 * @author  GridYard Team
 * @brief   设备会话的统一消息时间线
 *
 * 将聊天消息和文件传输任务按时间混排展示，形成单一会话流。
 *
 * Change Log:
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
 * [v7.15.19] GY   2026-10-04
 * * 版本头对齐到 v7.15.19
 * [v7.15.18] GY   2026-10-04
 * * 版本头对齐到 v7.15.18
 * [v7.15.17] GY   2026-10-04
 * * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
 * [v7.15.13] GY   2026-10-04
 * * 版本头对齐到 v7.15.13
 * [v7.15.12] GY   2026-10-03
 * 版本头对齐到 v7.15.12
 * [v7.15.11] GY   2026-10-03
 * * 版本头对齐到 v7.15.11
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.13.0] GY   2026-10-02
 * * 时间线改绑 C++ 合并模型，进度刷新只更新对应行，不再全量重建
 * * 气泡与头像的反白文字统一使用 textOnAccent 语义色
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
    property bool followLatest: true      // 用户在底部附近时自动跟随最新内容

    signal expansionRequested(string sessionId, bool expanded)

    // 聊天消息与传输任务按时间合并，行级增量通知由 C++ 模型负责
    ConversationTimelineModel {
        id: timelineModel

        deviceId: timelineView.deviceId
        chatController: AppController.chatController
        transferSessions: AppController.transferController.sessionModel
    }

    // 设备切换后历史加载延迟到绑定传播完成后执行
    onDeviceIdChanged: {
        Qt.callLater(loadInitialHistory)
    }

    // 组件树构建完成后触发一次历史首页加载
    Component.onCompleted: {
        Qt.callLater(loadInitialHistory)
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
        model: timelineModel  // 统一时间线数据源，包含消息和传输两种 kind

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

        // 顶部加载行：滚动加载更早的历史消息时给出反馈。
        // 用 Loader 承载并直接绑加载状态：header Item 上绑高度会被视图
        // 布局回写卷入绑定环（height 与 implicitHeight 实测都会触发）
        header: Loader {
            width: timelineList.width
            active: AppController.historyController.loading
            sourceComponent: Row {
                spacing: Style.Space.sm
                leftPadding: 8
                topPadding: 6
                bottomPadding: 6

                BusyIndicator {
                    implicitWidth: 16
                    implicitHeight: 16
                    running: true
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("正在加载更早的消息...")
                    font.pixelSize: 12
                    color: Style.Color.textWeak
                }
            }
        }

        delegate: Item {
            id: timelineDelegate

            required property int index
            required property var kind
            required property var showAvatar
            required property var senderName
            required property var content
            required property var sentAt
            required property var isOutgoing
            required property var status
            required property var sessionId
            required property var type
            required property var fileName
            required property var progress
            required property var bytesTransferred
            required property var totalBytes
            required property var createdAt
            required property var peerDeviceName
            required property var isDirectory
            required property var fileList
            required property var canDeleteLocalFile
            required property var errorMsg

            readonly property bool isMessage: kind === "message"  // 区分消息和传输两种卡片
            readonly property bool isOutgoingMessage: isMessage && isOutgoing
            readonly property bool shouldShowAvatar: isMessage && showAvatar
            // 安全字段：防御 undefined/null 导致 QML 绑定崩溃
            readonly property string safeSenderName: isMessage ? String(senderName || "") : ""
            readonly property string safeContent: isMessage ? String(content || "") : ""
            readonly property string safeSentAt: isMessage ? String(sentAt || "") : ""
            readonly property int safeMessageStatus: isMessage ? Number(status || 0) : 0

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
                                color: timelineDelegate.isOutgoingMessage ? Style.Color.textOnAccent : Style.Color.textMain
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
                            color: Style.Color.textOnAccent
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
                    anchors.right: timelineDelegate.type === "send" ? parent.right : undefined
                    sessionId: String(timelineDelegate.sessionId || "")
                    taskType: String(timelineDelegate.type || "")
                    taskName: String(timelineDelegate.fileName || "")
                    status: String(timelineDelegate.status || "")
                    progress: Number(timelineDelegate.progress || 0)
                    bytesTransferred: timelineDelegate.bytesTransferred || 0
                    totalBytes: timelineDelegate.totalBytes || 0
                    createdAt: String(timelineDelegate.createdAt || "")
                    peerDeviceName: String(timelineDelegate.peerDeviceName || "")
                    isDirectory: Boolean(timelineDelegate.isDirectory)
                    fileList: timelineDelegate.fileList || []
                    canDeleteLocalFile: Boolean(timelineDelegate.canDeleteLocalFile)
                    errorMsg: String(timelineDelegate.errorMsg || "")
                    expanded: timelineView.expandedSessions[timelineDelegate.sessionId] === true
                    onExpansionRequested: function(expanded) {
                        timelineView.expansionRequested(timelineDelegate.sessionId, expanded)
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
}
