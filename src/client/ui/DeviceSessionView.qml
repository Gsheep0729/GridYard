/**
 * @file    DeviceSessionView.qml
 * @version 7.15.11
 * @date    2026-10-03
 * @author  GridYard Team
 * @brief   当前设备的统一会话页
 *
 * 按设备展示聊天消息和传输任务，并提供文本、文件和文件夹发送入口；
 * 设备离线时在页内给出明确状态提示。
 *
 * Change Log:
 * [v7.15.11] GY   2026-10-03
 * * 输入区重构：附件入口并入输入框左侧，发送改文字按钮，计数器移入框内右下角
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.11.0] GY   2026-10-02
 * * 增加离线横幅和历史记录入口，字符计数改为接近上限时出现，补齐禁用提示
 * [v6.7.0] GY   2026-06-28
 * * 同步统一会话页到 6.7.0 交付版本
 * [v6.6.3] GY   2026-06-28
 * * 去掉聊天和传输页签，改为统一会话时间线
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
   property string chatError: ""  // 聊天发送失败时的错误提示文字
   // 发送按钮可用条件：设备在线 + 输入非空 + 不超过协议层字符上限
   readonly property bool canSendChat: isOnline
                                      && messageInput.text.trim().length > 0
                                      && messageInput.text.length <= AppController.chatController.maxChatContentLength
   // 接近协议上限时才显示字符计数，避免常驻噪音
   readonly property bool showCharCounter: messageInput.text.length > 3600

   // 校验输入后发送文本消息并清空输入框
   function sendChatMessage(): void {
       if (!canSendChat) {
           return
       }
       AppController.chatController.sendText(deviceId, messageInput.text)
       messageInput.clear()
       chatError = ""
   }

   // 使用 Object.assign 浅拷贝再修改，触发 QML 属性绑定更新
   function setSessionExpanded(sessionId: string, expanded: bool): void {
       const next = Object.assign({}, expandedSessions)
       next[sessionId] = expanded
       expandedSessions = next
   }

   // 当前设备已结束的传输会话数量（用于清空按钮可用性判断）
   property int finishedCount: {
       let count = 0
       const sessions = AppController.transferController.sessions
       for (let i = 0; i < sessions.length; i++) {
           const status = sessions[i].status
           // 只统计当前设备的会话，跳过其他设备
           if (sessions[i].deviceId !== deviceSessionView.deviceId) {
               continue
           }
           // 四种结束态：完成、失败、拒绝、取消
           if (status === "completed" || status === "failed"
                   || status === "rejected" || status === "cancelled") {
               count++
           }
       }
       return count
   }

   signal sendFileRequested()
   signal sendFolderRequested()
   signal filesDropped(var urls)  // 拖拽内容原样冒泡，由 Main.qml 统一裁决

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
               anchors.leftMargin: Style.Space.lg
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

                   RowLayout {
                       spacing: Style.Space.xs

                       // 在线状态圆点：与会话列表中的表达保持一致
                       Rectangle {
                           Layout.preferredWidth: 7
                           Layout.preferredHeight: 7
                           radius: 3.5
                           color: deviceSessionView.isOnline
                                  ? Style.Color.success : Style.Color.textWeak
                       }

                       Label {
                           text: deviceSessionView.ipAddress
                           color: Style.Color.textMuted
                           font.pixelSize: 12
                       }
                   }
               }

               Label {
                   text: deviceSessionView.isOnline ? qsTr("在线") : qsTr("离线")
                   color: deviceSessionView.isOnline ? Style.Color.success : Style.Color.textWeak
                   font.pixelSize: 12
                   font.bold: true
               }

              // 历史记录入口：查看跨重启保留的聊天与传输历史
              ToolButton {
                  text: qsTr("历史")
                  Layout.preferredHeight: 32
                  ToolTip.text: qsTr("查看与该设备的历史记录")
                  ToolTip.delay: 500
                  ToolTip.visible: hovered
                  onClicked: deviceHistoryDialog.open()
              }

              ToolButton {
                  text: qsTr("清理")
                  enabled: deviceSessionView.finishedCount > 0
                  Layout.preferredHeight: 32
                  ToolTip.text: deviceSessionView.finishedCount > 0
                                ? qsTr("清理已结束的传输记录")
                                : qsTr("当前没有已结束的传输记录")
                  ToolTip.delay: 500
                  ToolTip.visible: hovered
                  onClicked: clearMenu.open()

                   Menu {
                       id: clearMenu

                       MenuItem {
                           text: qsTr("清空已结束传输记录")
                           onTriggered: AppController.transferController.clearFinishedSessions(
                                            false, deviceSessionView.deviceId)
                       }

                       MenuItem {
                           text: qsTr("清空传输记录并删除已接收文件")
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

       // 离线横幅：说明当前状态和恢复条件
       Rectangle {
           Layout.fillWidth: true
           Layout.preferredHeight: deviceSessionView.isOnline ? 0 : 30
           color: Style.Color.warningSoft
           clip: true

           Behavior on Layout.preferredHeight {
               NumberAnimation { duration: Style.Motion.base }
           }

           RowLayout {
               anchors.fill: parent
               anchors.leftMargin: Style.Space.lg
               anchors.rightMargin: Style.Space.lg
               spacing: Style.Space.xs
               visible: !deviceSessionView.isOnline

               Rectangle {
                   Layout.preferredWidth: 6
                   Layout.preferredHeight: 6
                   radius: 3
                   color: Style.Color.warning
               }

               Label {
                   Layout.fillWidth: true
                   text: qsTr("设备当前离线，消息和文件暂时无法发送；对方上线后会自动恢复")
                   font.pixelSize: 12
                   color: Style.Color.textSecondary
                   elide: Text.ElideRight
               }
           }
       }

       // ===== 统一会话时间线 =====
       Item {
           Layout.fillWidth: true
           Layout.fillHeight: true

           ConversationTimelineView {
               anchors.fill: parent
               deviceId: deviceSessionView.deviceId
               expandedSessions: deviceSessionView.expandedSessions
               onExpansionRequested: function(sessionId, expanded) {
                   deviceSessionView.setSessionExpanded(sessionId, expanded)
               }
           }

           DropArea {
               anchors.fill: parent
               keys: ["text/uri-list"]  // 接受文件管理器拖入的 URI 列表

               onDropped: function(drop) {
                   deviceSessionView.filesDropped(drop.urls)
               }
           }
       }

       // 分隔线
       Rectangle {
           Layout.fillWidth: true
           Layout.preferredHeight: 1
           color: Style.Color.border
       }

       // ===== 文本输入区 =====
       Rectangle {
           Layout.fillWidth: true
           Layout.preferredHeight: 104
           color: Style.Color.surface

           RowLayout {
               anchors.fill: parent
               anchors.margins: Style.Space.sm
               spacing: Style.Space.sm

               // 输入框容器：附件入口并入框内左侧（"+"），与主流 IM 布局一致
               Rectangle {
                   id: inputBox
                   Layout.fillWidth: true
                   Layout.fillHeight: true
                   radius: Style.Radius.sm
                   color: deviceSessionView.isOnline ? Style.Color.window : Style.Color.surfaceSoft
                   border.color: Style.Color.border
                   border.width: 1

                   // 附件入口：点击弹出文件/文件夹选择菜单
                   ToolButton {
                       id: attachButton
                       anchors.left: parent.left
                       anchors.leftMargin: Style.Space.xs
                       anchors.verticalCenter: parent.verticalCenter
                       enabled: deviceSessionView.isOnline
                       text: "+"
                       font.pixelSize: 20
                       font.bold: true
                       ToolTip.text: deviceSessionView.isOnline
                                     ? qsTr("发送文件或文件夹")
                                     : qsTr("设备离线，无法发送文件")
                       ToolTip.delay: 500
                       ToolTip.visible: attachButton.hovered
                       onClicked: sendMenu.open()

                       Menu {
                           id: sendMenu
                           // 输入区贴着窗口底部，菜单向上弹出
                           y: -sendMenu.height

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

                   TextArea {
                       id: messageInput
                       anchors {
                           top: parent.top
                           left: attachButton.right
                           right: parent.right
                           bottom: parent.bottom
                           topMargin: Style.Space.sm
                           leftMargin: Style.Space.sm
                           rightMargin: Style.Space.sm
                           // 计数器出现时抬高文本，避免与右下角计数重叠
                           bottomMargin: deviceSessionView.showCharCounter ? 18 : Style.Space.sm
                       }
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

                       // 超过协议层上限时截断，与 kMaxChatContentChars 保持一致
                       onTextChanged: {
                           const maxChars = AppController.chatController.maxChatContentLength
                           if (text.length > maxChars) {
                               text = text.slice(0, maxChars)
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
                             ? qsTr("输入消息，回车发送") : qsTr("设备离线，无法发送")
                       color: deviceSessionView.isOnline ? Style.Color.textWeak : Style.Color.error
                       font.pixelSize: 14
                       elide: Text.ElideRight
                   }

                   // 字符计数器：剩余不足 400 字时显示在右下角内侧
                   Label {
                       anchors.right: parent.right
                       anchors.rightMargin: Style.Space.sm
                       anchors.bottom: parent.bottom
                       anchors.bottomMargin: 2
                       text: "%1/%2".arg(messageInput.text.length)
                              .arg(AppController.chatController.maxChatContentLength)
                       color: messageInput.text.length >= AppController.chatController.maxChatContentLength
                              ? Style.Color.error : Style.Color.textWeak
                       font.pixelSize: 10
                       visible: deviceSessionView.showCharCounter
                   }
               }

               // 发送按钮：文字按钮，主色样式，宽度自适应
               Button {
                   id: chatSendButton
                   text: qsTr("发送")
                   highlighted: true
                   enabled: deviceSessionView.canSendChat
                   Layout.alignment: Qt.AlignVCenter
                   onClicked: deviceSessionView.sendChatMessage()
                   ToolTip.text: qsTr("设备离线，无法发送")
                   ToolTip.delay: 500
                   ToolTip.visible: chatSendButton.hovered && !deviceSessionView.isOnline
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

   DeviceHistoryDialog {
       id: deviceHistoryDialog
       anchors.centerIn: Overlay.overlay
       deviceId: deviceSessionView.deviceId
       deviceName: deviceSessionView.deviceName
   }

   // ===== 清空确认弹窗（Frame 级别）=====
   Dialog {
       id: clearDeleteConfirmDialog
       title: qsTr("清空传输记录并删除本地文件")
       modal: true
       anchors.centerIn: Overlay.overlay
       standardButtons: Dialog.Yes | Dialog.No

       Label {
           text: qsTr("确定清空当前设备的已结束传输记录，并删除其中已接收成功的本地文件和文件夹吗？发送源文件不会被删除。")
           wrapMode: Text.WordWrap
       }

       onAccepted: AppController.transferController.clearFinishedSessions(true, deviceSessionView.deviceId)
   }
}
