/**
 * @file    PeerListView.qml
 * @version 4.10.0
 * @date    2026-06-13
 * @author  GridYard Team
 * @brief   在线设备列表组件
 *
 * 顶部显示本机设备名（可编辑）和 IP 地址，下方绑定
 * AppController.discovery.peers 显示发现的其他设备。
 * 支持手动刷新和设备名即时修改。
 *
 * Change Log:
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

Frame {
    id: peerListView

    property string selectedDeviceId: ""

    signal deviceSelected(string deviceId, string deviceName, string ipAddress, bool isOnline)
    signal fileDropped(string deviceId, string filePath)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 本机信息区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            color: "#F8F9FA"
            radius: 8

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                // 本机标识
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width: 36
                    height: 36
                    radius: 18
                    color: "#4A90D9"

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
                            color: nameField.activeFocus ? "#FFFFFF" : "transparent"
                            border.color: nameField.activeFocus ? "#4A90D9" : "transparent"
                            border.width: 1
                            radius: 4
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
                        color: "#666666"
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
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            height: 1
            color: "#E0E0E0"
        }

        // 在线设备标题
        Label {
            text: qsTr("在线设备")
            font.pixelSize: 14
            font.bold: true
            Layout.fillWidth: true
            Layout.margins: 8
        }

        // 设备列表
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4

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
                color: "#999999"
                font.pixelSize: 14
                visible: listView.count === 0
            }
        }
    }
}
