/**
 * @file    TransferPanel.qml
 * @version 4.10.0
 * @date    2026-06-13
 * @author  GY
 * @brief   传输面板
 *
 * 显示所有进行中的传输任务，每个任务显示进度条、速度、取消按钮。
 * 绑定 TransferSessionManager.sessions。
 *
 * Change Log:
 * [v0.2.0] GY   2026-06-02
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

            delegate: TransferTaskCard {
                width: listView.width
                sessionId: model.sessionId || ""
                taskType: model.type || ""
                taskName: model.type === "send"
                    ? (model.filePath || "").split("/").pop()
                    : (model.fileName || "")
                status: model.status || ""
                progress: model.progress || 0
                bytesTransferred: model.bytesTransferred || 0
                totalBytes: model.totalBytes || 0
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
