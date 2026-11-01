/**
 * @file    AddDeviceDialog.qml
 * @version 8.2.1
 * @date 2026-11-01
 * @author  GY
 * @brief   添加设备对话框
 *
 * 提供三种跨网段添加设备的方式：复制本机邀请码、粘贴对方邀请码导入、
 * 手动输入 IP 和端口。结果通过行内提示反馈，成功后自动关闭。
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

        // 读屏语义：弹窗本体不是 Item，Accessible 附挂不上，由内容根承载
        // Dialog 角色，名字对齐标题
        Accessible.role: Accessible.Dialog
        Accessible.name: addDeviceDialog.title

        // 方式切换页签（内容左右留白 20、底部留白 16，与 DialogHeader 视觉衔接）
        TabBar {
            id: modeBar
            Layout.fillWidth: true
            Layout.leftMargin: Style.Space.xl
            Layout.rightMargin: Style.Space.xl
            currentIndex: addDeviceDialog._mode
            onCurrentIndexChanged: addDeviceDialog._mode = currentIndex

            TabButton { text: qsTr("我的邀请码"); Accessible.name: text }  // 读屏名
            TabButton { text: qsTr("导入邀请码"); Accessible.name: text }
            TabButton { text: qsTr("手动添加"); Accessible.name: text }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Style.Space.xl
            Layout.rightMargin: Style.Space.xl
            Layout.bottomMargin: Style.Space.lg
            currentIndex: addDeviceDialog._mode

            // 方式一：展示本机邀请码供对方导入
            ColumnLayout {
                spacing: Style.Space.sm

                Label {
                    objectName: "addInviteHintText"
                    text: qsTr("把下面的邀请码发给别人（聊天软件均可），对方在 GridYard 里导入后即可互相添加。")
                    font.pixelSize: 13
                    color: Style.Color.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true

                    // 读屏语义：说明文本以静态文本可达
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                // 直接用 TextArea 自带的滚动与换行；包一层 ScrollView 会让宽度失去约束。
                // 内容不超框时垂直居中，超框时回退顶端对齐，长文本不锚死中间
                TextArea {
                    id: inviteArea
                    objectName: "inviteArea"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    readOnly: true
                    text: addDeviceDialog._inviteText
                    wrapMode: TextEdit.Wrap
                    verticalAlignment: contentHeight > height ? Text.AlignTop : Text.AlignVCenter
                    font.pixelSize: 11
                    color: Style.Color.textSecondary
                    selectByMouse: true

                    // 读屏名：QQC2 内建只给 role 不给 name，内容经文本接口仍可逐段读出
                    Accessible.name: qsTr("我的邀请码")

                    background: Rectangle {
                        color: Style.Color.surfaceSoft
                        radius: Style.Radius.sm
                        border.color: Style.Color.border
                    }
                }

                SecondaryButton {
                    id: copyInviteButton
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("复制邀请码")
                    Accessible.name: text
                    onClicked: {
                        addDeviceDialog.copyInvite()
                        copyFeedbackTimer.restart()
                    }

                    // 复制反馈只在点击后短暂出现，不随悬停即时显隐
                    ToolTip.text: qsTr("已复制到剪贴板")
                    ToolTip.visible: copyFeedbackTimer.running

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
                    objectName: "addImportHintText"
                    text: qsTr("粘贴对方发来的邀请码，GridYard 会自动探测并添加该设备。")
                    font.pixelSize: 13
                    color: Style.Color.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true

                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                TextArea {
                    id: importArea
                    objectName: "importArea"
                    Accessible.name: qsTr("对方的邀请码")
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    // 占位提示用自绘 Label：样式自带的 placeholder 不跟随垂直居中
                    placeholderText: ""
                    wrapMode: TextEdit.Wrap
                    verticalAlignment: contentHeight > height ? Text.AlignTop : Text.AlignVCenter
                    font.pixelSize: 12
                    color: Style.Color.textMain
                    selectByMouse: true
                    background: Rectangle {
                        color: Style.Color.window
                        radius: Style.Radius.sm
                        border.color: importArea.activeFocus
                                      ? Style.Color.primary : Style.Color.border

                        // 占位提示：输入为空时显示，与文本同样的垂直居中
                        Label {
                            visible: importArea.text.length === 0
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            text: qsTr("粘贴 gridyard://invite 开头的邀请码")
                            color: Style.Color.textWeak
                            font.pixelSize: 12
                        }
                    }
                }

                Label {
                    objectName: "addImportErrorText"
                    Layout.fillWidth: true
                    visible: addDeviceDialog._importError.length > 0
                    text: addDeviceDialog._importError
                    color: Style.Color.error
                    font.pixelSize: 12
                    wrapMode: Text.Wrap

                    // 读屏语义：行内失败原因以静态文本可达
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                SecondaryButton {
                    Layout.alignment: Qt.AlignRight
                    text: addDeviceDialog._busy ? qsTr("正在连接...") : qsTr("导入并连接")
                    Accessible.name: text
                    enabled: importArea.text.trim().length > 0 && !addDeviceDialog._busy
                    onClicked: {
                        addDeviceDialog._importError = ""
                        AppController.reachabilityController.importInvite(importArea.text.trim())
                    }
                }
            }

            // 方式三：手动输入 IP 和端口
            ColumnLayout {
                spacing: Style.Space.sm

                Label {
                    objectName: "addManualHintText"
                    text: qsTr("已知对方电脑的 IP 和端口时可直接填写，GridYard 会先测试连通再添加。")
                    font.pixelSize: 13
                    color: Style.Color.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true

                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
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
                            objectName: "manualHostField"
                            Layout.fillWidth: true
                            placeholderText: qsTr("例如：10.10.10.100")
                            selectByMouse: true

                            // 读屏名：QQC2 内建只给 role 不给 name（对齐上方可见标签）
                            Accessible.name: qsTr("IP 地址")
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
                            objectName: "manualPortSpin"
                            Layout.fillWidth: true
                            from: 1024
                            to: 65535
                            value: ConfigManager.tcpPort
                            editable: true
                            locale: Qt.locale("C")  // C 区域无千分位，端口不显示逗号分组

                            // 读屏名：数值经 SpinBox 值接口播报，内部输入框免名
                            Accessible.name: qsTr("端口")
                        }
                    }
                }

                Label {
                    objectName: "addManualErrorText"
                    Layout.fillWidth: true
                    visible: addDeviceDialog._manualError.length > 0
                    text: addDeviceDialog._manualError
                    color: Style.Color.error
                    font.pixelSize: 12
                    wrapMode: Text.Wrap

                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                SecondaryButton {
                    Layout.alignment: Qt.AlignRight
                    text: addDeviceDialog._busy ? qsTr("正在连接...") : qsTr("测试并添加")
                    Accessible.name: text
                    enabled: manualHostField.text.trim().length > 0 && !addDeviceDialog._busy
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

        // 邀请添加即好友：目标探测可达并注入设备目录后自动建立好友标记
        //（导入自己的邀请码不会注入，也就不会走到这里）
        function onInvitePeerInjected(deviceId: string): void {
            if (deviceId.length > 0 && deviceId !== ConfigManager.deviceId) {
                AppController.peerDiscoveryViewModel.setDeviceFavorite(deviceId, true)
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
