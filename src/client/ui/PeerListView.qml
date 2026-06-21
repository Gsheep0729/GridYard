/**
 * @file    PeerListView.qml
 * @version 4.16.0
 * @date    2026-06-18
 * @author  GridYard Team
 * @brief   在线设备列表组件
 *
 * 绑定 AppController.discovery.peers 显示发现的其他设备。
 * 支持手动刷新。
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
    Rectangle{
        color:Style.Color.select
        ColumnLayout {
            anchors.fill: parent
            // 搜索区域
            Rectangle{
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                color:Style.Color.select

                TextField {
                    height: 30
                    width: 150
                    text: qsTr("搜索")
                    font.pixelSize: 13
                    color: black
                    anchors.bottom: parent.bottom
                }

                Item { Layout.fillWidth: true }

                Label {
                    height: 30
                    width: 30
                    text: qsTr("%1 台").arg(listView.count)
                    font.pixelSize: 12
                    color: Style.Color.select
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

                Label {
                    anchors.centerIn: parent
                    text: qsTr("正在搜索设备...")
                    color: Style.Color.textWeak
                    font.pixelSize: 14
                    visible: listView.count === 0
                }
            }
        }}

}