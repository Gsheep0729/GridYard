/**
 * @file    SettingsDialog.qml
 * @date    2026-06-02
 * @author  GY
 * @brief   设置对话框
 *
 * 编辑设备名、选择接收路径、修改 TCP 端口。
 * 保存时调用 ConfigManager 的 setter 方法。
 *
 * Change Log:
 * [v0.1] GY   2026-06-02
 * * Stage 2：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import cqnu.gridyard.client 1.0

Dialog {
    id: tw_settingsDialog

    title: qsTr("设置")
    modal: true
    anchors.centerIn: parent
    width: 480
    height: 360

    // 临时存储编辑中的值
    property string _tempDeviceName:  ConfigManager.deviceName
    property string _tempReceivePath: ConfigManager.receivePath
    property int    _tempTcpPort:     ConfigManager.tcpPort

    // 打开对话框时重置临时值
    onAboutToShow: {
        _tempDeviceName  = ConfigManager.deviceName
        _tempReceivePath = ConfigManager.receivePath
        _tempTcpPort     = ConfigManager.tcpPort
    }

    contentItem: ColumnLayout {
        spacing: 16

        // 设备名
        GroupBox {
            title: qsTr("设备信息")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label { text: qsTr("设备名称：") }
                TextField {
                    id: deviceNameField
                    Layout.fillWidth: true
                    text: tw_settingsDialog._tempDeviceName
                    placeholderText: qsTr("输入设备名称")
                    onTextChanged: tw_settingsDialog._tempDeviceName = text
                }
            }
        }

        // 接收路径
        GroupBox {
            title: qsTr("文件接收")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label { text: qsTr("接收路径：") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: receivePathField
                        Layout.fillWidth: true
                        text: tw_settingsDialog._tempReceivePath
                        readOnly: true
                    }
                    Button {
                        text: qsTr("浏览...")
                        onClicked: folderDialog.open()
                    }
                }
            }
        }

        // TCP 端口
        GroupBox {
            title: qsTr("网络设置")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label { text: qsTr("TCP 端口：") }
                SpinBox {
                    id: tcpPortSpinBox
                    from: 1024
                    to: 65535
                    value: tw_settingsDialog._tempTcpPort
                    onValueModified: tw_settingsDialog._tempTcpPort = value
                }
            }
        }

        // 占位，推动按钮到底部
        Item { Layout.fillHeight: true }
    }

    // 底部按钮
    standardButtons: Dialog.Ok | Dialog.Cancel

    onAccepted: {
        // 保存配置
        ConfigManager.deviceName  = tw_settingsDialog._tempDeviceName
        ConfigManager.receivePath = tw_settingsDialog._tempReceivePath
        ConfigManager.tcpPort     = tw_settingsDialog._tempTcpPort
    }

    // 文件夹选择对话框
    FolderDialog {
        id: folderDialog
        title: qsTr("选择接收路径")
        currentFolder: "file://" + tw_settingsDialog._tempReceivePath
        onAccepted: {
            // 提取路径字符串
            let path = selectedFolder.toString()
            if (path.startsWith("file://")) {
                path = path.substring(7)
            }
            tw_settingsDialog._tempReceivePath = path
        }
    }
}
