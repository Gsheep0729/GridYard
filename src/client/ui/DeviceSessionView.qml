/**
 * @file    DeviceSessionView.qml
 * @version 6.6.2
 * @date    2026-06-24
 * @author  GridYard Team
 * @brief   当前设备的文件传输会话页
 *
 * 按设备筛选传输任务，并提供文件、文件夹和拖拽发送入口。
 *
 * Change Log:
 * [v6.6.2] GY   2026-06-25
 * * 整理会话页标题栏、页签、内容区和底部输入区尺寸，避免控件遮挡
 * * 清空记录限定当前设备，发送入口改为文件夹图标下拉菜单
 * * 对齐聊天输入框占位提示和真实输入光标
 * [v6.6.1] GY   2026-06-25
 * * 修复聊天输入框占位提示不隐藏并遮挡输入内容
 * [v5.3.0] GY   2026-06-24
 * * 接入聊天视图和在线文本发送入口
 * [v4.16.3] FengChunlin   2026-06-24
 * * 调整设备会话页头部、传输列表和底部发送区布局
 * [v4.16.0] DuRuoxian   2026-06-18
 * * 使用 Style.js 统一样式常量，调整底部发送栏为 Stage 5 预留文本输入区域
 * [v4.15.2] DuRuoxian   2026-06-17
 * * 优化会话页头部、传输空状态和底部发送栏视觉层级
 * [v4.14.0] GY   2026-06-15
 * * 增加清空传输记录及删除已接收本地文件选项
 * [v4.13.3] GY   2026-06-15
 * * 按会话保存文件夹根目录预览的展开状态
 * [v4.13.1] DuRuoxian   2026-06-15
 * * 传递文件夹和相对路径列表到任务卡片
 * [v4.10.1] DuRuoxian   2026-06-13
 * * 使用显式模型角色修复传输记录字段为空
 * [v4.10.0] DuRuoxian   2026-06-13
 * * 修复 delegate 绑定问题，添加 filteredCount 属性
 * [v4.9.0] DuRuoxian   2026-06-13
 * * 初始版本
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Frame {
   id: deviceSessionView

   required property string deviceId
   required property string deviceName
   required property string ipAddress
   required property bool isOnline

   property var expandedSessions: ({})  // 记录每个传输任务文件夹展开状态的字典
   property int timelineMode: 0  // 0=聊天页签, 1=传输页签
   property string chatError: ""  // 聊天发送失败时的错误提示文字
   // 发送按钮可用条件：设备在线 + 输入非空 + 不超过 4000 字符限制
   readonly property bool canSendChat: isOnline
                                      && messageInput.text.trim().length > 0
                                      && messageInput.text.length <= 4000

   // 校验输入后发送文本消息并清空输入框
   function sendChatMessage(): void {
       if (!canSendChat) {
           return
       }
       AppController.chatController.sendText(deviceId, messageInput.text)
       messageInput.clear()
       chatError = ""
   }

   function isSessionExpanded(sessionId: string): bool {
       return expandedSessions[sessionId] === true
   }

   // 使用 Object.assign 浅拷贝再修改，触发 QML 属性绑定更新
   function setSessionExpanded(sessionId: string, expanded: bool): void {
       const next = Object.assign({}, expandedSessions)
       next[sessionId] = expanded
       expandedSessions = next
   }

   // 当前设备对应的传输会话数量（用于空状态判断）
   property int filteredCount: {
       let count = 0
       const sessions = AppController.transferController.sessions
       for (let i = 0; i < sessions.length; i++) {
           if (sessions[i].deviceId === deviceSessionView.deviceId) {
               count++
           }
       }
       return count
   }

   // 当前设备已结束的传输会话数量（用于清空按钮可用性判断）
   property int finishedCount: {
       let count = 0
       const sessions = AppController.transferController.sessions
       for (let i = 0; i < sessions.length; i++) {
           const status = sessions[i].status
           if (sessions[i].deviceId !== deviceSessionView.deviceId) {
               continue
           }
           if (status === "completed" || status === "failed"
                   || status === "rejected" || status === "cancelled") {
               count++
           }
       }
       return count
   }

   signal sendFileRequested()
   signal sendFolderRequested()
   signal fileDropped(string filePath)

   ColumnLayout {
       anchors.fill: parent
       spacing: 0

       // ===== ① 标题栏 =====
       Rectangle {
           Layout.fillWidth: true
           Layout.preferredHeight: 58
           color: Style.Color.surface

           RowLayout {
               anchors.fill: parent
               anchors.leftMargin: Style.Space.md
               anchors.rightMargin: Style.Space.md
               anchors.topMargin: Style.Space.sm
               anchors.bottomMargin: Style.Space.sm
               spacing: Style.Space.md

               ColumnLayout {
                   Layout.fillWidth: true
                   spacing: 2

                   Label {
                       text: deviceSessionView.deviceName
                       font.pixelSize: 16
                       font.bold: true
                       color: Style.Color.textMain
                       Layout.fillWidth: true
                       elide: Text.ElideRight
                   }

                   Label {
                       text: deviceSessionView.ipAddress
                       color: Style.Color.textMuted
                       font.pixelSize: 12
                   }
               }

               Label {
                   text: deviceSessionView.isOnline ? qsTr("在线") : qsTr("离线")
                   color: deviceSessionView.isOnline ? Style.Color.success : Style.Color.textWeak
                   font.pixelSize: 12
                   font.bold: true
               }

               ToolButton {
                   text: qsTr("清空记录")
                   enabled: deviceSessionView.finishedCount > 0
                   Layout.preferredHeight: 32
                   onClicked: clearMenu.open()

                   Menu {
                       id: clearMenu

                       MenuItem {
                           text: qsTr("清空已结束记录")
                           onTriggered: AppController.transferController.clearFinishedSessions(
                                            false, deviceSessionView.deviceId)
                       }

                       MenuItem {
                           text: qsTr("清空记录并删除已接收文件")
                           onTriggered: clearDeleteConfirmDialog.open()
                       }
                   }
               }
           }
       }

       // 分隔线
       Rectangle {
           Layout.fillWidth: true
           Layout.preferredHeight: 1
           color: Style.Color.border
       }

       TabBar {
           id: timelineTabs
           Layout.fillWidth: true
           Layout.preferredHeight: 44
           currentIndex: deviceSessionView.timelineMode
           background: Rectangle {
               color: Style.Color.surface
           }

           onCurrentIndexChanged: deviceSessionView.timelineMode = currentIndex

           TabButton {
               text: qsTr("聊天")
               height: timelineTabs.height
               font.pixelSize: 14
           }

           TabButton {
               text: qsTr("传输")
               height: timelineTabs.height
               font.pixelSize: 14
           }
       }

       // ===== 记录浏览 =====
       StackLayout {
           Layout.fillWidth: true
           Layout.fillHeight: true
           currentIndex: deviceSessionView.timelineMode

           ChatView {
               deviceId: deviceSessionView.deviceId
           }

           Item {
               ListView {
                   id: sessionList
                   anchors.fill: parent
                   anchors.margins: Style.Space.md
                   clip: true
                   spacing: Style.Space.sm

                   model: AppController.transferController.sessions

                   delegate: Item {
                       id: sessionDelegate

                       required property string sessionId
                       required property string type
                       required property string deviceId
                       required property string fileName
                       required property string status
                       required property int progress
                       required property real bytesTransferred
                       required property real totalBytes
                       required property string createdAt
                       required property string peerDeviceName
                       required property bool isDirectory
                       required property list<string> fileList
                       required property bool canDeleteLocalFile

                       width: sessionList.width
                       visible: deviceId === deviceSessionView.deviceId
                       height: visible ? taskCard.height : 0

                       TransferTaskCard {
                           id: taskCard

                           width: sessionDelegate.width
                           sessionId: sessionDelegate.sessionId
                           taskType: sessionDelegate.type
                           taskName: sessionDelegate.fileName
                           status: sessionDelegate.status
                           progress: sessionDelegate.progress
                           bytesTransferred: sessionDelegate.bytesTransferred
                           totalBytes: sessionDelegate.totalBytes
                           createdAt: sessionDelegate.createdAt
                           peerDeviceName: sessionDelegate.peerDeviceName
                           isDirectory: sessionDelegate.isDirectory
                           fileList: sessionDelegate.fileList
                           canDeleteLocalFile: sessionDelegate.canDeleteLocalFile
                           expanded: deviceSessionView.isSessionExpanded(sessionDelegate.sessionId)
                           onExpansionRequested: function(expanded) {
                               deviceSessionView.setSessionExpanded(sessionDelegate.sessionId, expanded)
                           }
                       }
                   }
               }

               ColumnLayout {
                   anchors.centerIn: parent
                   visible: deviceSessionView.filteredCount === 0
                   spacing: Style.Space.md

                   Label {
                       text: qsTr("还没有传输记录")
                       color: Style.Color.textWeak
                       font.pixelSize: 20
                       font.bold: true
                       Layout.alignment: Qt.AlignHCenter
                   }

                   Label {
                       text: qsTr("从下方菜单发送文件或文件夹")
                       color: Style.Color.textMuted
                       horizontalAlignment: Text.AlignHCenter
                       font.pixelSize: 13
                   }
               }

               DropArea {
                   anchors.fill: parent
                   keys: ["text/uri-list"]

                   onDropped: function(drop) {
                       if (!deviceSessionView.isOnline) {
                           return
                       }
                       const urls = drop.urls
                       for (let i = 0; i < urls.length; i++) {
                           let path = urls[i].toString()
                           if (path.startsWith("file://")) {
                               path = path.substring(7)
                           }
                           deviceSessionView.fileDropped(path)
                       }
                   }
               }
           }
       }

       // 分隔线
       Rectangle {
           Layout.fillWidth: true
           Layout.preferredHeight: 1
           color: Style.Color.border
       }

       // ===== 文件操作工具栏 =====
       Rectangle {
           Layout.fillWidth: true
           Layout.preferredHeight: 40
           color: Style.Color.surface

           RowLayout {
               anchors.fill: parent
               anchors.leftMargin: Style.Space.sm
               anchors.rightMargin: Style.Space.sm

               // 发送入口按钮：点击弹出文件/文件夹选择下拉菜单
               ToolButton {
                   id: sendButton
                   icon.name: "folder-open"
                   ToolTip.text: qsTr("发送文件或文件夹")
                   ToolTip.visible: hovered
                   onClicked: sendMenu.open()

                   Menu {
                       id: sendMenu
                       y: sendButton.height

                       MenuItem {
                           text: qsTr("发送文件")
                           onTriggered: deviceSessionView.sendFileRequested()
                       }
                       MenuItem {
                           text: qsTr("发送文件夹")
                           onTriggered: deviceSessionView.sendFolderRequested()
                       }
                   }
               }

               Item { Layout.fillWidth: true }
           }
       }

       // ===== 文本输入区：仅聊天页签显示，高度为 0 时完全隐藏 =====
       Rectangle {
           Layout.fillWidth: true
           Layout.preferredHeight: deviceSessionView.timelineMode === 0 ? 104 : 0
           visible: deviceSessionView.timelineMode === 0
           color: Style.Color.surface

           RowLayout {
               anchors.fill: parent
               anchors.margins: Style.Space.sm
               spacing: Style.Space.sm

               Rectangle {
                   Layout.fillWidth: true
                   Layout.fillHeight: true
                   radius: Style.Radius.sm
                   color: deviceSessionView.isOnline ? Style.Color.window : Style.Color.surfaceSoft
                   border.color: Style.Color.border
                   border.width: 1

                   TextArea {
                       id: messageInput
                       anchors.fill: parent
                       anchors.margins: Style.Space.sm
                       enabled: deviceSessionView.isOnline
                       placeholderText: ""  // 占位提示由下方 Label 实现，避免 TextArea 默认样式冲突
                       font.pixelSize: 14
                       color: Style.Color.textMain
                       wrapMode: TextEdit.Wrap
                       selectByMouse: true
                       leftPadding: 0
                       rightPadding: 0
                       topPadding: 0
                       bottomPadding: 0

                       background: Item {}

                       // 超过 4000 字符时截断，与协议层 kMaxChatContentLength 保持一致
                       onTextChanged: {
                           if (text.length > 4000) {
                               text = text.slice(0, 4000)
                           }
                           deviceSessionView.chatError = ""
                       }

                       // 回车发送（Shift+回车换行），与桌面 IM 习惯一致
                       Keys.onReturnPressed: function(event) {
                           if ((event.modifiers & Qt.ShiftModifier) === 0) {
                               event.accepted = true
                               deviceSessionView.sendChatMessage()
                           }
                       }
                   }

                   // 输入框占位提示：输入为空时显示，有内容时隐藏
                   Label {
                       anchors.left: messageInput.left
                       anchors.right: messageInput.right
                       anchors.top: messageInput.top
                       visible: messageInput.text.length === 0
                       text: deviceSessionView.isOnline
                             ? qsTr("输入消息") : qsTr("设备离线，无法发送")
                       color: deviceSessionView.isOnline ? Style.Color.textWeak : Style.Color.error
                       font.pixelSize: 14
                       elide: Text.ElideRight
                   }
               }

               ColumnLayout {
                   Layout.fillHeight: true
                   Layout.preferredWidth: 44
                   spacing: Style.Space.xs

                   // 发送按钮：输入校验通过时可用
                   ToolButton {
                       icon.name: "mail-send"
                       enabled: deviceSessionView.canSendChat
                       ToolTip.text: qsTr("发送消息")
                       ToolTip.visible: hovered
                       onClicked: deviceSessionView.sendChatMessage()
                       Layout.alignment: Qt.AlignHCenter
                   }

                   // 字符计数器：接近上限时提醒用户
                   Label {
                       text: "%1/4000".arg(messageInput.text.length)
                       color: Style.Color.textWeak
                       font.pixelSize: 10
                       Layout.alignment: Qt.AlignHCenter
                   }
               }
           }

           // 聊天发送错误提示：仅当前设备的发送失败才显示
           Label {
               anchors.left: parent.left
               anchors.leftMargin: Style.Space.md
               anchors.bottom: parent.bottom
               anchors.bottomMargin: 2
               text: deviceSessionView.chatError
               color: Style.Color.error
               font.pixelSize: 11
               visible: text.length > 0
           }
       }
   }

   // 监听聊天发送失败信号，仅处理当前设备的错误
   Connections {
       target: AppController.chatController

       function onSendFailed(targetDeviceId: string, error: int, errorMessage: string): void {
           if (targetDeviceId === deviceSessionView.deviceId) {
               deviceSessionView.chatError = errorMessage  // 在输入框下方显示错误提示
           }
       }
   }

   // ===== 清空确认弹窗（Frame 级别）=====
   Dialog {
       id: clearDeleteConfirmDialog
       title: qsTr("清空记录并删除本地文件")
       modal: true
       anchors.centerIn: Overlay.overlay
       standardButtons: Dialog.Yes | Dialog.No

       Label {
           text: qsTr("确定清空所有已结束记录，并删除其中已接收成功的本地文件和文件夹吗？发送源文件不会被删除。")
           wrapMode: Text.WordWrap
       }

       onAccepted: AppController.transferController.clearFinishedSessions(true, deviceSessionView.deviceId)
   }
}
