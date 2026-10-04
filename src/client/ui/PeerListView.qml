/**
 * @file    PeerListView.qml
 * @version 7.17.3
 * @date 2026-10-04
 * @author  GridYard Team
 * @brief   设备列表组件
 *
 * 绑定 AppController.peerDiscoveryViewModel.peers 显示在线和历史设备。
 * 提供搜索过滤、手动刷新和添加设备入口。
 *
 * Change Log:
* [v7.17.3] GY   2026-10-04
* * 搜索过滤在设备名与 IP 之外加入备注匹配，delegate 补 alias 字段
* [v7.17.2] GY   2026-10-04
* * 右键菜单意图经列表信号上抛，delegate 补 pinned 字段
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
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
 * 版本头对齐到 v7.15.12
 * [v7.15.11] GY   2026-10-03
 * * 刷新按钮图标改为自绘，tooltip 加延迟避免鼠标扫过时即时闪现
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.11.0] GY   2026-10-02
 * * 空状态分层引导并接入添加设备弹窗，搜索区改为弹性布局
 * [v6.7.0] GY   2026-06-28
 * * 设备列表支持显示离线历史设备
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
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Rectangle {
    id: peerListView

    property string selectedDeviceId: ""  // 当前选中的设备 ID，由 Main.qml 设置
    readonly property int kDeviceCardHeight: 76  // 单个设备卡片的固定高度
    readonly property string _searchKeyword: searchInput.text.trim().toLowerCase()
    // 过滤后的设备数量，用于标题栏显示"N 台"
    readonly property int _filteredCount: {
        const peers = AppController.peerDiscoveryViewModel.peers
        if (_searchKeyword.length === 0) {
            return peers.length
        }

        let count = 0
        for (let i = 0; i < peers.length; i++) {
            if (matchesPeer(peers[i].deviceName, peers[i].ipAddress, peers[i].alias)) {
                count++
            }
        }
        return count
    }

    // 模糊匹配：同时搜索设备名、本地备注和 IP 地址，任一包含关键词即匹配
    function matchesPeer(deviceName: string, ipAddress: string, alias: string): bool {
        if (_searchKeyword.length === 0) {
            return true
        }

        const name = String(deviceName).toLowerCase()
        const ip = String(ipAddress).toLowerCase()
        const remark = String(alias).toLowerCase()
        return name.indexOf(_searchKeyword) >= 0 || ip.indexOf(_searchKeyword) >= 0
               || remark.indexOf(_searchKeyword) >= 0
    }

    signal deviceSelected(string deviceId, string deviceName, string ipAddress, bool isOnline)
    signal filesDropped(string deviceId, var urls)  // 拖拽文件到设备卡片时触发，由 Main 统一裁决
    // 右键菜单意图上抛（pin/unpin/hide/rename/delete），由 Main 接确认弹窗与控制器
    signal contextActionRequested(string deviceId, string deviceName, string action)

    color: Style.Color.surfaceMid

    // 搜索区：搜索输入框 + 刷新按钮
    Item {
        id: searchArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Style.Space.sm
        height: 48

        RowLayout {
            anchors.fill: parent
            spacing: Style.Space.sm

            // 搜索输入框容器：管理焦点和悬停视觉反馈
            Rectangle {
                id: searchBox
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Style.Radius.sm
                color: Style.Color.window
                border.width: searchInput.activeFocus ? 1.5 : 1
                border.color: searchInput.activeFocus
                              ? Style.Color.primary : Style.Color.border

                Behavior on border.color {
                    ColorAnimation { duration: Style.Motion.base }
                }

                TextField {
                    id: searchInput
                    anchors.fill: parent
                    anchors.leftMargin: Style.Space.md
                    anchors.rightMargin: Style.Space.md
                    verticalAlignment: Text.AlignVCenter
                    placeholderText: qsTr("搜索设备名、备注或 IP")
                    font.pixelSize: 13
                    color: Style.Color.textMain
                    clip: true
                    selectByMouse: true
                    background: Item {}
                }

                // 清空按钮：输入内容时出现
                ToolButton {
                    anchors.right: parent.right
                    anchors.rightMargin: Style.Space.xs
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20; height: 20
                    visible: searchInput.text.length > 0
                    text: "×"
                    font.pixelSize: 13
                    onClicked: {
                        searchInput.clear()
                        searchInput.forceActiveFocus()
                    }
                }
            }

            // 刷新设备按钮：同时刷新在线发现和本地历史设备
            // 图标自绘，不依赖系统图标主题
            ToolButton {
                id: refreshPeersButton
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                padding: 8
                contentItem: RefreshIcon {
                    iconColor: refreshPeersButton.hovered
                               ? Style.Color.primary : Style.Color.textSecondary
                }
                ToolTip.text: qsTr("刷新设备列表")
                ToolTip.delay: 500
                ToolTip.visible: hovered
                onClicked: AppController.peerDiscoveryViewModel.refresh()
            }
        }
    }

    // 设备标题：在线和历史设备统一展示
    Label {
        id: deviceTitle
        anchors.left: parent.left
        anchors.leftMargin: Style.Space.lg
        anchors.top: searchArea.bottom
        anchors.topMargin: Style.Space.xs
        text: qsTr("设备")
        font.pixelSize: 13
        font.bold: true
        color: Style.Color.textSecondary
    }

    // 设备数量标签：显示过滤后的设备数量
    Label {
        anchors.right: parent.right
        anchors.rightMargin: Style.Space.lg
        anchors.verticalCenter: deviceTitle.verticalCenter
        text: qsTr("%1 台").arg(peerListView._filteredCount)
        font.pixelSize: 12
        color: Style.Color.textWeak
    }

    // 设备列表：绑定在线设备数组，delegate 自动从 PeerInfo 填充 required property
    ListView {
        id: listView
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: deviceTitle.bottom
        anchors.bottom: addDeviceBar.top
        clip: true
        model: AppController.peerDiscoveryViewModel.peers
        delegate: Item {
            id: peerDelegate

            required property string deviceId
            required property string deviceName
            required property string ipAddress
            required property bool isOnline
            required property bool pinned
            required property string alias

            readonly property bool _matches: peerListView.matchesPeer(deviceName, ipAddress, alias)

            width: listView.width
            height: _matches ? peerListView.kDeviceCardHeight : 0  // 不匹配时高度为 0 实现隐藏
            visible: _matches

            DeviceCard {
                id: deviceCard

                anchors.fill: parent
                deviceId: peerDelegate.deviceId
                deviceName: peerDelegate.deviceName
                ipAddress: peerDelegate.ipAddress
                isOnline: peerDelegate.isOnline
                isSelected: peerListView.selectedDeviceId === peerDelegate.deviceId
                isPinned: peerDelegate.pinned
                alias: peerDelegate.alias

                onCardClicked: function(deviceId, deviceName, ipAddress, isOnline) {
                    peerListView.deviceSelected(deviceId, deviceName, ipAddress, isOnline)
                }
                onFilesDropped: function(deviceId, urls) {
                    peerListView.filesDropped(deviceId, urls)
                }
                onContextActionRequested: function(deviceId, deviceName, action) {
                    peerListView.contextActionRequested(deviceId, deviceName, action)
                }
            }
        }

        // 空状态：搜索无结果与尚未发现设备分别引导
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - Style.Space.xl * 2, 190)
            spacing: Style.Space.sm
            visible: peerListView._filteredCount === 0

            // 搜索无结果
            ColumnLayout {
                visible: peerListView._searchKeyword.length > 0
                spacing: Style.Space.xs
                Layout.alignment: Qt.AlignHCenter

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("没有匹配的设备")
                    color: Style.Color.textWeak
                    font.pixelSize: 14
                    font.bold: true
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("换个关键词试试")
                    color: Style.Color.textMuted
                    font.pixelSize: 12
                }
            }

            // 尚未发现任何设备：给新用户明确的下一步
            ColumnLayout {
                visible: peerListView._searchKeyword.length === 0
                spacing: Style.Space.sm
                Layout.alignment: Qt.AlignHCenter

                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 28
                    implicitHeight: 28
                    running: peerListView._filteredCount === 0
                             && peerListView._searchKeyword.length === 0
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("正在搜索附近设备...")
                    color: Style.Color.textWeak
                    font.pixelSize: 14
                    font.bold: true
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("请确认对方已打开 GridYard 并连接同一网络；跨网段可通过邀请码添加")
                    color: Style.Color.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }

                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("添加设备")
                    highlighted: true
                    onClicked: addDeviceDialog.open()
                }
            }
        }
    }

    // 底部固定入口：任何时候都能添加设备
    Rectangle {
        id: addDeviceBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 44
        color: Style.Color.transparent

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Style.Color.border
        }

        ItemDelegate {
            id: addDeviceItem
            anchors.fill: parent
            hoverEnabled: true

            RowLayout {
                anchors.centerIn: parent
                spacing: Style.Space.xs

                Label {
                    text: "+"
                    font.pixelSize: 15
                    font.bold: true
                    color: addDeviceItem.hovered
                           ? Style.Color.primary : Style.Color.textSecondary
                }

                Label {
                    text: qsTr("添加设备")
                    font.pixelSize: 13
                    color: addDeviceItem.hovered
                           ? Style.Color.primary : Style.Color.textSecondary
                }
            }

            onClicked: addDeviceDialog.open()
        }
    }

    AddDeviceDialog {
        id: addDeviceDialog
        anchors.centerIn: Overlay.overlay

        onDeviceAdded: AppController.peerDiscoveryViewModel.refresh()
    }
}
