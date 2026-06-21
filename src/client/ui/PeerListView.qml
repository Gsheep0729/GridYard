/**
 * @file    PeerListView.qml
 * @version 4.16.3
 * @date    2026-06-24
 * @author  GridYard Team
 * @brief   在线设备列表组件
 *
 * 绑定 AppController.discovery.peers 显示发现的其他设备。
 * 支持手动刷新。
 *
 * Change Log:
 * [v4.16.3] FengChunlin   2026-06-24
 * * 调整设备搜索、会话列表和空状态展示
 * [v4.16.0] DuRuoxian   2026-06-18
 * * 统一设备列表样式，调整为现代会话列表结构
 * [v4.9.0] DuRuoxian   2026-06-13
 * * 增加当前设备选中态
 * [v0.3.1] DuRuoxian   2026-06-03
 * * Stage 3.9：传递拖拽文件信号
 * [v0.3.0] DuRuoxian   2026-06-03
 * * 添加本机信息区域（设备名可编辑 + IP 地址）和刷新按钮
 * [v0.2.0] DuRuoxian   2026-06-02
 * * Stage 2：初始版本
 */

import QtQuick
import QtQuick.Controls
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Rectangle {
    id: peerListView

    property string selectedDeviceId: ""

    signal deviceSelected(string deviceId, string deviceName, string ipAddress, bool isOnline)
    signal fileDropped(string deviceId, string filePath)

    color: Style.Color.surfaceMid

    // 搜索区
    Rectangle {
        id: searchArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        height: 70
        color: Style.Color.transparent
        Row{
            anchors.centerIn: parent
            spacing: 15

            Rectangle {
                id: searchBox
                width: 150; height: 25
                radius: 4
                color: _activeFocus
                       ? Style.Color.surfaceMid
                       : (_hovered ? Style.Color.surfaceSoft : Style.Color.select)
                border.width: _activeFocus? 1.5 : 0
                border.color: Style.Color.textfield
                property bool _hovered: false
                property bool _activeFocus: false

                Behavior on color {
                    ColorAnimation { duration: Style.Motion.base }
                }

                TextInput {
                    id: searchInput
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: TextInput.AlignVCenter
                    font.pixelSize: 13
                    color: Style.Color.textMain
                    clip: true

                    onActiveFocusChanged: searchBox._activeFocus = activeFocus
                }

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("搜索")
                    font.pixelSize: 13
                    color: Style.Color.textWeak
                    visible: searchInput.text.length === 0 && !searchBox._activeFocus
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.IBeamCursor
                    onEntered: searchBox._hovered = true
                    onExited: searchBox._hovered = false
                    onClicked: searchInput.forceActiveFocus()
                }
            }
            // + 按钮
            Rectangle {
                id: addBtn
                width: 25; height: 25
                radius: 4
                color: _hovered ? Style.Color.surfaceSoft : Style.Color.select

                property bool _hovered: false

                Label {
                    anchors.centerIn: parent
                    text: "+"
                    font.pixelSize: 16
                    color: Style.Color.textSecondary
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: addBtn._hovered = true
                    onExited: addBtn._hovered = false
                    onClicked: addFriendPopup.open()
                }

                Behavior on color {
                    ColorAnimation { duration: Style.Motion.base }
                }
            }
        }
    }

    // 附近设备标题
    Label {
        id: deviceTitle
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.top: searchArea.bottom
        anchors.topMargin: 4
        text: qsTr("附近设备")
        font.pixelSize: 13
        font.bold: true
        color: Style.Color.textSecondary
    }

    Label {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: deviceTitle.verticalCenter
        text: qsTr("%1 台").arg(listView.count)
        font.pixelSize: 12
        color: Style.Color.textWeak
    }

    // 设备列表
    ListView {
        id: listView
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: deviceTitle.bottom
        anchors.bottom: parent.bottom
        clip: true
        model: AppController.discovery.peers
        delegate: DeviceCard {
            width: listView.width
            isSelected: peerListView.selectedDeviceId === deviceId
            onCardClicked: function(deviceId, deviceName, ipAddress, isOnline) {
                peerListView.deviceSelected(deviceId, deviceName, ipAddress, isOnline)
            }
            onFileDropped: function(deviceId, filePath) {
                peerListView.fileDropped(deviceId, filePath)
            }
        }

        Label {
            anchors.centerIn: parent
            text: qsTr("正在搜索设备...")
            color: Style.Color.textWeak
            font.pixelSize: 14
            visible: listView.count === 0
        }
    }
}
