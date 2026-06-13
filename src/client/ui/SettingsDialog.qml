/**
 * @file    SettingsDialog.qml
 * @version 4.11.0
 * @date    2026-06-13
 * @author  GridYard Team
 * @brief   设置对话框
 *
 * 编辑设备名、选择接收路径、修改 TCP 端口。
 * 保存时调用 ConfigManager 的 setter 方法。
 *
 * Change Log:
 * [v4.11.0] GY   2026-06-13
 * * 新增自动接收并保存文件设置
 * [v4.9.0] DuRuoxian   2026-06-13
 * * 重构为卡片式设置页，增加本机摘要、输入校验和修改状态提示
 * [v0.2.0] DuRuoxian   2026-06-02
 * * Stage 2：初始版本
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import cqnu.gridyard.client 1.0

Dialog {
    id: settingsDialog

    title: qsTr("设置")
    modal: true
    anchors.centerIn: parent
    width: Math.min(680, parent ? parent.width - 32 : 680)
    height: Math.min(620, parent ? parent.height - 32 : 620)
    padding: 0

    // 临时存储编辑中的值
    property string _tempDeviceName:  ConfigManager.deviceName
    property string _tempReceivePath: ConfigManager.receivePath
    property bool   _tempAutoAcceptFiles: ConfigManager.autoAcceptFiles
    property int    _tempTcpPort:     ConfigManager.tcpPort

    readonly property bool _isValid: _tempDeviceName.trim().length > 0
                                    && _tempReceivePath.length > 0
                                    && _tempTcpPort >= 1024
                                    && _tempTcpPort <= 65535
    readonly property bool _isDirty: _tempDeviceName.trim() !== ConfigManager.deviceName
                                    || _tempReceivePath !== ConfigManager.receivePath
                                    || _tempAutoAcceptFiles !== ConfigManager.autoAcceptFiles
                                    || _tempTcpPort !== ConfigManager.tcpPort

    // 打开对话框时重置临时值
    onAboutToShow: {
        _tempDeviceName  = ConfigManager.deviceName
        _tempReceivePath = ConfigManager.receivePath
        _tempAutoAcceptFiles = ConfigManager.autoAcceptFiles
        _tempTcpPort     = ConfigManager.tcpPort
    }

    background: Rectangle {
        color: "#F5F7FA"
        radius: 10
        border.color: "#D8DEE6"
    }

    header: Rectangle {
        implicitHeight: 96
        color: "#FFFFFF"
        radius: 10

        RowLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 52
                Layout.preferredHeight: 52
                radius: 26
                color: "#4A90D9"

                Label {
                    anchors.centerIn: parent
                    text: settingsDialog._tempDeviceName.trim().length > 0
                          ? settingsDialog._tempDeviceName.trim().charAt(0)
                          : "?"
                    color: "#FFFFFF"
                    font.pixelSize: 22
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Label {
                    text: qsTr("本机设置")
                    font.pixelSize: 20
                    font.bold: true
                }

                Label {
                    text: qsTr("%1 · %2").arg(ConfigManager.localIp).arg(ConfigManager.deviceId)
                    color: "#6B7280"
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }
    }

    contentItem: ScrollView {
        id: scrollView
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: 12

            Item {
                Layout.preferredHeight: 4
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                implicitHeight: deviceSection.implicitHeight + 32
                color: "#FFFFFF"
                radius: 8
                border.color: "#E4E8EE"

                ColumnLayout {
                    id: deviceSection
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8

                    Label {
                        text: qsTr("设备信息")
                        font.pixelSize: 15
                        font.bold: true
                    }

                    Label {
                        text: qsTr("其他设备会通过这个名称识别你，修改后会实时同步。")
                        color: "#6B7280"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    TextField {
                        id: deviceNameField
                        Layout.fillWidth: true
                        text: settingsDialog._tempDeviceName
                        placeholderText: qsTr("输入设备名称")
                        selectByMouse: true
                        onTextChanged: settingsDialog._tempDeviceName = text
                    }

                    Label {
                        visible: settingsDialog._tempDeviceName.trim().length === 0
                        text: qsTr("设备名称不能为空")
                        color: "#C62828"
                        font.pixelSize: 11
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                implicitHeight: receiveSection.implicitHeight + 32
                color: "#FFFFFF"
                radius: 8
                border.color: "#E4E8EE"

                ColumnLayout {
                    id: receiveSection
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    Label {
                        text: qsTr("文件接收")
                        font.pixelSize: 15
                        font.bold: true
                    }

                    Label {
                        text: qsTr("接收完成的文件会保存到以下目录。")
                        color: "#6B7280"
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: receivePathField
                            Layout.fillWidth: true
                            text: settingsDialog._tempReceivePath
                            readOnly: true
                            selectByMouse: true
                        }

                        Button {
                            text: qsTr("选择目录")
                            onClicked: folderDialog.open()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Label {
                                text: qsTr("自动接收并保存")
                                font.bold: true
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("开启后跳过接收确认，文件将直接保存到上述目录。")
                                color: "#6B7280"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }

                        Switch {
                            checked: settingsDialog._tempAutoAcceptFiles
                            onToggled: settingsDialog._tempAutoAcceptFiles = checked
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 4
                implicitHeight: networkSection.implicitHeight + 32
                color: "#FFFFFF"
                radius: 8
                border.color: "#E4E8EE"

                RowLayout {
                    id: networkSection
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5

                        Label {
                            text: qsTr("网络连接")
                            font.pixelSize: 15
                            font.bold: true
                        }

                        Label {
                            text: qsTr("TCP 端口用于局域网设备建立文件传输连接。")
                            color: "#6B7280"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }

                    SpinBox {
                        id: tcpPortSpinBox
                        from: 1024
                        to: 65535
                        value: settingsDialog._tempTcpPort
                        editable: true
                        onValueModified: settingsDialog._tempTcpPort = value
                    }
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 68
        color: "#FFFFFF"
        radius: 10

        RowLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: settingsDialog._isDirty
                      ? qsTr("有尚未保存的修改")
                      : qsTr("所有设置均已保存")
                color: settingsDialog._isDirty ? "#B26A00" : "#6B7280"
                font.pixelSize: 12
            }

            Button {
                text: qsTr("取消")
                onClicked: settingsDialog.reject()
            }

            Button {
                text: qsTr("保存设置")
                enabled: settingsDialog._isDirty && settingsDialog._isValid
                highlighted: true
                onClicked: {
                    ConfigManager.deviceName = settingsDialog._tempDeviceName.trim()
                    ConfigManager.receivePath = settingsDialog._tempReceivePath
                    ConfigManager.autoAcceptFiles = settingsDialog._tempAutoAcceptFiles
                    ConfigManager.tcpPort = settingsDialog._tempTcpPort
                    settingsDialog.accept()
                }
            }
        }
    }

    // 文件夹选择对话框
    FolderDialog {
        id: folderDialog
        title: qsTr("选择接收路径")
        currentFolder: "file://" + settingsDialog._tempReceivePath
        onAccepted: {
            // 提取路径字符串
            let path = selectedFolder.toString()
            if (path.startsWith("file://")) {
                path = path.substring(7)
            }
            settingsDialog._tempReceivePath = path
        }
    }
}
