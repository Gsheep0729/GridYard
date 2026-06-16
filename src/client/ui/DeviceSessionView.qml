/**
 * @file    DeviceSessionView.qml
 * @version 4.15.2
 * @date    2026-06-17
 * @author  GridYard Team
 * @brief   当前设备的文件传输会话页
 *
 * 按设备筛选传输任务，并提供文件、文件夹和拖拽发送入口。
 *
 * Change Log:
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

    // 当前设备的会话数量（用于空列表判断）
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
    // 总清空操作面向全部传输记录，不受当前设备筛选影响
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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            color: "#FFFFFF"
            radius: 8
            border.color: "#E5E7EB"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    radius: 21
                    color: deviceSessionView.isOnline ? "#3B82F6" : "#9CA3AF"

                    Label {
                        anchors.centerIn: parent
                        text: deviceSessionView.deviceName.length > 0
                              ? deviceSessionView.deviceName.charAt(0).toUpperCase()
                              : "?"
                        color: "#FFFFFF"
                        font.pixelSize: 18
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: deviceSessionView.deviceName
                        font.pixelSize: 16
                        font.bold: true
                        color: "#111827"
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        text: deviceSessionView.ipAddress
                        color: "#6B7280"
                        font.pixelSize: 12
                    }
                }

                Label {
                    text: deviceSessionView.isOnline ? qsTr("在线") : qsTr("离线")
                    color: deviceSessionView.isOnline ? "#10B981" : "#9CA3AF"
                    font.pixelSize: 12
                    font.bold: true
                }

                ToolButton {
                    text: qsTr("清空记录 ▼")
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

        ListView {
            id: sessionList

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
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
                spacing: 12

                Label {
                    text: qsTr("暂无传输")
                    color: "#9CA3AF"
                    font.pixelSize: 32
                    Layout.alignment: Qt.AlignHCenter
                    opacity: 0.3
                }

                Label {
                    text: qsTr("还没有传输记录\n从下方选择文件，或直接拖放到这里")
                    color: "#6B7280"
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 1.4
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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            Layout.topMargin: 8
            color: "#FFFFFF"
            radius: 8
            border.color: "#E5E7EB"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Label {
                    Layout.fillWidth: true
                    text: deviceSessionView.isOnline
                          ? qsTr("选择文件或文件夹发送，也可以拖放到记录区")
                          : qsTr("设备当前离线，暂时无法发送")
                    color: "#6B7280"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }

                Button {
                    text: qsTr("发送文件夹")
                    enabled: deviceSessionView.isOnline
                    onClicked: deviceSessionView.sendFolderRequested()
                }

                Button {
                    text: qsTr("发送文件")
                    enabled: deviceSessionView.isOnline
                    highlighted: true
                    onClicked: deviceSessionView.sendFileRequested()
                }
            }
        }
    }

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
