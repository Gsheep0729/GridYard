/**
 * @file    TransferPanel.qml
 * @version 4.10.1
 * @date    2026-06-13
 * @author  GridYard Team
 * @brief   传输面板
 *
 * 显示所有进行中的传输任务，每个任务显示进度条、速度、取消按钮。
 * 绑定 TransferSessionManager.sessions。
 *
 * Change Log:
 * [v4.10.1] DuRuoxian   2026-06-13
 * * 修复会话字段绑定
 * [v0.2.0] DuRuoxian   2026-06-02
 * * Stage 3：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0

Frame {
    id: transferPanel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 标题栏
        Label {
            text: qsTr("传输任务")
            font.pixelSize: 16
            font.bold: true
            Layout.fillWidth: true
            Layout.margins: 12
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
                property bool isDirectory: false
                property var fileList: []

                width: listView.width
                height: taskCard.height

                TransferTaskCard {
                    id: taskCard

                    width: sessionDelegate.width
                    sessionId: sessionDelegate.sessionId
                    taskType: sessionDelegate.type
                    taskName: sessionDelegate.type === "send"
                        ? sessionDelegate.filePath.split("/").pop()
                        : sessionDelegate.fileName
                    isDirectory: sessionDelegate.isDirectory
                    fileList: sessionDelegate.fileList
                    status: sessionDelegate.status
                    progress: sessionDelegate.progress
                    bytesTransferred: sessionDelegate.bytesTransferred
                    totalBytes: sessionDelegate.totalBytes
                    createdAt: sessionDelegate.createdAt
                    peerDeviceName: sessionDelegate.peerDeviceName
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
}
