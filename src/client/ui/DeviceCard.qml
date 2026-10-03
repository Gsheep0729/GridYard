/**
 * @file    DeviceCard.qml
 * @version 7.15.19
 * @date    2026-10-04
 * @author  GridYard Team
 * @brief   在线设备列表项 delegate
 *
 * 全部 property 都声明为 required：当本组件作为 delegate
 * 使用时，QML 引擎会根据名字自动从 model 项里填值（model 项
 * 是 PeerInfo Q_GADGET，其 Q_PROPERTY 名字与本文件 required
 * property 名一一对应）。
 * 支持拖拽文件到卡片触发传输；拖拽内容原样向上冒泡，
 * 由 Main.qml 统一解码、过滤并裁决设备是否在线。
 *
 * Change Log:
 * [v7.15.19] GY   2026-10-04
 * * 版本头对齐到 v7.15.19
 * [v7.15.18] GY   2026-10-04
 * * 版本头对齐到 v7.15.18
 * [v7.15.17] GY   2026-10-04
 * * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
 * [v7.15.13] GY   2026-10-04
 * * 版本头对齐到 v7.15.13
 * [v7.15.12] GY   2026-10-03
 * * 选中底色与悬停一样即时切换，消除切换设备时颜色动画被卡出的闪烁
 * [v7.15.11] GY   2026-10-03
 * * 悬停底色改为即时切换并移除离线卡 tooltip，消除鼠标扫过卡片时的闪烁
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.11.0] GY   2026-10-02
 * * 恢复选中态指示条，离线卡片整体淡化，拖拽改为整体冒泡
 * [v6.6.2] GY   2026-06-25
 * * 同步文件头版本与当前主版本
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
    // 以下 4 个 required property 与 PeerInfo Q_GADGET 的 Q_PROPERTY 名一一对应，
    // QML 引擎会根据名字自动从 model 项里填值，无需手动绑定。
    required property string deviceId
    required property string deviceName
    required property string ipAddress
    required property bool   isOnline
    required property bool   isSelected

    readonly property int   kCardHeight: 76
    readonly property color kOnlineColor:  Style.Color.success   // 在线状态圆点颜色
    readonly property color kOfflineColor: Style.Color.textWeak  // 离线状态圆点颜色
    readonly property int kStatusDuration: Style.Motion.slow     // 在线状态切换动画时长

    signal cardClicked(string deviceId, string deviceName, string ipAddress, bool isOnline)
    signal filesDropped(string deviceId, var urls)

    height: kCardHeight
    background: Rectangle {
        radius: Style.Radius.sm
        // 选中态与悬停底色都即时切换：切换设备时颜色动画会被会话页重建卡出中间帧，观感是闪烁
        color: deviceCard.isSelected ? Style.Color.primarySoft
             : (deviceCard.hovered ? Style.Color.surfaceLeft : Style.Color.transparent)

        // 左侧选中指示条：明确标识当前会话设备
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: Style.Space.sm
            width: 3
            radius: 1.5
            color: Style.Color.primary
            visible: deviceCard.isSelected
        }
    }

    // 点击卡片时向父级传递完整设备信息，由 PeerListView 再向上冒泡到 Main.qml
    onClicked: deviceCard.cardClicked(deviceCard.deviceId,
                                         deviceCard.deviceName,
                                         deviceCard.ipAddress,
                                         deviceCard.isOnline)

    // 拖拽接收区域：拖拽内容原样向上冒泡，由 Main.qml 统一裁决
    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]  // 接受文件 URI 列表

        onEntered: function(drag) {
            drag.accept(Qt.CopyAction)
        }

        onDropped: function(drop) {
            deviceCard.filesDropped(deviceCard.deviceId, drop.urls)
        }
    }

    // 离线设备用颜色弱化区分：设备名与头像降为次级色，
    // 状态点与状态文字各自绑定在线色，与在线设备形成明显视觉差
    readonly property color kNameColor: deviceCard.isOnline
                                        ? Style.Color.textMain : Style.Color.textMuted
    readonly property color kAvatarColor: deviceCard.isOnline
                                          ? Style.Color.primary : Style.Color.textWeak

    RowLayout {
        anchors.fill: parent
        anchors.margins: Style.Space.md
        anchors.leftMargin: Style.Space.lg
        spacing: Style.Space.md

        // 设备头像：取设备名首字母作为标识
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: 18
            color: deviceCard.kAvatarColor
            Label {
                anchors.centerIn: parent
                text: deviceCard.deviceName.length > 0
                      ? deviceCard.deviceName.charAt(0).toUpperCase()
                      : "?"
                color: Style.Color.textOnAccent
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
                color: deviceCard.kNameColor
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

            // 在线状态圆点：颜色和透明度都带过渡动画
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: deviceCard.isOnline ? deviceCard.kOnlineColor : deviceCard.kOfflineColor
                opacity: deviceCard.isOnline ? 1 : 0.7  // 淡化叠加上圆点透明度

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
