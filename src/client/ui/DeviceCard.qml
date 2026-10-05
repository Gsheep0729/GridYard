/**
 * @file    DeviceCard.qml
 * @version 7.20.3
 * @date 2026-10-05
 * @author  GridYard Team
 * @brief   在线设备列表项 delegate
 *
 * 全部 property 都声明为 required：当本组件作为 delegate
 * 使用时，QML 引擎会根据名字自动从 model 项里填值（model 项
 * 是视图模型输出的展示字段映射，字段名与本文件 required
 * property 名一一对应）。
 * 支持拖拽文件到卡片触发传输；拖拽内容原样向上冒泡，
 * 由 Main.qml 统一解码、过滤并裁决设备是否在线。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/FormatUtils.js" as FormatUtils
import "../utils/Style.js" as Style

ItemDelegate {
    id: deviceCard
    hoverEnabled: true
    // 以下 required property 与视图模型条目的字段名一一对应，QML 引擎会
    // 根据名字自动从 model 项里填值，无需手动绑定。
    required property string deviceId
    required property string deviceName
    required property string ipAddress
    required property bool   isOnline
    required property bool   isSelected
    // 最后见过时间（ISO 文本）：离线卡片据此显示相对时间，在线卡不消费
    required property string lastSeenAt

    // 置顶状态由视图模型按数据库状态注入，仅用于菜单文案展示
    property bool isPinned: false

    // 本地备注由视图模型按数据库状态注入，展示名备注优先于广播名
    required property string alias
    readonly property string displayName: FormatUtils.displayName(deviceCard.alias,
                                                                  deviceCard.deviceName)
    // 备注是否生效：生效时副行补出原名便于核对（远程改名仍可见）
    readonly property bool hasAlias: String(deviceCard.alias ?? "").trim().length > 0

    readonly property int   kCardHeight: 76
    readonly property color kOnlineColor:  Style.Color.success   // 在线状态圆点颜色
    readonly property color kOfflineColor: Style.Color.textWeak  // 离线状态圆点颜色
    readonly property int kStatusDuration: Style.Motion.slow     // 在线状态切换动画时长

    signal cardClicked(string deviceId, string deviceName, string ipAddress, bool isOnline)
    signal filesDropped(string deviceId, var urls)
    // 右键手势只上报"在某设备处请求上下文菜单"的意图并携带打开时的设备上下文
    // （含在线状态，供装配层决定删除入口是否可用）；菜单实例挂在窗口层单例打开，
    // 列表刷新销毁重建 delegate 不影响已打开的菜单，菜单项触发时按捕获上下文
    // 上报 pin/unpin/hide/rename/delete 意图给装配层
    signal contextMenuRequested(string deviceId, string deviceName, bool isPinned, bool isOnline)

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

    // 右键手势：只接管右键，与左键选中（ItemDelegate onClicked）和拖拽（DropArea）互不干扰
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: deviceCard.contextMenuRequested(deviceCard.deviceId,
                                                  deviceCard.deviceName,
                                                  deviceCard.isPinned,
                                                  deviceCard.isOnline)
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

        // 设备头像：取展示名（备注优先）首字母作为标识
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: 18
            color: deviceCard.kAvatarColor
            Label {
                anchors.centerIn: parent
                text: deviceCard.displayName.length > 0
                      ? deviceCard.displayName.charAt(0).toUpperCase()
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
                text: deviceCard.displayName
                font.pixelSize: 14
                font.bold: true
                color: deviceCard.kNameColor
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            // 副行：备注生效时"原名 · IP"并存（核对同名设备、察觉远程改名），
            // 无备注维持纯 IP
            Label {
                text: deviceCard.hasAlias
                      ? deviceCard.deviceName + " · " + deviceCard.ipAddress
                      : deviceCard.ipAddress
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
                // 在线卡保持"在线"；离线卡把固定状态换成最后在线的相对时间，
                // 时间缺失时回落"离线"
                text: deviceCard.isOnline
                      ? qsTr("在线")
                      : (FormatUtils.relativeSeen(deviceCard.lastSeenAt) || qsTr("离线"))
                color: deviceCard.isOnline ? deviceCard.kOnlineColor : deviceCard.kOfflineColor
                font.pixelSize: 11
            }
        }
    }
}
