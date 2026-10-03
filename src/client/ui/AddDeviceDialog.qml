/**
* @file    AddDeviceDialog.qml
* @version 7.15.10
* @date    2026-10-03
* @author  GY
* @brief   添加设备对话框
*
* 提供三种跨网段添加设备的方式：复制本机邀请码、粘贴对方邀请码导入、
* 手动输入 IP 和端口。结果通过行内提示反馈，成功后自动关闭。
*
* Change Log:
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.15.3] GY   2026-10-03
* * 页眉改用 DialogHeader 组件
* [v7.13.2] GY   2026-10-03
* * 修复探测期间关窗吞掉添加结果的问题，成功分支不再依赖弹窗存续
* [v7.11.0] GY   2026-10-02
* * 初始版本，为邀请连接与手动添加设备提供界面入口
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Dialog {
    id: addDeviceDialog

    title: qsTr("添加设备")
    modal: true
    // 弹窗居中并定宽：parent 是 210px 的设备列表列，不能引用 parent.width
    anchors.centerIn: Overlay.overlay
    width: 520
    padding: 0

    // 当前页签：0=我的邀请码 1=导入邀请码 2=手动添加
    property int _mode: 0
    property string _importError: ""
    property string _manualError: ""
    property bool _busy: AppController.reachabilityController.isProbing

    signal deviceAdded()

    onAboutToShow: {
        _mode = 0
        _importError = ""
        _manualError = ""
        _inviteText = AppController.reachabilityController.generateInvite()
    }

    // 邀请码较长，生成一次后缓存展示
    property string _inviteText: ""

    // 将邀请文本写入隐藏 TextEdit 后调用 copy()，借助剪贴板完成复制
    function copyInvite(): void {
        clipboardHelper.text = _inviteText
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.deselect()
    }

    background: Rectangle {
        color: Style.Color.window
        radius: Style.Radius.md
        border.color: Style.Color.border
    }

    header: DialogHeader {
        title: qsTr("添加设备")
        subtitle: qsTr("同一局域网内的设备会自动出现；跨网段时可以用下面任一方式添加。")
        onCloseClicked: addDeviceDialog.close()
    }

    contentItem: ColumnLayout {
        spacing: Style.Space.md

        // 方式切换页签
        TabBar {
            id: modeBar
            Layout.fillWidth: true
            currentIndex: addDeviceDialog._mode
            onCurrentIndexChanged: addDeviceDialog._mode = currentIndex

            TabButton { text: qsTr("我的邀请码") }
            TabButton { text: qsTr("导入邀请码") }
            TabButton { text: qsTr("手动添加") }
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: addDeviceDialog._mode

            // 方式一：展示本机邀请码供对方导入
            ColumnLayout {
                spacing: Style.Space.sm

                Label {
                    text: qsTr("把下面的邀请码发给别人（聊天软件均可），对方在 GridYard 里导入后即可互相添加。")
                    font.pixelSize: 13
                    color: Style.Color.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96

                    TextArea {
                        id: inviteArea
                        readOnly: true
                        text: addDeviceDialog._inviteText
                        wrapMode: TextEdit.WrapAnywhere
                        font.pixelSize: 11
                        color: Style.Color.textSecondary
                        selectByMouse: true
                        background: Rectangle {
                            color: Style.Color.surfaceSoft
                            radius: Style.Radius.sm
                            border.color: Style.Color.border
                        }
                    }
                }

                Button {
                    id: copyInviteButton
                    text: qsTr("复制邀请码")
                    highlighted: true
                    onClicked: {
                        addDeviceDialog.copyInvite()
                        copyFeedbackTimer.restart()
                    }

                    ToolTip.text: qsTr("已复制到剪贴板")
                    ToolTip.visible: copyInviteButton.hovered || copyFeedbackTimer.running

                    Timer {
                        id: copyFeedbackTimer
                        interval: 1500
                    }
                }
            }

            // 方式二：粘贴对方邀请码导入
            ColumnLayout {
                spacing: Style.Space.sm

                Label {
                    text: qsTr("粘贴对方发来的邀请码，GridYard 会自动探测并添加该设备。")
                    font.pixelSize: 13
                    color: Style.Color.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96

                    TextArea {
                        id: importArea
                        placeholderText: qsTr("粘贴 gridyard://invite 开头的邀请码")
                        wrapMode: TextEdit.WrapAnywhere
                        font.pixelSize: 12
                        color: Style.Color.textMain
                        selectByMouse: true
                        background: Rectangle {
                            color: Style.Color.window
                            radius: Style.Radius.sm
                            border.color: importArea.activeFocus
                                          ? Style.Color.primary : Style.Color.border
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: addDeviceDialog._importError.length > 0
                    text: addDeviceDialog._importError
                    color: Style.Color.error
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    spacing: Style.Space.sm

                    Button {
                        text: addDeviceDialog._busy ? qsTr("正在连接...") : qsTr("导入并连接")
                        enabled: importArea.text.trim().length > 0 && !addDeviceDialog._busy
                        highlighted: true
                        onClicked: {
                            addDeviceDialog._importError = ""
                            AppController.reachabilityController.importInvite(importArea.text.trim())
                        }
                    }
                }
            }

            // 方式三：手动输入 IP 和端口
            ColumnLayout {
                spacing: Style.Space.sm

                Label {
                    text: qsTr("已知对方电脑的 IP 和端口时可直接填写，GridYard 会先测试连通再添加。")
                    font.pixelSize: 13
                    color: Style.Color.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: Style.Space.sm

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.Space.xs

                        Label {
                            text: qsTr("IP 地址")
                            font.pixelSize: 12
                            color: Style.Color.textSecondary
                        }

                        TextField {
                            id: manualHostField
                            Layout.fillWidth: true
                            placeholderText: qsTr("例如：10.10.10.100")
                            selectByMouse: true
                        }
                    }

                    ColumnLayout {
                        Layout.preferredWidth: 130
                        spacing: Style.Space.xs

                        Label {
                            text: qsTr("端口")
                            font.pixelSize: 12
                            color: Style.Color.textSecondary
                        }

                        SpinBox {
                            id: manualPortSpin
                            Layout.fillWidth: true
                            from: 1024
                            to: 65535
                            value: ConfigManager.tcpPort
                            editable: true
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: addDeviceDialog._manualError.length > 0
                    text: addDeviceDialog._manualError
                    color: Style.Color.error
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }

                Button {
                    text: addDeviceDialog._busy ? qsTr("正在连接...") : qsTr("测试并添加")
                    enabled: manualHostField.text.trim().length > 0 && !addDeviceDialog._busy
                    highlighted: true
                    onClicked: {
                        addDeviceDialog._manualError = ""
                        AppController.reachabilityController.addManualEndpoint(
                                    manualHostField.text.trim(),
                                    manualPortSpin.value)
                    }
                }
            }
        }
    }

    // 复制邀请码用的隐藏载体：copy() 将选中文本写入系统剪贴板
    TextEdit {
        id: clipboardHelper
        visible: false
        width: 0
        height: 0
    }

    // 导入结果反馈：成功关闭弹窗，失败行内提示
    Connections {
        target: AppController.reachabilityController

        function onInviteImported(success: bool, deviceId: string, errorString: string): void {
            if (success) {
                // 成功结果不受弹窗存续限制：副作用已在 C++ 侧发生，关闭状态下也要通知列表刷新
                addDeviceDialog.close()
                addDeviceDialog.deviceAdded()
            } else if (addDeviceDialog.opened) {
                // 失败提示仅在弹窗仍打开时写行内错误，关窗后无处展示，忽略合理
                addDeviceDialog._importError = errorString.length > 0
                                               ? errorString : qsTr("邀请码无效，请检查后重试")
            }
        }

        function onManualEndpointTestResult(success: bool, errorString: string): void {
            if (success) {
                // 成功结果不受弹窗存续限制：探测期间关窗也不能吞掉添加结果
                addDeviceDialog.close()
                addDeviceDialog.deviceAdded()
            } else if (addDeviceDialog.opened) {
                // 失败提示仅在弹窗仍打开时写行内错误，关窗后无处展示，忽略合理
                addDeviceDialog._manualError = errorString.length > 0
                                               ? errorString : qsTr("无法连接该地址")
            }
        }
    }
}
