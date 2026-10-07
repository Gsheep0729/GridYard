/**
 * @file    PeerListView.qml
 * @version 7.21.2
 * @date 2026-10-06
 * @author  GridYard Team
 * @brief   设备列表组件
 *
 * 绑定 AppController.peerDiscoveryViewModel.peers 显示在线和历史设备。
 * 提供搜索过滤、手动刷新和添加设备入口。
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
    readonly property bool _searchActive: _searchKeyword.length > 0
    // 搜索模式下的合并模型：已加载列表的命中条目在前，列表自身的置顶档与
    // 来源优先级排序保证在线条目天然靠前；其后追加数据库历史命中条目（按
    // deviceId 去重，以离线卡样式展示）。数据库单次检索上限 50 条（视图模型
    // kSearchPeerLimit），命中按置顶与最近活动倒序返回；检索进行中只显示
    // 已加载命中，避免旧关键字的结果闪现
    readonly property var _searchMergedPeers: {
        if (!_searchActive) {
            return []
        }
        const vm = AppController.peerDiscoveryViewModel
        const merged = []
        const seen = new Set()
        const peers = vm.peers
        for (let i = 0; i < peers.length; i++) {
            const peer = peers[i]
            if (matchesPeer(peer.deviceName, peer.ipAddress, peer.alias)) {
                merged.push(peer)
                seen.add(peer.deviceId)
            }
        }
        if (!vm.searchBusy) {
            const hits = vm.searchResults
            for (let j = 0; j < hits.length; j++) {
                if (!seen.has(hits[j].deviceId)) {
                    merged.push(hits[j])
                }
            }
        }
        return merged
    }
    // 过滤后的设备数量，用于搜索态标题显示"N 台"
    readonly property int _filteredCount: _searchActive
                                           ? _searchMergedPeers.length
                                           : AppController.peerDiscoveryViewModel.peers.length
    // 在线台数（含置顶段中的在线设备）：标题行常显，回答"现在能传谁"
    readonly property int _onlineCount: {
        const peers = AppController.peerDiscoveryViewModel.peers
        let n = 0
        for (let i = 0; i < peers.length; i++) {
            if (peers[i].isOnline) {
                n++
            }
        }
        return n
    }
    // "最近见过"段在列表中的条数：段头计数与截尾提示都以它为准
    readonly property int _recentCount: {
        const peers = AppController.peerDiscoveryViewModel.peers
        let n = 0
        for (let i = 0; i < peers.length; i++) {
            if (peers[i].segment === "recent") {
                n++
            }
        }
        return n
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
    // 右键打开菜单的意图上抛（携带打开时捕获的设备上下文，含在线与收藏状态），菜单实例由侧栏窗口层单例持有
    signal contextMenuRequested(string deviceId, string deviceName, bool isPinned, bool isOnline,
                                bool isFavorite)

    color: Style.Color.surfaceMid

    // 搜索防抖：输入停顿 300ms 后才提交数据库检索；清空时立即提交，结果即时回落
    Timer {
        id: searchDebounce
        interval: 300
        onTriggered: AppController.peerDiscoveryViewModel.searchPeers(searchInput.text)
    }

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
                    // 文本与光标避开清除按钮，不再压到按钮下方
                    rightPadding: 16
                    verticalAlignment: Text.AlignVCenter
                    // 原生占位留空：Material 样式会把占位浮成标签，输入后浮到
                    // 固定高度输入框的边框外；占位改由 background 内自绘承载
                    placeholderText: ""
                    font.pixelSize: 13
                    color: Style.Color.textMain
                    clip: true
                    selectByMouse: true
                    background: Item {
                        // 自绘占位（v7.15.12 同款模式）：空输入时垂直居中显示，
                        // 超宽自动省略，输入即隐
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: qsTr("搜索设备名、备注或 IP")
                            color: Style.Color.textWeak
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            visible: searchInput.text.length === 0
                        }
                    }
                    onTextChanged: {
                        if (text.length === 0) {
                            searchDebounce.stop()
                            AppController.peerDiscoveryViewModel.searchPeers("")
                        } else {
                            searchDebounce.restart()
                        }
                    }
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

    // 标题行计数：常规态一眼可见在线台数（绿色呼应在线圆点），搜索态维持命中数
    Label {
        anchors.right: parent.right
        anchors.rightMargin: Style.Space.lg
        anchors.verticalCenter: deviceTitle.verticalCenter
        text: peerListView._searchActive
              ? qsTr("%1 台").arg(peerListView._filteredCount)
              : qsTr("在线 %1 台").arg(peerListView._onlineCount)
        font.pixelSize: 12
        font.bold: !peerListView._searchActive
        color: peerListView._searchActive ? Style.Color.textWeak : Style.Color.success
    }

    // 段头组件：置顶/在线设备/最近见过，样式与"设备"小标题一致；
    // 最近见过段头自带条数。空段不产生条目，也就没有段头
    Component {
        id: sectionHeader

        Item {
            width: listView.width
            height: 30

            Label {
                anchors.left: parent.left
                anchors.leftMargin: Style.Space.lg
                anchors.verticalCenter: parent.verticalCenter
                text: section === "pinned" ? qsTr("置顶")
                      : (section === "online" ? qsTr("在线设备")
                         : qsTr("最近见过 · %1").arg(peerListView._recentCount))
                font.pixelSize: 13
                font.bold: true
                color: Style.Color.textSecondary
            }
        }
    }

    // 最近见过截尾提示：搜索态不分段，提示也随之隐藏
    Component {
        id: recentLimitHint

        Item {
            width: listView.width
            height: 42

            Label {
                anchors.centerIn: parent
                text: qsTr("仅显示最近 %1 台，试试搜索")
                      .arg(AppController.peerDiscoveryViewModel.recentVisibleLimit)
                color: Style.Color.textWeak
                font.pixelSize: 12
            }
        }
    }

    // footer 用 Loader 承载并按条件启停，避免 footer 高度绑定卷入视图布局回写；
    // footer 属性要的是 Component，Loader 包在 Component 内由视图实例化
    Component {
        id: recentLimitFooter

        Loader {
            active: !peerListView._searchActive && peerListView._recentCount >=
                    AppController.peerDiscoveryViewModel.recentVisibleLimit
            sourceComponent: recentLimitHint
        }
    }

    // 设备列表：无关键字绑定全量合并列表，搜索时绑定已加载命中与数据库命中的合并结果
    ListView {
        id: listView
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: deviceTitle.bottom
        anchors.bottom: addDeviceBar.top
        clip: true
        model: peerListView._searchActive
               ? peerListView._searchMergedPeers
               : AppController.peerDiscoveryViewModel.peers
        // 搜索路径不分段：命中条目平铺展示，行为与数量与改前一致
        section.property: peerListView._searchActive ? "" : "segment"
        section.delegate: sectionHeader
        footer: recentLimitFooter
        delegate: Item {
            id: peerDelegate

            required property string deviceId
            required property string deviceName
            required property string ipAddress
            required property bool isOnline
            required property bool pinned
            required property bool favorite
            required property string alias
            required property string lastSeenAt
            // segment 只由 section.property 消费（搜索命中的数据库条目没有该字段，
            // 不声明为 delegate 角色），不在此声明

            width: listView.width
            height: peerListView.kDeviceCardHeight  // 模型已按关键字过滤，条目全部可见

            DeviceCard {
                id: deviceCard

                anchors.fill: parent
                deviceId: peerDelegate.deviceId
                deviceName: peerDelegate.deviceName
                ipAddress: peerDelegate.ipAddress
                isOnline: peerDelegate.isOnline
                isSelected: peerListView.selectedDeviceId === peerDelegate.deviceId
                isPinned: peerDelegate.pinned
                favorite: peerDelegate.favorite
                alias: peerDelegate.alias
                lastSeenAt: peerDelegate.lastSeenAt

                onCardClicked: function(deviceId, deviceName, ipAddress, isOnline) {
                    peerListView.deviceSelected(deviceId, deviceName, ipAddress, isOnline)
                }
                onFilesDropped: function(deviceId, urls) {
                    peerListView.filesDropped(deviceId, urls)
                }
                onContextMenuRequested: function(deviceId, deviceName, isPinned, isOnline,
                                                 isFavorite) {
                    peerListView.contextMenuRequested(deviceId, deviceName, isPinned, isOnline,
                                                      isFavorite)
                }
            }
        }

        // 空状态：搜索无结果与尚未发现设备分别引导；数据库检索进行中不显示，
        // 避免结果回投前的空态闪现
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - Style.Space.xl * 2, 190)
            spacing: Style.Space.sm
            visible: peerListView._filteredCount === 0
                     && !(peerListView._searchActive
                          && AppController.peerDiscoveryViewModel.searchBusy)

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
