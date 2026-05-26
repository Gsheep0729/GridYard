/**
 * @file    Main.qml
 * @date    2026-05-24
 * @author  GY
 * @brief   integration_demo 根窗口
 *
 * 演示三种 C++ 类型在 QML 端的标准消费方式：
 *   - AppController：通过 import 后直接以单例名访问
 *   - DiscoveryStub：通过类型名实例化为子对象
 *   - PeerInfo：通过 Q_GADGET property 直接在 binding 中访问
 *
 * Change Log:
 * [v1.0] GY   2026-05-24
 * * Initial creation
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import cqnu.gridyard.demo.core 1.0

ApplicationWindow {
    id: tw_mainWindow

    readonly property int kSpacing: 12

    width:   480
    height:  640
    visible: true
    title:   AppController.applicationName + " v" + AppController.applicationVersion

    DiscoveryStub {
        id: discovery
    }

    Connections {
        target: AppController
        function onAppReady() {
            console.log("AppController appReady received in QML")
        }
    }

    onClosing: AppController.quit()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: tw_mainWindow.kSpacing
        spacing: tw_mainWindow.kSpacing

        Label {
            text: qsTr("在线设备 (%1)").arg(discovery.peers.length)
            font.pixelSize: 18
            font.bold: true
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ListView {
                id: tw_peerList
                model: discovery.peers
                spacing: 6

                // DeviceCard.qml 内部已用 required property 声明了 4 个字段，
                // ListView 会按名字自动从 model 项（PeerInfo Q_GADGET 的
                // Q_PROPERTY）中绑定。此处无需再声明 required。
                delegate: DeviceCard {
                    width: ListView.view.width

                    onCardClicked: function(id) {
                        console.log("cardClicked:", id)
                        discovery.markOffline(id)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: tw_mainWindow.kSpacing

            Button {
                Layout.fillWidth: true
                text: qsTr("添加伪造设备")
                onClicked: {
                    const peer = makeFakePeer()
                    discovery.addPeer(peer.name, peer.ip)
                }
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("清空全部")
                onClicked: discovery.removeAllPeers()
            }
        }
    }

    function makeFakePeer(): var {
        const names = ["高扬的工作站", "杜若贤的笔记本", "冯春霖的台式机", "实验室服务器"]
        const idx   = Math.floor(Math.random() * names.length)
        const ip    = "192.168.1." + (10 + Math.floor(Math.random() * 240))
        return { name: names[idx], ip: ip }
    }
}
