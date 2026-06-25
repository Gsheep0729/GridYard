/**
 * @file    ChatView.qml
 * @version 6.6.2
 * @date    2026-06-24
 * @author  GridYard Team
 * @brief   设备会话的在线聊天消息视图
 *
 * 仅绑定 ChatManager 提供的内存消息模型，负责空状态、气泡、时间和
 * 滚动行为。此组件不处理网络发送、协议解析或消息持久化。
 *
 * Change Log:
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
    // 延迟获取消息模型：设备切换时自动重新绑定
    property var messageModel: deviceId.length > 0
                               ? AppController.chatController.messageModelForDevice(deviceId)
                               : null

    onDeviceIdChanged: Qt.callLater(chatView.loadInitialHistory)  // 延迟执行避免绑定风暴
    onMessageModelChanged: Qt.callLater(chatView.loadInitialHistory)
    Component.onCompleted: Qt.callLater(chatView.loadInitialHistory)

    // 设备切换或模型为空时加载首页本地历史
    function loadInitialHistory(): void {
        if (deviceId.length === 0 || messageList.count > 0 || AppController.historyController.loading) {
            return
        }
        AppController.historyController.loadMoreMessages(deviceId)
    }

    function scrollToLatest(): void {
        if (messageList.count > 0) {
            messageList.positionViewAtEnd()
        }
    }

    ListView {
        id: messageList
        anchors.fill: parent
        anchors.margins: Style.Space.lg
        clip: true
        spacing: Style.Space.md
        model: chatView.messageModel

        property bool followLatest: true  // 是否自动滚动到最新消息

        onContentYChanged: {
            // 判断用户是否仍在底部附近（16px 容差），决定是否自动跟随新消息
            followLatest = contentY + height >= contentHeight - Style.Space.lg
            // 滚动到顶部且有消息时触发向上翻页加载更早历史
            if (contentY <= 0 && messageList.count > 0 && !AppController.historyController.loading) {
                AppController.historyController.loadMoreMessages(chatView.deviceId)
            }
        }

        // 新消息追加后，如果处于跟随模式则自动滚动到底部
        onCountChanged: {
            if (followLatest) {
                Qt.callLater(chatView.scrollToLatest)
            }
        }

        // 历史消息 prepend 后内容高度变化，同样需要跟随滚动
        onContentHeightChanged: {
            if (followLatest) {
                Qt.callLater(chatView.scrollToLatest)
            }
        }

        delegate: Item {
            id: messageDelegate

            required property string messageId
            required property string deviceId
            required property string senderName
            required property string content
            required property string sentAt
            required property bool isOutgoing
            required property int status

            width: messageList.width
            height: bubbleColumn.implicitHeight

            Column {
                id: bubbleColumn
                width: parent.width
                spacing: Style.Space.xs

                Label {
                    width: parent.width
                    visible: !messageDelegate.isOutgoing
                    text: messageDelegate.senderName
                    color: Style.Color.textMuted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Rectangle {
                    // 气泡宽度自适应：最大 72% 容器宽度，最小 96px 保证短消息可读
                    width: Math.min(parent.width * 0.72,
                                    Math.max(96, messageText.implicitWidth + Style.Space.lg * 2))
                    height: messageText.implicitHeight + Style.Space.md * 2
                    anchors.right: messageDelegate.isOutgoing ? parent.right : undefined
                    radius: Style.Radius.sm
                    color: messageDelegate.isOutgoing ? Style.Color.primary : Style.Color.window
                    border.width: messageDelegate.status === 2 ? 1 : 0  // 发送失败时显示红色边框
                    border.color: Style.Color.error

                    Label {
                        id: messageText
                        anchors.fill: parent
                        anchors.margins: Style.Space.md
                        text: messageDelegate.content
                        color: messageDelegate.isOutgoing ? Style.Color.window : Style.Color.textMain
                        font.pixelSize: 14
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }
                }

                Row {
                    anchors.right: messageDelegate.isOutgoing ? parent.right : undefined
                    spacing: Style.Space.xs

                    Label {
                        text: FormatUtils.formatTime(messageDelegate.sentAt)
                        color: Style.Color.textWeak
                        font.pixelSize: 11
                    }

                    Label {
                        // 仅出站消息且状态非 Sent 时显示发送状态文字（Pending/Failed）
                        visible: messageDelegate.isOutgoing && messageDelegate.status !== 1
                        text: messageDelegate.status === 2 ? qsTr("发送失败") : qsTr("发送中")
                        color: messageDelegate.status === 2 ? Style.Color.error : Style.Color.textWeak
                        font.pixelSize: 11
                    }
                }
            }
        }

        // 空聊天状态占位，引导用户发送第一条消息
        ColumnLayout {
            anchors.centerIn: parent
            visible: messageList.count === 0
            spacing: Style.Space.sm

            Label {
                text: qsTr("还没有聊天记录")
                color: Style.Color.textWeak
                font.pixelSize: 16
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: qsTr("发送消息后将在本机历史中保留")
                color: Style.Color.textMuted
                font.pixelSize: 13
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }
}
