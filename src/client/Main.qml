/**
* @file    Main.qml
* @version 4.16.3
* @date    2026-06-24
* @author  GridYard Team
* @brief   GridYard 客户端根窗口
*
* 标题通过 AppController.applicationName/Version 绑定，
* 关窗触发 AppController.quit()。
* 左侧显示在线设备列表，右侧显示设备会话页。
*
* Change Log:
* [v4.16.3] FengChunlin   2026-06-24
* * 调整主窗口为三栏会话布局，增加本机信息和菜单入口
* [v4.16.2] DuRuoxian   2026-06-22
* * 添加窗口图标设置，解决任务栏图标缺失问题
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
    height:  725
    visible: true
    title:   "%1 v%2".arg(AppController.applicationName)
                     .arg(AppController.applicationVersion)
    color: Style.Color.pageBg

    onClosing: AppController.quit()

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
        if (_targetDeviceId.length === 0) return
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

    //对话框

    FileDialog {
        id: fileDialog
        title: qsTr("选择要发送的文件")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("所有文件 (*)")]
        onAccepted: {
            let urls = fileDialog.selectedFiles
            for (let i = 0; i < urls.length; i++) {
                let path = urls[i].toString()
                if (path.startsWith("file://")) path = path.substring(7)
                AppController.transfer.createSendSession(mainWindow._targetDeviceId, path)
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("选择要发送的文件夹")
        onAccepted: {
            let path = selectedFolder.toString()
            if (path.startsWith("file://")) path = path.substring(7)
            AppController.transfer.createSendSession(mainWindow._targetDeviceId, path)
        }
    }

    SettingsDialog { id: settingsDialog }

    // ======== 弹出窗口 ========

    // 本机信息弹出窗口(FCL)
    Popup {
        id: deviceInfoPopup
        x: 70; y: 10
        width: 282; height: 110
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        //弹出的个人主机信息框（FCL）
        background: Rectangle {
            color: Style.Color.window
            border.color: Style.Color.borderSoft
            border.width: 1
            radius: 12
        }
        //个人信息框，头像 + 信息列表
        contentItem: ColumnLayout {
            anchors.fill: parent
   //         anchors.margins: 5
            spacing: Style.Space.md
            RowLayout{
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                spacing: 25
                //左侧头像
                Rectangle{
                    Layout.alignment: Qt.AlignCenter
                    Layout.preferredHeight: 56
                    Layout.preferredWidth: 56
                    radius: 4
                    color: Style.Color.primary
                    Label{
                        anchors.centerIn: parent
                        text: "我"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
                //中间信息列
                    ColumnLayout {
                        Layout.alignment: Qt.AlignVertical
                        Layout.fillWidth: true
                        spacing:2
                        Label {
                            Layout.fillWidth: true
                            text: ConfigManager.deviceName
                            color: Style.Color.text
                            font.pixelSize: 14
                            font.bold: true
                            elide: Text.ElideRight  // 文字太长时显示省略号
                        }

                        Label {
                            Layout.alignment: Qt.AlignTop
                            text: ConfigManager.localIp.length > 0
                                  ? ConfigManager.localIp : qsTr("未获取到 IP")
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
                        Layout.alignment: Qt.AlignTop
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
                    Behavior on color { ColorAnimation { duration: Style.Motion.base } }
                }
                onClicked: { menuPopup.close(); settingsDialog.open() }
            }
        }
    }

    // ======== 三栏主体(主体) ========

    // 左侧工具栏（62px）
    Rectangle {
        id: sidebar
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 62
        color: Style.Color.surfaceLeft

        // 本机头像
        Rectangle {
            id: avatarBtn
            anchors.top: parent.top
            anchors.topMargin: 16
            anchors.horizontalCenter: parent.horizontalCenter
            width: 36; height: 36
            color: Style.Color.primary

            property bool _hovered: false

            Label {
                anchors.centerIn: parent
                text: "我"
                color: "#FFFFFF"
                font.pixelSize: 12
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: avatarBtn._hovered = true
                onExited: avatarBtn._hovered = false
                onClicked: deviceInfoPopup.open()
            }
        }

        // 设备名
        Label {
            anchors.top: avatarBtn.bottom
            anchors.topMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            text: ConfigManager.deviceName
            font.pixelSize: 10
            color: Style.Color.textSecondary
            elide: Text.ElideRight
        }

        // 菜单按钮（底部）
        Rectangle {
            id: menuBtn
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            anchors.horizontalCenter: parent.horizontalCenter
            width: 41; height: 41
            radius: 3

            property bool _hovered: false
            property bool _pressed: false

            color: _pressed
                   ? Style.Color.menubarClicked
                   : (_hovered ? Style.Color.menubarSelect: Style.Color.surfaceLeft)

            Behavior on color { ColorAnimation { duration: Style.Motion.base } }

            // 三横线
            Item {
                anchors.centerIn: parent
                width: 16; height: 13

                Rectangle {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 16; height: 2
                    radius: 1
                    color: Style.Color.menubar
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 16; height: 2
                    radius: 1
                    color: Style.Color.menubar
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 16; height: 2
                    radius: 1
                    color: Style.Color.menubar
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: menuBtn._hovered = true
                onExited: { menuBtn._hovered = false; menuBtn._pressed = false }
                onPressed: menuBtn._pressed = true
                onReleased: menuBtn._pressed = false
                onClicked: menuPopup.open()
            }
        }
    }

    // 中间：设备列表
    PeerListView {
        id: peerList
        anchors.left: sidebar.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 210
        selectedDeviceId: mainWindow._targetDeviceId

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

    // 右侧：会话页（填充剩余宽度）
    Rectangle {
        anchors.left: peerList.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: Style.Color.surfaceLeft

        // 未选中设备时的占位
        Item {
            anchors.fill: parent
            visible: mainWindow._targetDeviceId.length === 0

            Column {
                anchors.centerIn: parent
                spacing: Style.Space.lg
                width: Math.min(parent.width - 80, 420)

                Label {
                    text: "GridYard"
                    color: Style.Color.primary
                    font.pixelSize: 48
                    font.bold: true
                    opacity: 0.16
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Label {
                    text: qsTr("选择一台设备开始会话")
                    font.pixelSize: 20
                    font.bold: true
                    color: Style.Color.textMain
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Label {
                    text: qsTr("发送文件，之后也会在这里查看聊天消息")
                    color: Style.Color.textMuted
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    width: parent.width
                }
            }
        }

        // 设备会话页
        DeviceSessionView {
            anchors.fill: parent
            visible: mainWindow._targetDeviceId.length > 0
            deviceId: mainWindow._targetDeviceId
            deviceName: mainWindow._targetDeviceName
            ipAddress: mainWindow._targetIpAddress
            isOnline: mainWindow._targetIsOnline

            background: Rectangle { color: "transparent" }

            onSendFileRequested: fileDialog.open()
            onSendFolderRequested: folderDialog.open()
            onFileDropped: function(filePath) {
                AppController.transfer.createSendSession(mainWindow._targetDeviceId, filePath)
            }
        }
    }

    // ======== 弹窗（不变） ========

    AcceptDialog { id: acceptDialog }

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

        onAccepted: ConfigManager.openFolder(completeDialog._filePath)
        onRejected: completeDialog.close()
    }

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
        function onErrorOccurred(message) { errorLabel.text = message; errorPopup.open() }
        function onMessageOccurred(message) { successLabel.text = message; successPopup.open() }
    }

    Connections {
        target: AppController.discovery
        function onPeersChanged() { mainWindow.refreshSelectedDevice() }
    }

    Popup {
        id: errorPopup
        anchors.centerIn: parent
        width: 300
        height: errorLabel.implicitHeight + 48
        modal: true
        closePolicy: Popup.CloseOnPressOutside

        enter: Transition {
            NumberAnimation {
                property: "opacity"; from: 0; to: 1
                duration: mainWindow.kPopupEnterDuration
                easing.type: Easing.OutCubic
            }
        }

        background: Rectangle { color: Style.Color.error; radius: Style.Radius.sm }

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

    Popup {
        id: successPopup
        anchors.centerIn: parent
        width: 300
        height: successLabel.implicitHeight + 48
        modal: true
        closePolicy: Popup.CloseOnPressOutside

        enter: Transition {
            NumberAnimation {
                property: "opacity"; from: 0; to: 1
                duration: mainWindow.kPopupEnterDuration
                easing.type: Easing.OutCubic
            }
        }

        background: Rectangle { color: Style.Color.success; radius: Style.Radius.sm }

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
