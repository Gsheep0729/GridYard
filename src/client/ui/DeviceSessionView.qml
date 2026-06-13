/**
 * @file    DeviceSessionView.qml
 * @date    2026-06-13
 * @author  GY
 * 当前设备的文件传输会话页
 *
 * 按设备筛选传输任务，并提供文件、文件夹和拖拽发送入口。
 *
 * Change Log:
 * [v1.0] GY   2026-06-13
 * * 初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0

Frame {
    id: tw_deviceSessionView

    required property string deviceId
    required property string deviceName
    required property string ipAddress
    required property bool isOnline

    // 当前设备的会话数量（用于空列表判断）
    property int filteredCount: {
        let count = 0
        const sessions = AppController.transfer.sessions
        for (let i = 0; i < sessions.length; i++) {
            if (sessions[i].deviceId === tw_deviceSessionView.deviceId) {
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
            color: "#F8F9FA"
            radius: 8

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width: 42
                    height: 42
                    radius: 21
                    color: tw_deviceSessionView.isOnline ? "#4A90D9" : "#9E9E9E"

                    Label {
                        anchors.centerIn: parent
                        text: tw_deviceSessionView.deviceName.length > 0
                              ? tw_deviceSessionView.deviceName.charAt(0)
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
                        text: tw_deviceSessionView.deviceName
                        font.pixelSize: 17
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        text: tw_deviceSessionView.ipAddress
                        color: "#666666"
                        font.pixelSize: 12
                    }
                }

                Label {
                    text: tw_deviceSessionView.isOnline ? qsTr("在线") : qsTr("离线")
                    color: tw_deviceSessionView.isOnline ? "#3AAF72" : "#888888"
                    font.pixelSize: 12
                    font.bold: true
                }
            }
        }

        ListView {
            id: tw_sessionList

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true
            spacing: 8
            model: AppController.transfer.sessions

            delegate: TransferTaskCard {
                width: tw_sessionList.width
                visible: modelData.deviceId === tw_deviceSessionView.deviceId
                height: visible ? implicitHeight : 0
                sessionId: modelData.sessionId || ""
                taskType: modelData.type || ""
                taskName: modelData.fileName || ""
                status: modelData.status || ""
                progress: modelData.progress || 0
                bytesTransferred: modelData.bytesTransferred || 0
                totalBytes: modelData.totalBytes || 0
                createdAt: modelData.createdAt || ""
                peerDeviceName: modelData.peerDeviceName || ""
            }

            Label {
                anchors.centerIn: parent
                visible: tw_deviceSessionView.filteredCount === 0
                text: qsTr("还没有传输记录\n从下方选择文件，或直接拖放到这里")
                color: "#888888"
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 1.4
            }

            DropArea {
                anchors.fill: parent
                keys: ["text/uri-list"]

                onDropped: function(drop) {
                    if (!tw_deviceSessionView.isOnline) {
                        return
                    }
                    const urls = drop.urls
                    for (let i = 0; i < urls.length; i++) {
                        let path = urls[i].toString()
                        if (path.startsWith("file://")) {
                            path = path.substring(7)
                        }
                        tw_deviceSessionView.fileDropped(path)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            Layout.topMargin: 8
            color: "#F8F9FA"
            radius: 8

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Label {
                    Layout.fillWidth: true
                    text: tw_deviceSessionView.isOnline
                          ? qsTr("选择文件或文件夹发送，也可以拖放到记录区")
                          : qsTr("设备当前离线，暂时无法发送")
                    color: "#666666"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }

                Button {
                    text: qsTr("发送文件夹")
                    enabled: tw_deviceSessionView.isOnline
                    onClicked: tw_deviceSessionView.sendFolderRequested()
                }

                Button {
                    text: qsTr("发送文件")
                    enabled: tw_deviceSessionView.isOnline
                    highlighted: true
                    onClicked: tw_deviceSessionView.sendFileRequested()
                }
            }
        }
    }
}
