/**
 * @file    SettingsDialog.qml
 * @version 7.15.16
 * @date    2026-10-04
 * @author  GridYard Team
 * @brief   设置对话框
 *
 * 编辑设备名、选择接收路径、修改 TCP 端口、配置协调服务器。
 * 保存时调用 ConfigManager 的 setter 方法。
 *
 * Change Log:
* [v7.15.16] GY   2026-10-04
* * 历史库重建后在存储区块显示备份位置提示
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
 * [v7.15.13] GY   2026-10-04
 * * 版本头对齐到 v7.15.13
 * [v7.15.12] GY   2026-10-03
 * * 接收路径输入框放开只读，支持直接粘贴或手输后保存
 * [v7.15.11] GY   2026-10-03
 * * 版本头对齐到 v7.15.11
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
* [v7.15.3] GY   2026-10-03
* * 清除缓存按钮改用 DangerButton 组件
* [v7.14.0] GY   2026-10-03
* * 协调服务器区块新增访问令牌输入框
 * [v7.13.0] GY   2026-10-02
 * * 六张分组卡片改用 SettingsCard 外壳组件
 * [v7.11.0] GY   2026-10-02
 * * 标注端口与协调服务器修改需重启生效，清除缓存改为危险色按钮
 * [v7.4.0] GY   2026-07-21
 * * 新增协调服务器配置区块（地址、端口、Relay 策略）
 * [v6.8.1] GY   2026-06-28
 * * 补充 localPathFromUrl 函数行内注释
 * [v6.7.0] GY   2026-06-28
 * * 增加清除缓存入口，删除配置、历史数据库和日志后退出
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
import "../utils/FormatUtils.js" as FormatUtils
import "../utils/Style.js" as Style

Dialog {
    id: settingsDialog

    title: qsTr("设置")
    modal: true
    anchors.centerIn: parent
    width: Math.min(680, parent ? parent.width - 32 : 680)
    height: Math.min(620, parent ? parent.height - 32 : 620)
    padding: 0

    // 临时存储编辑中的值：打开对话框时从 ConfigManager 复制，保存时写回
    property string _tempDeviceName:  ConfigManager.deviceName
    property string _tempReceivePath: ConfigManager.receivePath
    property bool   _tempAutoAcceptFiles: ConfigManager.autoAcceptFiles
    property int    _tempTcpPort:     ConfigManager.tcpPort
    property int    _tempRetentionDays: ConfigManager.retentionDays
    property bool   _tempRendezvousEnabled: ConfigManager.rendezvousEnabled
    property string _tempRendezvousHost: ConfigManager.rendezvousHost
    property int    _tempRendezvousPort: ConfigManager.rendezvousPort
    property string _tempRendezvousToken: ConfigManager.rendezvousToken
    property int    _tempRelayMode: ConfigManager.relayMode

    // 表单校验：设备名非空、接收路径非空、端口在合法范围内
    readonly property bool _isValid: _tempDeviceName.trim().length > 0
                                    && _tempReceivePath.length > 0
                                    && FormatUtils.isValidPort(_tempTcpPort)
                                    && FormatUtils.isValidPort(_tempRendezvousPort)
    // 脏标记：任一字段与当前配置不同则视为已修改
    readonly property bool _isDirty: _tempDeviceName.trim() !== ConfigManager.deviceName
                                    || _tempReceivePath !== ConfigManager.receivePath
                                    || _tempAutoAcceptFiles !== ConfigManager.autoAcceptFiles
                                    || _tempTcpPort !== ConfigManager.tcpPort
                                    || _tempRetentionDays !== ConfigManager.retentionDays
                                    || _tempRendezvousEnabled !== ConfigManager.rendezvousEnabled
                                    || _tempRendezvousHost !== ConfigManager.rendezvousHost
                                    || _tempRendezvousPort !== ConfigManager.rendezvousPort
                                    || _tempRendezvousToken !== ConfigManager.rendezvousToken
                                    || _tempRelayMode !== ConfigManager.relayMode
    readonly property int kColorDuration: Style.Motion.base
    readonly property int kEnterDuration: 200  // 弹窗入场动画时长

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: settingsDialog.kEnterDuration
            easing.type: Easing.OutCubic
        }
    }

    // 打开对话框时重置临时值为当前配置，确保取消操作不会残留上次编辑状态
    onAboutToShow: {
        _tempDeviceName  = ConfigManager.deviceName
        _tempReceivePath = ConfigManager.receivePath
        _tempAutoAcceptFiles = ConfigManager.autoAcceptFiles
        _tempTcpPort     = ConfigManager.tcpPort
        _tempRetentionDays = ConfigManager.retentionDays
        _tempRendezvousEnabled = ConfigManager.rendezvousEnabled
        _tempRendezvousHost = ConfigManager.rendezvousHost
        _tempRendezvousPort = ConfigManager.rendezvousPort
        _tempRendezvousToken = ConfigManager.rendezvousToken
        _tempRelayMode = ConfigManager.relayMode
        // 手输过路径的 TextEdit 会失去 text 绑定，重开时显式回填
        receivePathField.text = _tempReceivePath
    }

    // 将 FolderDialog 返回的 URL 转成本地路径，统一走 FormatUtils 避免多处实现漂移
    function localPathFromUrl(fileUrl: url): string {
        return FormatUtils.localPathFromUrl(fileUrl)
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
        contentWidth: availableWidth  // 内容宽度跟随可用宽度，避免水平滚动

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: Style.Space.lg
            Layout.margins: Style.Space.lg

            Item { Layout.preferredHeight: Style.Space.sm }  // 顶部留白

            // 设备信息卡片：设备名编辑、校验提示
            SettingsCard {
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

            // 文件接收卡片：接收路径选择、自动接受开关
            SettingsCard {
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
                        placeholderText: qsTr("输入或粘贴接收路径")
                        selectByMouse: true
                        font.pixelSize: 13
                        // 支持直接粘贴或手输路径；编辑实时同步临时值，与更改目录按钮等价
                        onTextEdited: settingsDialog._tempReceivePath = text
                        onEditingFinished: {
                            text = text.trim()
                            settingsDialog._tempReceivePath = text
                        }
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

            // 历史保留期限卡片
            SettingsCard {
                RowLayout {
                    Layout.fillWidth: true
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

            // 传输服务卡片
            SettingsCard {
                RowLayout {
                    Layout.fillWidth: true
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
                            text: qsTr("TCP 端口用于局域网设备间的数据通信；修改端口后需重启应用生效。")
                            color: Style.Color.textMuted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }

                    SpinBox {
                        id: tcpPortSpinBox
                        from: 1024
                        to: 65535  // SpinBox 须写数值边界，表单校验语义统一走 FormatUtils.isValidPort
                        value: settingsDialog._tempTcpPort
                        editable: true
                        onValueModified: settingsDialog._tempTcpPort = value
                    }
                }
            }

            // 协调服务器卡片
            SettingsCard {
                RowLayout {
                    spacing: Style.Space.md
                    Layout.fillWidth: true

                    ColumnLayout {
                        spacing: Style.Space.xs
                        Layout.fillWidth: true

                        Label {
                            text: qsTr("协调服务器")
                            font.pixelSize: 15
                            font.bold: true
                            color: Style.Color.textMain
                        }

                        Label {
                            text: qsTr("校园网或 VPN 环境下，通过协调服务器发现跨 AP 的设备；修改后需重启应用生效。")
                            color: Style.Color.textMuted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }

                    Switch {
                        checked: settingsDialog._tempRendezvousEnabled
                        onToggled: settingsDialog._tempRendezvousEnabled = checked
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Style.Color.borderSoft
                    visible: settingsDialog._tempRendezvousEnabled
                }

                GridLayout {
                    columns: 3
                    columnSpacing: Style.Space.md
                    rowSpacing: Style.Space.sm
                    visible: settingsDialog._tempRendezvousEnabled

                    Label {
                        text: qsTr("服务器地址")
                        font.pixelSize: 13
                        color: Style.Color.textSecondary
                    }

                    TextField {
                        id: rendezvousHostField
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        text: settingsDialog._tempRendezvousHost
                        placeholderText: qsTr("例如：10.10.10.100")
                        selectByMouse: true
                        onTextChanged: settingsDialog._tempRendezvousHost = text
                    }

                    Label {
                        text: qsTr("服务器端口")
                        font.pixelSize: 13
                        color: Style.Color.textSecondary
                    }

                    SpinBox {
                        id: rendezvousPortSpinBox
                        from: 1024
                        to: 65535
                        value: settingsDialog._tempRendezvousPort
                        editable: true
                        onValueModified: settingsDialog._tempRendezvousPort = value
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: qsTr("访问令牌")
                        font.pixelSize: 13
                        color: Style.Color.textSecondary
                    }

                    TextField {
                        id: rendezvousTokenField
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        text: settingsDialog._tempRendezvousToken
                        placeholderText: qsTr("与服务器 --token 一致，留空表示未启用")
                        selectByMouse: true
                        onTextChanged: settingsDialog._tempRendezvousToken = text
                    }

                    Label {
                        text: qsTr("Relay 策略")
                        font.pixelSize: 13
                        color: Style.Color.textSecondary
                    }

                    ComboBox {
                        id: relayModeComboBox
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        model: [
                            { label: qsTr("询问后中继（默认）"), value: 0 },
                            { label: qsTr("自动中继"), value: 1 },
                            { label: qsTr("从不中继"), value: 2 }
                        ]
                        textRole: "label"
                        currentIndex: settingsDialog._tempRelayMode
                        onActivated: settingsDialog._tempRelayMode = model[currentIndex].value
                    }
                }

                Label {
                    text: qsTr("提示：自动中继会直接通过服务器转发流量，速度可能受限。")
                    font.pixelSize: 12
                    color: Style.Color.warning
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    visible: settingsDialog._tempRendezvousEnabled && settingsDialog._tempRelayMode !== 2
                }
            }

            // 清除缓存卡片
            SettingsCard {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.Space.lg

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.Space.xs

                        Label {
                            text: qsTr("清除缓存")
                            font.pixelSize: 15
                            font.bold: true
                            color: Style.Color.textMain
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("删除配置文件、本地历史数据库和运行日志。应用会退出，下次启动重新生成设备身份。")
                            color: Style.Color.textMuted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                    }

                    // 危险操作用语义化红色按钮，与删除确认弹窗的确认按钮一致
                    DangerButton {
                        id: clearCacheButton
                        text: qsTr("清除缓存")
                        onClicked: clearCacheDialog.open()
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

            Label {
                Layout.fillWidth: true
                visible: AppController.historyDatabaseRebuilt
                text: qsTr("上次启动时本地历史库损坏，已自动重建；旧库备份于 %1")
                      .arg(AppController.rebuiltBackupPath)
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

        // 顶部分隔线
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

            // 修改状态提示：脏标记为 true 时显示"修改尚未应用"
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

            // 保存按钮：仅在脏标记且校验通过时可用
            Button {
                text: qsTr("保存更改")
                enabled: settingsDialog._isDirty && settingsDialog._isValid
                highlighted: true
                onClicked: {
                    // 批量写回 ConfigManager，触发各属性的 NOTIFY 信号
                    ConfigManager.deviceName = settingsDialog._tempDeviceName.trim()
                    ConfigManager.receivePath = settingsDialog._tempReceivePath
                    ConfigManager.autoAcceptFiles = settingsDialog._tempAutoAcceptFiles
                    ConfigManager.tcpPort = settingsDialog._tempTcpPort
                    AppController.historyController.setRetentionDays(settingsDialog._tempRetentionDays)
                    ConfigManager.rendezvousEnabled = settingsDialog._tempRendezvousEnabled
                    ConfigManager.rendezvousHost = settingsDialog._tempRendezvousHost
                    ConfigManager.rendezvousPort = settingsDialog._tempRendezvousPort
                    ConfigManager.rendezvousToken = settingsDialog._tempRendezvousToken
                    ConfigManager.relayMode = settingsDialog._tempRelayMode
                    settingsDialog.accept()
                }
            }
        }
    }

    // 文件夹选择对话框：用户选择新接收路径后更新临时变量并回填输入框
    FolderDialog {
        id: folderDialog
        title: qsTr("选择接收路径")
        currentFolder: "file://" + settingsDialog._tempReceivePath
        onAccepted: {
            settingsDialog._tempReceivePath = settingsDialog.localPathFromUrl(selectedFolder)
            receivePathField.text = settingsDialog._tempReceivePath
        }
    }

    Dialog {
        id: clearCacheDialog
        title: qsTr("清除缓存")
        modal: true
        anchors.centerIn: parent
        width: 420
        padding: Style.Space.lg

        contentItem: Label {
            text: qsTr("将删除配置文件、聊天和传输历史数据库、运行日志。此操作不可撤销，应用会立即退出。")
            color: Style.Color.textMain
            wrapMode: Text.Wrap
            font.pixelSize: 14
        }

        footer: DialogButtonBox {
            Button {
                text: qsTr("取消")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: qsTr("清除并退出")
                highlighted: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }

        onAccepted: {
            settingsDialog.close()
            AppController.clearLocalCache()
        }
    }
}
