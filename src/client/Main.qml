/**
* @file    Main.qml
* @version 4.16.0
* @date    2026-06-18
* @author  GridYard Team
* @brief   GridYard 客户端根窗口
*
* 标题通过 AppController.applicationName/Version 绑定，
* 关窗触发 AppController.quit()。
* 左侧工具栏，中间设备列表，右侧设备会话页。
*
* Change Log:
* [v4.16.0] DuRuoxian   2026-06-18
* * 统一主窗口样式常量，调整为现代设备会话工作台
* [v4.15.2] DuRuoxian   2026-06-17
* * 优化主窗口工具栏、设备列表容器和未选中设备占位状态
* [v4.13.2] DuRuoxian   2026-06-15
* * 接收确认弹窗接入文件夹标记和根目录预览
* [v4.12.0] DuRuoxian   2026-06-14
* * 增加成功和错误提示进入过渡
* [v4.10.0] DuRuoxian   2026-06-13
* * 传输完成弹窗改为自定义按钮
* [v4.9.0] DuRuoxian   2026-06-13
* * 点击设备切换会话页，增加文件与文件夹发送入口
* [v4.3.4] DuRuoxian   2026-06-04
* * Stage 4.3：更新接收请求信号处理，支持多文件信息
* [v0.2.0] DuRuoxian   2026-06-02
* * Stage 2：嵌入设备列表，实现左右分栏布局
* [v0.1.0] DuRuoxian   2026-05-24
* * Stage 0：空白窗口框架
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import cqnu.gridyard.client 1.0
import "utils/Style.js" as Style

ApplicationWindow {
    id: mainWindow

    width:   980
    height:  709
    visible: true
    title:   "%1 v%2".arg(AppController.applicationName)
                     .arg(AppController.applicationVersion)
    color: Style.Color.pageBg

    onClosing: AppController.quit()

    // 目标设备 ID（点击设备卡片时设置）
    property string _targetDeviceId: ""
    property string _targetDeviceName: ""
    property string _targetIpAddress: ""
    property bool _targetIsOnline: false
    readonly property int kPopupEnterDuration: 180

    function selectDevice(deviceId: string, deviceName: string,
                          ipAddress: string, isOnline: bool): void {
        _targetDeviceId = deviceId
        _targetDeviceName = deviceName
        _targetIpAddress = ipAddress
        _targetIsOnline = isOnline
    }

    function refreshSelectedDevice(): void {
        if (_targetDeviceId.length === 0) {
            return
        }
        const peers = AppController.discovery.peers
        for (let i = 0; i < peers.length; i++) {
            if (peers[i].deviceId === _targetDeviceId) {
                selectDevice(peers[i].deviceId, peers[i].deviceName,
                             peers[i].ipAddress, peers[i].isOnline)
                return
            }
        }
        _targetIsOnline = false
    }

    // 文件选择对话框由 Qt 平台主题接入系统原生实现
    FileDialog {
        id: fileDialog
        title: qsTr("选择要发送的文件")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("所有文件 (*)")]

        onAccepted: {
            let urls = fileDialog.selectedFiles
            for (let i = 0; i < urls.length; i++) {
                let path = urls[i].toString()
                if (path.startsWith("file://")) {
                    path = path.substring(7)
                }
                AppController.transfer.createSendSession(mainWindow._targetDeviceId, path)
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("选择要发送的文件夹")

        onAccepted: {
            let path = selectedFolder.toString()
            if (path.startsWith("file://")) {
                path = path.substring(7)
            }
            AppController.transfer.createSendSession(mainWindow._targetDeviceId, path)
        }
    }

    // 设置对话框
    SettingsDialog {
        id: settingsDialog
    }

    // 本机信息弹出窗口
    Popup {
        id: deviceInfoPopup
        x: 70
        y: 10
        width: 260
        height: 120
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Style.Color.surface
            radius: Style.Radius.lg
            border.color: Style.Color.borderSoft
            border.width: 1
        }

        contentItem: RowLayout {
            anchors.fill: parent
            anchors.margins: Style.Space.md
            spacing: Style.Space.md

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                radius: 20
                color: Style.Color.primary

                Label {
                    anchors.centerIn: parent
                    text: "我"
                    color: "#FFFFFF"
                    font.pixelSize: 14
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                TextField {
                    Layout.fillWidth: true
                    text: ConfigManager.deviceName
                    placeholderText: qsTr("输入设备名称")
                    font.pixelSize: 14
                    font.bold: true
                    background: Rectangle {
                        color: activeFocus ? Style.Color.surfaceSoft : Style.Color.transparent
                        border.color: activeFocus ? Style.Color.primary : Style.Color.transparent
                        border.width: 1
                        radius: Style.Radius.xs
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

                    Connections {
                        target: ConfigManager
                        function onDeviceNameChanged() {
                            if (!activeFocus) {
                                text = ConfigManager.deviceName
                            }
                            AppController.discovery.refresh()
                        }
                    }
                }

                Label {
                    text: ConfigManager.localIp.length > 0
                          ? ConfigManager.localIp
                          : qsTr("未获取到 IP")
                    color: Style.Color.textMuted
                    font.pixelSize: 12
                }

                Label {
                    text: "%1 v%2".arg(AppController.applicationName)
                                   .arg(AppController.applicationVersion)
                    color: Style.Color.textWeak
                    font.pixelSize: 11
                }
            }

            Button {
                Layout.alignment: Qt.AlignVCenter
                icon.name: "view-refresh"
                icon.width: 20
                icon.height: 20
                flat: true
                ToolTip.text: qsTr("刷新")
                ToolTip.visible: hovered
                onClicked: AppController.discovery.refresh()
            }
        }
    }

    // 菜单弹出窗口
    Popup {
        id: menuPopup
        x: 70
        y: mainWindow.height - height - 10
        width: 160
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Style.Color.surface
            radius: Style.Radius.md
            border.color: Style.Color.borderSoft
            border.width: 1
        }

        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 2

            ItemDelegate {
                id: settingsItem
                Layout.fillWidth: true
                text: qsTr("设置")

                contentItem: Label {
                    text: settingsItem.text
                    font.pixelSize: 13
                    color: settingsItem.hovered ? Style.Color.primary : Style.Color.textMain
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Style.Space.sm
                }

                background: Rectangle {
                    color: settingsItem.hovered ? Style.Color.surfaceSoft : Style.Color.transparent
                    radius: Style.Radius.sm
                    Behavior on color {
                        ColorAnimation { duration: Style.Motion.base }
                    }
                }

                onClicked: {
                    menuPopup.close()
                    settingsDialog.open()
                }
            }
        }
    }

    // 三栏布局
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // 左侧：工具栏
        ToolBar {
            Layout.preferredWidth: 62
            Layout.fillHeight: true

            background: Rectangle {
                color: Style.Color.surfaceLeft
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: Style.Space.md

                // 本机头像
                Rectangle {
                    id: avatarButton
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Style.Space.lg
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    color: Style.Color.primary
                    opacity: hovered ? 0.85 : 1.0

                    Label {
                        anchors.centerIn: parent
                        text: "我"
                        color: "#FFFFFF"
                        font.pixelSize: 12
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: deviceInfoPopup.open()
                    }

                    Behavior on opacity {
                        NumberAnimation { duration: Style.Motion.base }
                    }
                }
                Item { Layout.fillHeight: true }

                // 菜单按钮（底部）
                Button {
                    id: menuButton
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: Style.Space.lg
                    flat: true

                    // 手动跟踪是否按下
                    property bool _pressed: false

                    contentItem: ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 3
                        Repeater {
                            model: 3
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                width: 16
                                height: 2
                                radius: 1
                                color: (menuButton._pressed || menuButton.down)
                                       ? Style.Color.menubarClicked
                                       : (menuButton.hovered
                                           ? Style.Color.menubarSelect
                                           : Style.Color.textSecondary)

                                Behavior on color {
                                    ColorAnimation { duration: Style.Motion.base }
                                }
                            }
                        }
                    }

                    background: Rectangle {
                        implicitWidth: 41
                        implicitHeight: 41
                        color: (menuButton._pressed || menuButton.down)
                               ? Style.Color.select
                               : (menuButton.hovered
                                   ? Style.Color.surfaceSoft
                                   : Style.Color.transparent)

                        Behavior on color {
                            ColorAnimation { duration: Style.Motion.base }
                        }
                    }

                    onPressedChanged: menuButton._pressed = pressed
                    onClicked: menuPopup.open()
                }
            }
        }

        // 中间：设备列表
        PeerListView {
            id: peerListView
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            selectedDeviceId: mainWindow._targetDeviceId

            background: Rectangle {
                color: Style.Color.surfaceMid
            }

            onDeviceSelected: function(deviceId, deviceName, ipAddress, isOnline) {
                console.log("选中设备:", deviceId)
                mainWindow.selectDevice(deviceId, deviceName, ipAddress, isOnline)
            }
            onFileDropped: function(deviceId, filePath) {
                console.log("拖拽文件到设备:", deviceId, filePath)
                const peers = AppController.discovery.peers
                for (let i = 0; i < peers.length; i++) {
                    if (peers[i].deviceId === deviceId) {
                        mainWindow.selectDevice(peers[i].deviceId, peers[i].deviceName,
                                                   peers[i].ipAddress, peers[i].isOnline)
                        break
                    }
                }
                AppController.transfer.createSendSession(deviceId, filePath)
            }
        }

        // 右侧：会话页
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Style.Color.surfaceLeft

            StackLayout {
                anchors.fill: parent
                currentIndex: mainWindow._targetDeviceId.length > 0 ? 1 : 0

                Rectangle {
                    color: "transparent"
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 80, 420)

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: "GridYard"
                            color: Style.Color.primary
                            font.pixelSize: 48
                            font.bold: true
                            opacity: 0.16
                        }

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("选择一台设备开始会话")
                            font.pixelSize: 20
                            font.bold: true
                            color: Style.Color.textMain
                        }

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("发送文件，之后也会在这里查看聊天消息")
                            color: Style.Color.textMuted
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                            Layout.fillWidth: true
                        }
                    }
                }

                DeviceSessionView {
                    deviceId: mainWindow._targetDeviceId
                    deviceName: mainWindow._targetDeviceName
                    ipAddress: mainWindow._targetIpAddress
                    isOnline: mainWindow._targetIsOnline

                    background: Rectangle {
                        color: "transparent"
                    }

                    onSendFileRequested: fileDialog.open()
                    onSendFolderRequested: folderDialog.open()
                    onFileDropped: function(filePath) {
                        AppController.transfer.createSendSession(mainWindow._targetDeviceId, filePath)
                    }
                }
            }
        }
    }

    // 接收确认弹窗
    AcceptDialog {
        id: acceptDialog
    }

    // 传输完成提示弹窗
    Dialog {
        id: completeDialog
        title: qsTr("接收完成")
        modal: true
        anchors.centerIn: parent
        width: 360

        property string _filePath: ""
        property string _fileName: ""

        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: qsTr("文件 \"%1\" 已接收完成").arg(completeDialog._fileName)
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        footer: DialogButtonBox {
            Button {
                text: qsTr("确定")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: qsTr("打开文件所在位置")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }

        onAccepted: {
            ConfigManager.openFolder(completeDialog._filePath)
        }

        onRejected: {
            completeDialog.close()
        }
    }

    // 连接 TransferSessionManager 信号
    Connections {
        target: AppController.transfer
        function onReceiveRequestReceived(sessionId, senderDeviceId, senderName, fileName,
                                          fileSize, totalFiles, totalBytes,
                                          isDirectory, fileList) {
            const peers = AppController.discovery.peers
            for (let i = 0; i < peers.length; i++) {
                if (peers[i].deviceId === senderDeviceId) {
                    mainWindow.selectDevice(peers[i].deviceId, peers[i].deviceName,
                                               peers[i].ipAddress, peers[i].isOnline)
                    break
                }
            }
            acceptDialog.sessionId = sessionId
            acceptDialog.senderName = senderName
            acceptDialog.fileName = fileName
            acceptDialog.fileSize = fileSize
            acceptDialog.totalFiles = totalFiles
            acceptDialog.totalBytes = totalBytes
            acceptDialog.isDirectory = isDirectory
            acceptDialog.fileList = fileList
            acceptDialog.open()
        }
        function onTransferCompleted(sessionId, fileName, filePath) {
            completeDialog._fileName = fileName
            completeDialog._filePath = filePath
            completeDialog.open()
        }
        function onErrorOccurred(message) {
            errorLabel.text = message
            errorPopup.open()
        }
        function onMessageOccurred(message) {
            successLabel.text = message
            successPopup.open()
        }
    }

    Connections {
        target: AppController.discovery
        function onPeersChanged() {
            mainWindow.refreshSelectedDevice()
        }
    }

    // 错误提示弹窗
    Popup {
        id: errorPopup
        anchors.centerIn: parent
        width: 300
        height: errorLabel.implicitHeight + 48
        modal: true
        closePolicy: Popup.CloseOnPressOutside

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: mainWindow.kPopupEnterDuration
                easing.type: Easing.OutCubic
            }
        }

        background: Rectangle {
            color: Style.Color.error
            radius: Style.Radius.sm
        }

        contentItem: Label {
            id: errorLabel
            color: "#FFFFFF"
            font.pixelSize: 14
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            padding: 12
        }

        Timer {
            interval: 3000
            running: errorPopup.visible
            onTriggered: errorPopup.close()
        }
    }

    // 成功提示弹窗
    Popup {
        id: successPopup
        anchors.centerIn: parent
        width: 300
        height: successLabel.implicitHeight + 48
        modal: true
        closePolicy: Popup.CloseOnPressOutside

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: mainWindow.kPopupEnterDuration
                easing.type: Easing.OutCubic
            }
        }

        background: Rectangle {
            color: Style.Color.success
            radius: Style.Radius.sm
        }

        contentItem: Label {
            id: successLabel
            color: "#FFFFFF"
            font.pixelSize: 14
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            padding: 12
        }

        Timer {
            interval: 3000
            running: successPopup.visible
            onTriggered: successPopup.close()
        }
    }
}