/**
 * @file    PeerListView.qml
 * @date    2026-06-02
 * @author  GY
 * @brief   在线设备列表组件
 *
 * 绑定 AppController.discovery.peers，使用 DeviceCard 作为 delegate
 * 渲染设备列表。支持设备选择信号。
 *
 * Change Log:
 * [v0.1] GY   2026-06-02
 * * Stage 2：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0

Frame {
    id: tw_peerListView

    signal deviceSelected(string deviceId)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 标题栏
        Label {
            text: qsTr("在线设备")
            font.pixelSize: 16
            font.bold: true
            Layout.fillWidth: true
            Layout.margins: 12
        }

        // 设备列表
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4

            // 绑定 AppController 的设备发现服务
            model: AppController.discovery.peers

            delegate: DeviceCard {
                width: listView.width
                onCardClicked: function(deviceId) {
                    tw_peerListView.deviceSelected(deviceId)
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
