/**
 * @file    DeviceCard.qml
 * @date    2026-06-02
 * @author  GY
 * @brief   在线设备列表项 delegate
 *
 * 全部 4 个 property 都声明为 required：当本组件作为 delegate
 * 使用时，QML 引擎会根据名字自动从 model 项里填值（model 项
 * 是 PeerInfo Q_GADGET，其 Q_PROPERTY 名字与本文件 required
 * property 名一一对应）。
 * 支持拖拽文件到卡片触发传输。
 *
 * Change Log:
 * [v0.1] GY   2026-06-02
 * * Stage 2：初始版本
 * [v0.2] GY   2026-06-03
 * * Stage 3.9：添加 DropArea 支持拖拽传输
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0

ItemDelegate {
    id: tw_deviceCard

    required property string deviceId
    required property string deviceName
    required property string ipAddress
    required property bool   isOnline

    readonly property int   kCardHeight: 72
    readonly property color kOnlineColor:  "#3DDC84"
    readonly property color kOfflineColor: "#999999"
    readonly property color kDropHighlight: "#E3F2FD"

    signal cardClicked(string deviceId)
    signal fileDropped(string deviceId, string filePath)

    height: kCardHeight

    // 卡片背景样式（拖拽高亮）
    background: Rectangle {
        color: tw_dropArea.containsDrag ? tw_deviceCard.kDropHighlight
             : tw_deviceCard.hovered   ? "#F2F2F2"
             :                           "#FFFFFF"
        border.color: tw_dropArea.containsDrag ? "#2196F3" : "#E0E0E0"
        border.width: tw_dropArea.containsDrag ? 2 : 1
        radius: 8
    }

    onClicked: tw_deviceCard.cardClicked(tw_deviceCard.deviceId)

    // 拖拽接收区域
    DropArea {
        id: tw_dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]

        onDropped: function(drop) {
            if (!tw_deviceCard.isOnline) return

            let urls = drop.urls
            for (let i = 0; i < urls.length; i++) {
                let path = urls[i]
                // 去掉 file:// 前缀
                if (path.startsWith("file://")) {
                    path = path.substring(7)
                }
                tw_deviceCard.fileDropped(tw_deviceCard.deviceId, path)
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // 在线状态指示灯
        Rectangle {
            id: statusDot
            Layout.alignment: Qt.AlignVCenter
            width: 12
            height: 12
            radius: 6
            color: tw_deviceCard.isOnline ? tw_deviceCard.kOnlineColor
                                          : tw_deviceCard.kOfflineColor
        }

        // 设备信息区域
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: tw_deviceCard.deviceName
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: tw_deviceCard.ipAddress
                color: "#666666"
                font.pixelSize: 12
            }
        }

        // 在线状态文字
        Label {
            text: tw_deviceCard.isOnline ? qsTr("在线") : qsTr("离线")
            color: tw_deviceCard.isOnline ? tw_deviceCard.kOnlineColor
                                          : tw_deviceCard.kOfflineColor
            font.pixelSize: 12
        }
    }
}
