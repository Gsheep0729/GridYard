/**
 * @file    Sidebar.qml
 * @version 7.15.5
 * @date    2026-10-03
 * @author  GridYard Team
 * @brief   左侧设备栏
 *
 * 工具栏（本机头像、设备名、菜单入口）、设备列表、本机信息弹窗与
 * 菜单弹窗的组合。设备选择与拖拽经信号上抛，设置入口发
 * settingsRequested 由主窗口打开弹窗。
 * 从 Main.qml 拆出，弹窗坐标相对工具栏，与原窗口布局一致。
 *
 * Change Log:
 * [v7.15.4] GY   2026-10-03
 * * 自 Main.qml 拆出：工具栏 + 设备列表 + 本机信息/菜单弹窗，
 *   设备选择、拖拽与设置入口经信号上抛
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Rectangle {
    id: sidebar

    // 设备选择与拖拽上抛（拖拽先选中再统一裁决）
    signal deviceSelected(string deviceId)
    signal deviceFilesDropped(string deviceId, var urls)
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
   //         anchors.margins: 5
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
                        Layout.alignment: Qt.AlignTop
                        icon.name: "view-refresh"
                        icon.width: 20
                        icon.height: 20
                        flat: true
                        ToolTip.text: qsTr("刷新")
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
                    Behavior on color { ColorAnimation { duration: Style.Motion.base } }
                }
                onClicked: {
                    menuPopup.close()
                    sidebar.settingsRequested()
                }
            }
        }
    }
}
