/**
 * @file    TransferPanel.qml
 * @version 4.14.0
 * @date    2026-06-15
 * @author  GridYard Team
 * @brief   传输面板
 *
 * 显示所有进行中的传输任务，每个任务显示进度条、速度、取消按钮。
 * 绑定 TransferSessionManager.sessions。
 *
 * Change Log:
 * [v4.14.0] GY   2026-06-15
 * * 增加清空传输记录及删除已接收本地文件选项
 * [v4.13.3] GY   2026-06-15
 * * 按会话保存文件夹根目录预览的展开状态
 * [v4.13.1] DuRuoxian   2026-06-15
 * * 传递 isDirectory 和 fileList 属性到任务卡片
 * [v4.10.1] DuRuoxian   2026-06-13
 * * 修复会话字段绑定
 * [v0.2.0] DuRuoxian   2026-06-02
 * * Stage 3：初始版本
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0

Frame {
    id: transferPanel

    property var expandedSessions: ({})

    function isSessionExpanded(sessionId: string): bool {
        return expandedSessions[sessionId] === true
    }

    function setSessionExpanded(sessionId: string, expanded: bool): void {
        const next = Object.assign({}, expandedSessions)
        next[sessionId] = expanded
        expandedSessions = next
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 标题栏
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12

            Label {
                text: qsTr("传输任务")
                font.pixelSize: 16
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                text: qsTr("清空记录 ▼")
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

        // 任务列表
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8

            model: AppController.transfer.sessions

            delegate: Item {
                id: sessionDelegate

                required property string sessionId
                required property string type
                required property string filePath
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

                width: listView.width
                height: taskCard.height

                TransferTaskCard {
                    id: taskCard

                    width: sessionDelegate.width
                    sessionId: sessionDelegate.sessionId
                    taskType: sessionDelegate.type
                    taskName: sessionDelegate.fileName
                    isDirectory: sessionDelegate.isDirectory
                    fileList: sessionDelegate.fileList
                    canDeleteLocalFile: sessionDelegate.canDeleteLocalFile
                    status: sessionDelegate.status
                    progress: sessionDelegate.progress
                    bytesTransferred: sessionDelegate.bytesTransferred
                    totalBytes: sessionDelegate.totalBytes
                    createdAt: sessionDelegate.createdAt
                    peerDeviceName: sessionDelegate.peerDeviceName
                    expanded: transferPanel.isSessionExpanded(sessionDelegate.sessionId)
                    onExpansionRequested: function(expanded) {
                        transferPanel.setSessionExpanded(sessionDelegate.sessionId, expanded)
                    }
                }
            }

            // 空列表提示
            Label {
                anchors.centerIn: parent
                text: qsTr("暂无传输任务")
                color: "#999999"
                font.pixelSize: 14
                visible: listView.count === 0
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
