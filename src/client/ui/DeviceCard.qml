/**
 * @file    DeviceCard.qml
 * @version 4.16.3
 * @date    2026-06-24
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
 * [v4.16.3] FengChunlin   2026-06-24
 * * 调整设备列表项的头像、在线状态和选中状态表现
 * [v4.16.0] DuRuoxian   2026-06-18
 * * 调整设备卡片为会话列表项，为在线聊天列表铺路
 * [v4.15.2] DuRuoxian   2026-06-17
 * * 优化设备卡片在线色、选中态边界和拖放高亮
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
import "../utils/Style.js" as Style

ItemDelegate {
    id: deviceCard
    hoverEnabled: true
    required property string deviceId
    required property string deviceName
    required property string ipAddress
    required property bool   isOnline
    required property bool   isSelected

    readonly property int   kCardHeight: 76
    readonly property color kOnlineColor:  Style.Color.success
    readonly property color kOfflineColor: Style.Color.textWeak
    readonly property color kSelectedColor: Style.Color.primary
    readonly property color kDropHighlight: Style.Color.primarySoft
    readonly property int kColorDuration: Style.Motion.base
    readonly property int kStatusDuration: Style.Motion.slow

    signal cardClicked(string deviceId, string deviceName, string ipAddress, bool isOnline)
    signal fileDropped(string deviceId, string filePath)

    height: kCardHeight
    background: Rectangle {
        radius: Style.Radius.sm
        color: deviceCard.hovered   ? Style.Color.surfaceLeft    // 悬停浅灰
             : Style.Color.transparent    // 默认透明
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
        anchors.margins: Style.Space.md
        spacing: Style.Space.md

        // 设备头像
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: 4
            color: Style.Color.primary
            Label {
                anchors.centerIn: parent
                text: deviceCard.deviceName.length > 0
                      ? deviceCard.deviceName.charAt(0).toUpperCase()
                      : "?"
                color: deviceCard.isSelected ? Style.Color.surface : Style.Color.textSecondary
                font.pixelSize: 14
                font.bold: true
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
                color: Style.Color.textMain
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: deviceCard.ipAddress
                color: Style.Color.textMuted
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: Style.Space.xs

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: deviceCard.isOnline ? deviceCard.kOnlineColor : deviceCard.kOfflineColor
                opacity: deviceCard.isOnline ? 1 : 0.55

                Behavior on color {
                    ColorAnimation {
                        duration: deviceCard.kStatusDuration
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Label {
                text: deviceCard.isOnline ? qsTr("在线") : qsTr("离线")
                color: deviceCard.isOnline ? deviceCard.kOnlineColor : deviceCard.kOfflineColor
                font.pixelSize: 11
            }
        }
    }
}
