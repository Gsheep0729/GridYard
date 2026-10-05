/**
 * @file    Sidebar.qml
 * @version 7.19.0
 * @date 2026-10-05
 * @author  GridYard Team
 * @brief   左侧设备栏
 *
 * 工具栏（本机头像、设备名、菜单入口）、设备列表、本机信息弹窗与
 * 菜单弹窗的组合。设备选择与拖拽经信号上抛，设置入口发
 * settingsRequested 由主窗口打开弹窗。
 * 从 Main.qml 拆出，弹窗坐标相对工具栏，与原窗口布局一致。
 *
 * Change Log:
 * [v7.19.0] GY   2026-10-05
 * * 版本头对齐到 v7.19.0
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 新增窗口层单例设备右键菜单，捕获上下文并加阴影与层次底色
 * [v7.17.4] GY   2026-10-04
 * * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 设备卡右键菜单意图经侧栏上抛至主窗口
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
 * * 本机信息弹窗刷新图标改为自绘，tooltip 加延迟
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.15.4] GY   2026-10-03
 * * 自 Main.qml 拆出：工具栏 + 设备列表 + 本机信息/菜单弹窗，
 *   设备选择、拖拽与设置入口经信号上抛
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Rectangle {
    id: sidebar

    // 设备选择与拖拽上抛（拖拽先选中再统一裁决）
    signal deviceSelected(string deviceId)
    signal deviceFilesDropped(string deviceId, var urls)
    // 设备卡右键打开菜单的意图（携带打开时捕获的设备上下文），由本组件的单例菜单承接
    signal contextMenuRequested(string deviceId, string deviceName, bool isPinned)
    // 菜单里的动作意图上抛（pin/unpin/hide/rename/delete），由 Main 接确认弹窗与控制器
    signal contextActionRequested(string deviceId, string deviceName, string action)
    // 菜单里的设置入口
    signal settingsRequested()

    anchors.left: parent.left
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: 272  // 工具栏 62px + 设备列表 210px
    color: Style.Color.surfaceLeft

    // 工具栏（62px）
    Rectangle {
        id: toolbar
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 62
        color: Style.Color.surfaceLeft

        // 本机头像
        Rectangle {
            id: avatarBtn
            anchors.top: parent.top
            anchors.topMargin: 16
            anchors.horizontalCenter: parent.horizontalCenter
            width: 36; height: 36
            radius: 18
            color: Style.Color.primary

            property bool _hovered: false

            Label {
                anchors.centerIn: parent
                text: "我"
                color: Style.Color.textOnAccent
                font.pixelSize: 12
                font.bold: true
            }

            HoverHandler {
                onHoveredChanged: avatarBtn._hovered = hovered
            }

            TapHandler {
                onTapped: deviceInfoPopup.open()
            }
        }

        // 设备名
        Label {
            anchors.top: avatarBtn.bottom
            anchors.topMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 10
            text: ConfigManager.deviceName
            font.pixelSize: 10
            color: Style.Color.textSecondary
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }

        // 菜单按钮（底部）
        Rectangle {
            id: menuBtn
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            anchors.horizontalCenter: parent.horizontalCenter
            width: 41; height: 41
            radius: 3

            property bool _hovered: false
            property bool _pressed: false

            color: _pressed
                   ? Style.Color.menubarClicked
                   : (_hovered ? Style.Color.menubarSelect: Style.Color.surfaceLeft)

            Behavior on color { ColorAnimation { duration: Style.Motion.base } }

            // 三横线
            Item {
                anchors.centerIn: parent
                width: 16; height: 13

                Rectangle {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 16; height: 2
                    radius: 1
                    color: Style.Color.menubar
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 16; height: 2
                    radius: 1
                    color: Style.Color.menubar
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 16; height: 2
                    radius: 1
                    color: Style.Color.menubar
                }
            }

            HoverHandler {
                onHoveredChanged: {
                    menuBtn._hovered = hovered
                    if (!hovered) {
                        menuBtn._pressed = false
                    }
                }
            }

            TapHandler {
                onPressedChanged: menuBtn._pressed = pressed
                onTapped: menuPopup.open()
            }
        }
    }

    // 设备列表
    PeerListView {
        id: peerList
        anchors.left: toolbar.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 210
        selectedDeviceId: AppController.peerDiscoveryViewModel.selectedDeviceId

        onDeviceSelected: (deviceId, deviceName, ipAddress, isOnline) =>
            sidebar.deviceSelected(deviceId)
        onFilesDropped: (deviceId, urls) =>
            sidebar.deviceFilesDropped(deviceId, urls)
        onContextMenuRequested: (deviceId, deviceName, isPinned) =>
            deviceContextMenu.openFor(deviceId, deviceName, isPinned)
    }

    // 设备卡右键菜单：单例挂在窗口层，列表心跳刷新销毁重建 delegate 不影响已打开的菜单。
    // 打开时捕获设备上下文，菜单项触发时按捕获值上报意图（业务判断全部在 Main 装配层；
    // 设备若已不存在，后端管理接口的幂等语义自行兜住）
    Menu {
        id: deviceContextMenu

        // 打开时捕获的设备上下文：菜单项触发以捕获值为准，不随列表刷新变化
        property string deviceId: ""
        property string deviceName: ""
        property bool isPinned: false

        function openFor(id, name, pinned) {
            deviceContextMenu.deviceId = id
            deviceContextMenu.deviceName = name
            deviceContextMenu.isPinned = pinned
            deviceContextMenu.popup()  // 光标处打开，即右键所在卡片位置
        }

        width: 160
        topPadding: 4
        bottomPadding: 4
        leftPadding: 4
        rightPadding: 4

        background: Item {
            id: deviceMenuShell

            // 菜单阴影：全应用唯一的阴影使用点，半径与不透明度取克制档位避免脏边
            MultiEffect {
                anchors.fill: parent
                source: deviceMenuPanel
                autoPaddingEnabled: true
                shadowEnabled: true
                shadowBlur: 0.85
                shadowVerticalOffset: 4
                shadowColor: Style.Color.menuShadow
            }

            // 底色用 menuBackground 与白色弹窗/卡片形成深浅层次
            Rectangle {
                id: deviceMenuPanel
                anchors.fill: parent
                color: Style.Color.menuBackground
                radius: Style.Radius.md
                border.color: Style.Color.borderSoft
                border.width: 1
            }
        }

        MenuItem {
            id: pinItem
            text: deviceContextMenu.isPinned ? qsTr("取消置顶") : qsTr("置顶该聊天")
            contentItem: Label {
                text: pinItem.text
                font.pixelSize: 13
                color: pinItem.hovered ? Style.Color.primary : Style.Color.textMain
                verticalAlignment: Text.AlignVCenter
                leftPadding: Style.Space.sm
            }
            // 悬停高亮即时切换：高亮一端是透明色，颜色动画会在快速进出时
            // 反复穿过深灰半透明区，观感是整行深色闪烁（同 UI-1 悬停纪律）
            background: Rectangle {
                color: pinItem.hovered ? Style.Color.surfaceSoft : Style.Color.transparent
                radius: Style.Radius.sm
            }
            onTriggered: sidebar.contextActionRequested(
                             deviceContextMenu.deviceId, deviceContextMenu.deviceName,
                             deviceContextMenu.isPinned ? "unpin" : "pin")
        }

        MenuItem {
            id: hideItem
            text: qsTr("不显示该聊天")
            contentItem: Label {
                text: hideItem.text
                font.pixelSize: 13
                color: hideItem.hovered ? Style.Color.primary : Style.Color.textMain
                verticalAlignment: Text.AlignVCenter
                leftPadding: Style.Space.sm
            }
            background: Rectangle {
                color: hideItem.hovered ? Style.Color.surfaceSoft : Style.Color.transparent
                radius: Style.Radius.sm
            }
            onTriggered: sidebar.contextActionRequested(
                             deviceContextMenu.deviceId, deviceContextMenu.deviceName, "hide")
        }

        MenuItem {
            id: renameItem
            text: qsTr("设置备注")
            contentItem: Label {
                text: renameItem.text
                font.pixelSize: 13
                color: renameItem.hovered ? Style.Color.primary : Style.Color.textMain
                verticalAlignment: Text.AlignVCenter
                leftPadding: Style.Space.sm
            }
            background: Rectangle {
                color: renameItem.hovered ? Style.Color.surfaceSoft : Style.Color.transparent
                radius: Style.Radius.sm
            }
            // 备注编辑入口：上报意图，由装配层打开备注弹窗
            onTriggered: sidebar.contextActionRequested(
                             deviceContextMenu.deviceId, deviceContextMenu.deviceName, "rename")
        }

        MenuItem {
            id: deleteItem
            text: qsTr("删除该聊天")
            contentItem: Label {
                text: deleteItem.text
                font.pixelSize: 13
                // 危险操作沿用 Style 语义色 error，与确认弹窗的危险按钮呼应
                color: deleteItem.hovered ? Style.Color.errorHover : Style.Color.error
                verticalAlignment: Text.AlignVCenter
                leftPadding: Style.Space.sm
            }
            background: Rectangle {
                color: deleteItem.hovered ? Style.Color.surfaceSoft : Style.Color.transparent
                radius: Style.Radius.sm
            }
            onTriggered: sidebar.contextActionRequested(
                             deviceContextMenu.deviceId, deviceContextMenu.deviceName, "delete")
        }
    }

    // 本机信息弹出窗口(FCL)
    Popup {
        id: deviceInfoPopup
        x: 70; y: 10
        width: 282; height: 110
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        //弹出的个人主机信息框（FCL）
        background: Rectangle {
            color: Style.Color.window
            border.color: Style.Color.borderSoft
            border.width: 1
            radius: 12
        }
        //个人信息框，头像 + 信息列表
        contentItem: ColumnLayout {
            anchors.fill: parent
            spacing: Style.Space.md
                RowLayout{
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    spacing: 25
                    //左侧头像
                    Rectangle{
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredHeight: 56
                        Layout.preferredWidth: 56
                        radius: 28
                        color: Style.Color.primary
                        Label{
                            anchors.centerIn: parent
                            text: "我"
                            color: Style.Color.textOnAccent
                            font.pixelSize: 14
                            font.bold: true
                        }
                    }
                //中间信息列
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        spacing:2
                        Label {
                            Layout.fillWidth: true
                            text: ConfigManager.deviceName
                            color: Style.Color.textMain
                            font.pixelSize: 14
                            font.bold: true
                            elide: Text.ElideRight  // 文字太长时显示省略号
                        }

                        Label {
                            Layout.alignment: Qt.AlignTop
                            text: ConfigManager.localIp.length > 0
                                  ? ConfigManager.localIp : qsTr("未获取到 IP")
                            color: Style.Color.textMuted
                            font.pixelSize: 12
                        }

                        Label {
                            text: "%1 v%2".arg(AppController.applicationName)
                                           .arg(AppController.applicationVersion)
                            color: Style.Color.textWeak
                            font.pixelSize: 11
                        }
                    }
                    Button {
                        id: refreshInfoButton
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 32
                        implicitHeight: 32
                        flat: true
                        padding: 6
                        // 图标自绘，不依赖系统图标主题
                        contentItem: RefreshIcon {
                            iconColor: refreshInfoButton.hovered
                                       ? Style.Color.primary : Style.Color.textSecondary
                        }
                        ToolTip.text: qsTr("刷新")
                        ToolTip.delay: 500
                        ToolTip.visible: hovered
                        onClicked: AppController.peerDiscoveryViewModel.refresh()
                    }
                }
            }
    }

    // 菜单弹出窗口
    Popup {
        id: menuPopup
        x: 70
        y: sidebar.height - height - 10
        width: 160
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Style.Color.surface
            radius: Style.Radius.md
            border.color: Style.Color.borderSoft
            border.width: 1
        }

        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 2

            ItemDelegate {
                id: settingsItem
                Layout.fillWidth: true
                text: qsTr("设置")
                contentItem: Label {
                    text: settingsItem.text
                    font.pixelSize: 13
                    color: settingsItem.hovered ? Style.Color.primary : Style.Color.textMain
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Style.Space.sm
                }
                background: Rectangle {
                    color: settingsItem.hovered ? Style.Color.surfaceSoft : Style.Color.transparent
                    radius: Style.Radius.sm
                }
                onClicked: {
                    menuPopup.close()
                    sidebar.settingsRequested()
                }
            }
        }
    }
}
