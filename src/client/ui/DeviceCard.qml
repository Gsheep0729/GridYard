/**
 * @file    DeviceCard.qml
 * @version 4.12.0
 * @date    2026-06-14
 * @author  GridYard Team
 * @brief   在线设备列表项 delegate
 *
 * 全部 4 个 property 都声明为 required：当本组件作为 delegate
 * 使用时，QML 引擎会根据名字自动从 model 项里填值（model 项
 * 是 PeerInfo Q_GADGET，其 Q_PROPERTY 名字与本文件 required
 * property 名一一对应）。
 * 支持拖拽文件到卡片触发传输。
 *
 * Change Log:
 * [v4.12.0] DuRuoxian   2026-06-14
 * * 增加悬停、选中、拖放和在线状态过渡
 * [v4.9.0] DuRuoxian   2026-06-13
 * * 增加会话选中样式，点击时传递完整设备信息
 * [v0.3.1] DuRuoxian   2026-06-03
 * * Stage 3.9：添加 DropArea 支持拖拽传输
 * [v0.2.0] DuRuoxian   2026-06-02
 * * Stage 2：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0

ItemDelegate {
    id: deviceCard

    required property string deviceId
    required property string deviceName
    required property string ipAddress
    required property bool   isOnline
    required property bool   isSelected

    readonly property int   kCardHeight: 72
    readonly property color kOnlineColor:  "#3DDC84"
    readonly property color kOfflineColor: "#999999"
    readonly property color kDropHighlight: "#E3F2FD"
    readonly property int kColorDuration: 150
    readonly property int kStatusDuration: 180

    signal cardClicked(string deviceId, string deviceName, string ipAddress, bool isOnline)
    signal fileDropped(string deviceId, string filePath)

    height: kCardHeight

    // 卡片背景样式（拖拽高亮）
    background: Rectangle {
        color: dropArea.containsDrag ? deviceCard.kDropHighlight
             : deviceCard.isSelected ? "#E8F2FC"
             : deviceCard.hovered   ? "#F2F2F2"
             :                           "#FFFFFF"
        border.color: dropArea.containsDrag || deviceCard.isSelected
                      ? "#2196F3" : "#E0E0E0"
        border.width: dropArea.containsDrag || deviceCard.isSelected ? 2 : 1
        radius: 8

        Behavior on color {
            ColorAnimation {
                duration: deviceCard.kColorDuration
                easing.type: Easing.OutCubic
            }
        }

        Behavior on border.color {
            ColorAnimation {
                duration: deviceCard.kColorDuration
                easing.type: Easing.OutCubic
            }
        }
    }

    onClicked: deviceCard.cardClicked(deviceCard.deviceId,
                                         deviceCard.deviceName,
                                         deviceCard.ipAddress,
                                         deviceCard.isOnline)

    // 拖拽接收区域
    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]

        onDropped: function(drop) {
            if (!deviceCard.isOnline) return

            let urls = drop.urls
            for (let i = 0; i < urls.length; i++) {
                // 转换为字符串并去掉 file:// 前缀
                let path = urls[i].toString()
                if (path.startsWith("file://")) {
                    path = path.substring(7)
                }
                deviceCard.fileDropped(deviceCard.deviceId, path)
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
            color: deviceCard.isOnline ? deviceCard.kOnlineColor
                                          : deviceCard.kOfflineColor
            opacity: deviceCard.isOnline ? 1 : 0.55

            Behavior on color {
                ColorAnimation {
                    duration: deviceCard.kStatusDuration
                    easing.type: Easing.OutCubic
                }
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: deviceCard.kStatusDuration
                    easing.type: Easing.OutCubic
                }
            }
        }

        // 设备信息区域
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: deviceCard.deviceName
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: deviceCard.ipAddress
                color: "#666666"
                font.pixelSize: 12
            }
        }

        // 在线状态文字
        Label {
            text: deviceCard.isOnline ? qsTr("在线") : qsTr("离线")
            color: deviceCard.isOnline ? deviceCard.kOnlineColor
                                          : deviceCard.kOfflineColor
            font.pixelSize: 12
        }
    }
}
