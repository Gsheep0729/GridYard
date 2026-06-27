/**
 * @file    PeerListView.qml
 * @version 6.6.2
 * @date    2026-06-24
 * @author  GY
 * @brief   在线设备列表组件
 *
 * 绑定 AppController.discovery.peers 显示发现的其他设备。
 * 支持手动刷新。
 *
 * Change Log:
 * [v6.6.2] GY   2026-06-25
 * * 搜索栏接入设备过滤，右侧入口改为刷新附近设备列表
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
    readonly property int kDeviceCardHeight: 76
    readonly property string _searchKeyword: searchInput.text.trim().toLowerCase()
    readonly property int _filteredCount: {
        const peers = AppController.discovery.peers
        if (_searchKeyword.length === 0) {
            return peers.length
        }

        let count = 0
        for (let i = 0; i < peers.length; i++) {
            if (matchesPeer(peers[i].deviceName, peers[i].ipAddress)) {
                count++
            }
        }
        return count
    }

    function matchesPeer(deviceName: string, ipAddress: string): bool {
        if (_searchKeyword.length === 0) {
            return true
        }

        const name = String(deviceName).toLowerCase()
        const ip = String(ipAddress).toLowerCase()
        return name.indexOf(_searchKeyword) >= 0 || ip.indexOf(_searchKeyword) >= 0
    }

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

                TextField {
                    id: searchInput
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    placeholderText: qsTr("搜索")
                    font.pixelSize: 13
                    color: Style.Color.textMain
                    clip: true
                    background: Item {}

                    onActiveFocusChanged: searchBox._activeFocus = activeFocus
                }

                HoverHandler {
                    cursorShape: Qt.IBeamCursor
                    onHoveredChanged: searchBox._hovered = hovered
                }

                TapHandler {
                    onTapped: searchInput.forceActiveFocus()
                }
            }
            // 刷新附近设备按钮
            ToolButton {
                id: refreshPeersButton
                width: 25; height: 25
                icon.name: "view-refresh"
                ToolTip.text: qsTr("刷新附近设备列表")
                ToolTip.visible: hovered
                onClicked: AppController.discovery.refresh()
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
        text: qsTr("%1 台").arg(peerListView._filteredCount)
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
        delegate: Item {
            id: peerDelegate

            required property string deviceId
            required property string deviceName
            required property string ipAddress
            required property bool isOnline

            readonly property bool _matches: peerListView.matchesPeer(deviceName, ipAddress)

            width: listView.width
            height: _matches ? peerListView.kDeviceCardHeight : 0
            visible: _matches

            DeviceCard {
                id: deviceCard

                anchors.fill: parent
                deviceId: peerDelegate.deviceId
                deviceName: peerDelegate.deviceName
                ipAddress: peerDelegate.ipAddress
                isOnline: peerDelegate.isOnline
                isSelected: peerListView.selectedDeviceId === peerDelegate.deviceId

                onCardClicked: function(deviceId, deviceName, ipAddress, isOnline) {
                    peerListView.deviceSelected(deviceId, deviceName, ipAddress, isOnline)
                }
                onFileDropped: function(deviceId, filePath) {
                    peerListView.fileDropped(deviceId, filePath)
                }
            }
        }

        Label {
            anchors.centerIn: parent
            text: peerListView._searchKeyword.length > 0
                  ? qsTr("没有匹配的设备") : qsTr("正在搜索设备...")
            color: Style.Color.textWeak
            font.pixelSize: 14
            visible: peerListView._filteredCount === 0
        }
    }
}
