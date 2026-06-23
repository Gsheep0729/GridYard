/**
 * @file    DeviceSessionView.qml
 * @version 4.16.3
 * @date    2026-06-24
 * @author  GridYard Team
 * @brief   当前设备的文件传输会话页
 *
 * 按设备筛选传输任务，并提供文件、文件夹和拖拽发送入口。
 *
 * Change Log:
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

   property var expandedSessions: ({})

   function isSessionExpanded(sessionId: string): bool {
       return expandedSessions[sessionId] === true
   }

   function setSessionExpanded(sessionId: string, expanded: bool): void {
       const next = Object.assign({}, expandedSessions)
       next[sessionId] = expanded
       expandedSessions = next
   }

   property int filteredCount: {
       let count = 0
       const sessions = AppController.transfer.sessions
       for (let i = 0; i < sessions.length; i++) {
           if (sessions[i].deviceId === deviceSessionView.deviceId) {
               count++
           }
       }
       return count
   }

   property int finishedCount: {
       let count = 0
       const sessions = AppController.transfer.sessions
       for (let i = 0; i < sessions.length; i++) {
           const status = sessions[i].status
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
           Layout.preferredHeight: 48
           color: Style.Color.surface

           RowLayout {
               anchors.fill: parent
               anchors.margins: 12
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
                   onClicked: clearMenu.open()

                   Menu {
                       id: clearMenu

                       MenuItem {
                           text: qsTr("清空已结束记录")
                           onTriggered: AppController.transfer.clearFinishedSessions(false)
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

       // ===== 记录浏览 =====
       ListView {
           id: sessionList
           Layout.fillWidth: true
           Layout.preferredHeight:500
           clip: true
           spacing: 8

           model: AppController.transfer.sessions

           delegate: Item {
               id: sessionDelegate

               required property string sessionId
               required property string type
               required property string deviceId
               required property string fileName
               required property string status
               required property int progress
               required property var bytesTransferred
               required property var totalBytes
               required property string createdAt
               required property string peerDeviceName
               required property bool isDirectory
               required property var fileList
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

           ColumnLayout {
               anchors.centerIn: parent
               visible: deviceSessionView.filteredCount === 0
               spacing: Style.Space.md

               Label {
                   text: qsTr("还没有会话内容")
                   color: Style.Color.textWeak
                   font.pixelSize: 20
                   font.bold: true
                   Layout.alignment: Qt.AlignHCenter
               }

               Label {
                   text: qsTr("发送文件，或稍后在这里查看聊天消息")
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

       // 分隔线
       Rectangle {
           Layout.fillWidth: true
           Layout.preferredHeight: 1
           color: Style.Color.border
       }

       // ===== ③ 操作工具栏 =====
       Rectangle {
           Layout.fillWidth: true
           Layout.preferredHeight: 25
           color: Style.Color.surface
           RowLayout {
               anchors.fill: parent
               Rectangle {
                   Layout.preferredWidth: 15
                   Layout.preferredHeight: 15
                   Layout.leftMargin: 10
                   color: _plusHovered ? "#E0E0E0" : "#F0F0F0"
                   property bool _plusHovered: false

                   Label {
                       anchors.centerIn: parent
                       text: "+"
                       font.pixelSize: 22
                       color: "#666666"
                   }
                   MouseArea {
                       anchors.fill: parent
                       hoverEnabled: true
                       onEntered: parent._plusHovered = true
                       onExited:  parent._plusHovered = false
                       onClicked: sendMenu.open()
                   }
               }

               Item { Layout.fillWidth: true }
           }
       }
       // ===== ④ 文本输入 =====
       Rectangle {
           Layout.fillWidth: true
           Layout.fillHeight: true
           color: Style.Color.surface
           RowLayout {
               anchors.fill: parent
               Rectangle {
                   Layout.fillWidth: true
                   Layout.fillHeight: true
                   radius: 4
                   color: Style.Color.surfaceLeft
                   border.color: Style.Color.border
                   border.width: 1

                   Label {
                       anchors.fill: parent
                       Layout.topMargin: 15
                       text: deviceSessionView.isOnline
                             ? qsTr("输入消息...")
                             : qsTr("设备离线，无法发送")
                       font.pixelSize: 14
                       color: deviceSessionView.isOnline
                             ? Style.Color.textWeak
                             : Style.Color.textMuted
                   }
               }
           }
       }
   }

   // ===== 文件发送菜单（Frame 级别）=====
   Menu {
       id: sendMenu
       y: -height - 4

       MenuItem {
           text: qsTr("发送文件")
           onTriggered: deviceSessionView.sendFileRequested()
       }
       MenuItem {
           text: qsTr("发送文件夹")
           onTriggered: deviceSessionView.sendFolderRequested()
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

       onAccepted: AppController.transfer.clearFinishedSessions(true)
   }
}
