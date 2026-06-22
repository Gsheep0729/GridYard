/**
 * @file    ChatView.qml
 * @version 5.2.0
 * @date    2026-06-24
 * @author  GridYard Team
 * @brief   设备会话的在线聊天消息视图
 *
 * 仅绑定 ChatManager 提供的内存消息模型，负责空状态、气泡、时间和
 * 滚动行为。此组件不处理网络发送、协议解析或消息持久化。
 *
 * Change Log:
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

        property bool followLatest: true

        onContentYChanged: {
            followLatest = contentY + height >= contentHeight - Style.Space.lg
            if (contentY <= 0 && messageList.count > 0 && !AppController.history.loading) {
                AppController.history.loadMoreMessages(chatView.deviceId)
            }
        }

        onCountChanged: {
            if (followLatest) {
                Qt.callLater(chatView.scrollToLatest)
            }
        }

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
                    width: Math.min(parent.width * 0.72,
                                    Math.max(96, messageText.implicitWidth + Style.Space.lg * 2))
                    height: messageText.implicitHeight + Style.Space.md * 2
                    anchors.right: messageDelegate.isOutgoing ? parent.right : undefined
                    radius: Style.Radius.sm
                    color: messageDelegate.isOutgoing ? Style.Color.primary : Style.Color.window
                    border.width: messageDelegate.status === 2 ? 1 : 0
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
                        visible: messageDelegate.isOutgoing && messageDelegate.status !== 1
                        text: messageDelegate.status === 2 ? qsTr("发送失败") : qsTr("发送中")
                        color: messageDelegate.status === 2 ? Style.Color.error : Style.Color.textWeak
                        font.pixelSize: 11
                    }
                }
            }
        }

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
