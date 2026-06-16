/**
 * @file    PeerListView.qml
 * @version 4.16.0
 * @date    2026-06-18
 * @author  GridYard Team
 * @brief   在线设备列表组件
 *
 * 顶部显示本机设备名（可编辑）和 IP 地址，下方绑定
 * AppController.discovery.peers 显示发现的其他设备。
 * 支持手动刷新和设备名即时修改。
 *
 * Change Log:
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

Frame {
    id: peerListView

    property string selectedDeviceId: ""

    signal deviceSelected(string deviceId, string deviceName, string ipAddress, bool isOnline)
    signal fileDropped(string deviceId, string filePath)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.Space.sm
        spacing: Style.Space.sm

        // 本机信息区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            color: Style.Color.primarySoft
            radius: Style.Radius.lg

            RowLayout {
                anchors.fill: parent
                anchors.margins: Style.Space.md
                spacing: Style.Space.md

                // 本机标识
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    radius: 20
                    color: Style.Color.primary

                    Label {
                        anchors.centerIn: parent
                        text: "我"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.bold: true
                    }
                }

                // 设备名 + IP
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    TextField {
                        id: nameField
                        Layout.fillWidth: true
                        text: ConfigManager.deviceName
                        placeholderText: qsTr("输入设备名称")
                        font.pixelSize: 14
                        font.bold: true
                        background: Rectangle {
                            color: nameField.activeFocus ? Style.Color.surface : Style.Color.transparent
                            border.color: nameField.activeFocus ? Style.Color.primary : Style.Color.transparent
                            border.width: 1
                            radius: Style.Radius.xs
                        }
                        padding: 2
                        selectByMouse: true

                        onEditingFinished: {
                            let trimmed = text.trim()
                            if (trimmed.length > 0 && trimmed !== ConfigManager.deviceName) {
                                ConfigManager.deviceName = trimmed
                            }
                            focus = false
                        }

                        // 同步外部修改（如 SettingsDialog）
                        Connections {
                            target: ConfigManager
                            function onDeviceNameChanged() {
                                if (!nameField.activeFocus) {
                                    nameField.text = ConfigManager.deviceName
                                }
                                // 通知 DiscoveryService 设备名称已更新，触发广播
                                AppController.discovery.refresh()
                            }
                        }
                    }

                    Label {
                        text: ConfigManager.localIp.length > 0
                              ? ConfigManager.localIp
                              : qsTr("未获取到 IP")
                        color: Style.Color.textMuted
                        font.pixelSize: 12
                    }
                }

                // 刷新按钮
                Button {
                    Layout.alignment: Qt.AlignVCenter
                    icon.name: "view-refresh"
                    icon.width: 20
                    icon.height: 20
                    flat: true
                    ToolTip.text: qsTr("刷新设备列表")
                    ToolTip.visible: hovered
                    onClicked: AppController.discovery.refresh()
                }
            }
        }

        // 分隔线
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Style.Color.borderSoft
        }

        // 在线设备标题
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Style.Space.sm
            Layout.rightMargin: Style.Space.sm

            Label {
                text: qsTr("附近设备")
                font.pixelSize: 13
                font.bold: true
                color: Style.Color.textSecondary
            }

            Item { Layout.fillWidth: true }

            Label {
                text: qsTr("%1 台").arg(listView.count)
                font.pixelSize: 12
                color: Style.Color.textWeak
            }
        }

        // 设备列表
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Style.Space.xs

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

            // 空列表提示
            Label {
                anchors.centerIn: parent
                text: qsTr("正在搜索设备...")
                color: Style.Color.textWeak
                font.pixelSize: 14
                visible: listView.count === 0
            }
        }
    }
}
