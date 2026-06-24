/**
 * @file    SettingsDialog.qml
 * @version 6.6.2
 * @date    2026-06-17
 * @author  GridYard Team
 * @brief   设置对话框
 *
 * 编辑设备名、选择接收路径、修改 TCP 端口。
 * 保存时调用 ConfigManager 的 setter 方法。
 *
 * Change Log:
 * [v6.6.2] GY   2026-06-25
 * * 同步文件头版本与当前主版本
 * [v6.5.0] GY   2026-06-25
 * * 显示本地历史不可用状态
 * [v4.16.0] DuRuoxian   2026-06-18
 * * 使用 Style.js 统一样式常量
 * [v4.15.2] DuRuoxian   2026-06-17
 * * 优化设置弹窗页眉、分组卡片和底部保存状态
 * [v4.12.0] DuRuoxian   2026-06-14
 * * 增加设置状态和对话框进入过渡
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
import "../utils/Style.js" as Style

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
    property int    _tempRetentionDays: ConfigManager.retentionDays

    readonly property bool _isValid: _tempDeviceName.trim().length > 0
                                    && _tempReceivePath.length > 0
                                    && _tempTcpPort >= 1024
                                    && _tempTcpPort <= 65535
    readonly property bool _isDirty: _tempDeviceName.trim() !== ConfigManager.deviceName
                                    || _tempReceivePath !== ConfigManager.receivePath
                                    || _tempAutoAcceptFiles !== ConfigManager.autoAcceptFiles
                                    || _tempTcpPort !== ConfigManager.tcpPort
                                    || _tempRetentionDays !== ConfigManager.retentionDays
    readonly property int kColorDuration: Style.Motion.base
    readonly property int kEnterDuration: 200

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: settingsDialog.kEnterDuration
            easing.type: Easing.OutCubic
        }
    }

    // 打开对话框时重置临时值
    onAboutToShow: {
        _tempDeviceName  = ConfigManager.deviceName
        _tempReceivePath = ConfigManager.receivePath
        _tempAutoAcceptFiles = ConfigManager.autoAcceptFiles
        _tempTcpPort     = ConfigManager.tcpPort
        _tempRetentionDays = ConfigManager.retentionDays
    }

    background: Rectangle {
        color: Style.Color.pageBg
        radius: Style.Radius.md
        border.color: Style.Color.border
    }

    header: Rectangle {
        implicitHeight: 100
        color: Style.Color.surface
        radius: Style.Radius.md

        RowLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: Style.Space.lg

            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: 28
                color: Style.Color.primary

                Label {
                    anchors.centerIn: parent
                    text: settingsDialog._tempDeviceName.trim().length > 0
                          ? settingsDialog._tempDeviceName.trim().charAt(0).toUpperCase()
                          : "?"
                    color: Style.Color.surface
                    font.pixelSize: 24
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.Space.xs

                Label {
                    text: qsTr("偏好设置")
                    font.pixelSize: 20
                    font.bold: true
                    color: Style.Color.textMain
                }

                Label {
                    text: qsTr("%1 · %2").arg(ConfigManager.localIp).arg(ConfigManager.deviceId)
                    color: Style.Color.textMuted
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Style.Color.border
        }
    }

    contentItem: ScrollView {
        id: scrollView
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: Style.Space.lg
            Layout.margins: Style.Space.lg

            Item { Layout.preferredHeight: Style.Space.sm }

            // 设备信息卡片
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: deviceSection.implicitHeight + 32
                color: Style.Color.surface
                radius: Style.Radius.lg
                border.color: Style.Color.border
                border.width: 1

                ColumnLayout {
                    id: deviceSection
                    anchors.fill: parent
                    anchors.margins: Style.Space.lg
                    spacing: Style.Space.md

                    Label {
                        text: qsTr("设备身份")
                        font.pixelSize: 15
                        font.bold: true
                        color: Style.Color.textMain
                    }

                    Label {
                        text: qsTr("设置一个易于识别的名称，以便在局域网中发现。")
                        color: Style.Color.textMuted
                        font.pixelSize: 13
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
                        color: Style.Color.error
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
            }

            // 文件接收卡片
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: receiveSection.implicitHeight + 32
                color: Style.Color.surface
                radius: Style.Radius.lg
                border.color: Style.Color.border
                border.width: 1

                ColumnLayout {
                    id: receiveSection
                    anchors.fill: parent
                    anchors.margins: Style.Space.lg
                    spacing: Style.Space.md

                    Label {
                        text: qsTr("存储与接收")
                        font.pixelSize: 15
                        font.bold: true
                        color: Style.Color.textMain
                    }

                    Label {
                        text: qsTr("配置文件保存路径及自动化接收行为。")
                        color: Style.Color.textMuted
                        font.pixelSize: 13
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Style.Space.sm

                        TextField {
                            id: receivePathField
                            Layout.fillWidth: true
                            text: settingsDialog._tempReceivePath
                            readOnly: true
                            selectByMouse: true
                            font.pixelSize: 13
                        }

                        Button {
                            text: qsTr("更改目录")
                            onClicked: folderDialog.open()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Style.Color.borderSoft
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Style.Space.lg

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Style.Space.xs

                            Label {
                                text: qsTr("自动接受文件")
                                font.bold: true
                                font.pixelSize: 14
                                color: Style.Color.textSecondary
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("跳过确认弹窗，直接保存文件。")
                                color: Style.Color.textWeak
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

            // 网络设置卡片
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: historySection.implicitHeight + 32
                color: Style.Color.surface
                radius: Style.Radius.lg
                border.color: Style.Color.border
                border.width: 1

                RowLayout {
                    id: historySection
                    anchors.fill: parent
                    anchors.margins: Style.Space.lg
                    spacing: Style.Space.lg

                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("历史保留期限")
                            font.pixelSize: 15
                            font.bold: true
                            color: Style.Color.textMain
                        }
                        Label {
                            text: qsTr("到期后自动删除本机聊天和传输历史，不影响文件。")
                            color: Style.Color.textMuted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }

                    ComboBox {
                        id: retentionSelector
                        model: [qsTr("永久保留"), qsTr("7 天"), qsTr("30 天"), qsTr("90 天")]
                        currentIndex: [0, 7, 30, 90].indexOf(settingsDialog._tempRetentionDays)
                        onActivated: settingsDialog._tempRetentionDays = [0, 7, 30, 90][currentIndex]
                    }
                }
            }

            // 网络设置卡片
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: networkSection.implicitHeight + 32
                color: Style.Color.surface
                radius: Style.Radius.lg
                border.color: Style.Color.border
                border.width: 1

                RowLayout {
                    id: networkSection
                    anchors.fill: parent
                    anchors.margins: Style.Space.lg
                    spacing: 24

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.Space.xs

                        Label {
                            text: qsTr("传输服务")
                            font.pixelSize: 15
                            font.bold: true
                            color: Style.Color.textMain
                        }

                        Label {
                            text: qsTr("TCP 端口用于局域网设备间的数据通信。")
                            color: Style.Color.textMuted
                            font.pixelSize: 13
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

            Item { Layout.preferredHeight: Style.Space.sm }

            Label {
                Layout.fillWidth: true
                visible: !AppController.localHistoryAvailable
                text: qsTr("本地历史不可用，聊天和传输仍可正常使用。")
                color: Style.Color.warning
                wrapMode: Text.Wrap
                font.pixelSize: 13
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 72
        color: Style.Color.surface
        radius: Style.Radius.md

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Style.Color.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: Style.Space.xl
            spacing: Style.Space.md

            Label {
                Layout.fillWidth: true
                text: settingsDialog._isDirty
                      ? qsTr("修改尚未应用")
                      : qsTr("设置已是最新")
                color: settingsDialog._isDirty ? Style.Color.warning : Style.Color.textWeak
                font.pixelSize: 13
                font.bold: settingsDialog._isDirty

                Behavior on color {
                    ColorAnimation {
                        duration: settingsDialog.kColorDuration
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Button {
                text: qsTr("取消")
                onClicked: settingsDialog.reject()
                flat: true
            }

            Button {
                text: qsTr("保存更改")
                enabled: settingsDialog._isDirty && settingsDialog._isValid
                highlighted: true
                onClicked: {
                    ConfigManager.deviceName = settingsDialog._tempDeviceName.trim()
                    ConfigManager.receivePath = settingsDialog._tempReceivePath
                    ConfigManager.autoAcceptFiles = settingsDialog._tempAutoAcceptFiles
                    ConfigManager.tcpPort = settingsDialog._tempTcpPort
                    AppController.historyController.setRetentionDays(settingsDialog._tempRetentionDays)
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
