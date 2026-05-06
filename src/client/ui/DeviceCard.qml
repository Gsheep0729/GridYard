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
 *
 * Change Log:
 * [v0.1] GY   2026-06-02
 * * Stage 2：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ItemDelegate {
    id: tw_deviceCard

    required property string deviceId
    required property string deviceName
    required property string ipAddress
    required property bool   isOnline

    readonly property int   kCardHeight: 72
    readonly property color kOnlineColor:  "#3DDC84"
    readonly property color kOfflineColor: "#999999"

    signal cardClicked(string deviceId)

    height: kCardHeight

    // 卡片背景样式
    background: Rectangle {
        color:        tw_deviceCard.hovered ? "#F2F2F2" : "#FFFFFF"
        border.color: "#E0E0E0"
        border.width: 1
        radius:       8
    }

    onClicked: tw_deviceCard.cardClicked(tw_deviceCard.deviceId)

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
