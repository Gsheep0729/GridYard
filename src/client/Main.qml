/**
* @file    Main.qml
* @version 4.15.2
* @date    2026-06-17
* @author  GridYard Team
* @brief   GridYard 客户端根窗口
*
* 标题通过 AppController.applicationName/Version 绑定，
* 关窗触发 AppController.quit()。
* 左侧显示在线设备列表，右侧显示设备会话页。
*
* Change Log:
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

ApplicationWindow {
    id: mainWindow

    width:   960
    height:  640
    visible: true
    title:   "%1 v%2".arg(AppController.applicationName)
                     .arg(AppController.applicationVersion)

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

    // 工具栏
    header: ToolBar {
        background: Rectangle {
            color: "#FFFFFF"
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: "#E5E7EB"
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Label {
                text: mainWindow.title
                font.pixelSize: 15
                font.bold: true
                color: "#111827"
                Layout.leftMargin: 20
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                id: settingsButton
                text: qsTr("设置")
                font.pixelSize: 13
                font.bold: true
                onClicked: settingsDialog.open()
                Layout.rightMargin: 12

                contentItem: Label {
                    text: settingsButton.text
                    font: settingsButton.font
                    // 文本颜色平滑过渡
                    color: settingsButton.down ? "#2563EB" : (settingsButton.hovered ? "#3B82F6" : "#4B5563")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter

                    Behavior on color {
                        ColorAnimation { duration: 200; easing.type: Easing.OutCubic }
                    }
                }

                background: Rectangle {
                    implicitWidth: 64
                    implicitHeight: 32
                    // 使用同色透明值作为起点，避免悬停背景过渡跳变
                    color: settingsButton.down ? "#E5E7EB" : (settingsButton.hovered ? "#F3F4F6" : "#00F3F4F6")
                    radius: 6

                    Behavior on color {
                        ColorAnimation { duration: 200; easing.type: Easing.OutCubic }
                    }
                }
            }
        }
    }

    // 设置对话框
    SettingsDialog {
        id: settingsDialog
    }

    // 左右分栏布局
    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // 左侧：设备列表
        PeerListView {
            id: peerListView
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            selectedDeviceId: mainWindow._targetDeviceId

            background: Rectangle {
                color: "#FFFFFF"
                radius: 12
                border.color: "#E5E7EB"
                border.width: 1
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

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: mainWindow._targetDeviceId.length > 0 ? 1 : 0

            Rectangle {
                color: "#F9FAFB"
                radius: 12
                border.color: "#E5E7EB"
                border.width: 1

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 16

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: "GridYard"
                        color: "#9CA3AF"
                        font.pixelSize: 48
                        font.bold: true
                        opacity: 0.15
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("选择一台在线设备开始传输")
                        font.pixelSize: 20
                        font.bold: true
                        color: "#374151"
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("点击左侧设备进入会话，或直接拖放文件到设备卡片")
                        color: "#6B7280"
                        font.pixelSize: 14
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
            color: "#F44336"
            radius: 8
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

        // 3 秒后自动关闭
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
            color: "#4CAF50"
            radius: 8
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

        // 3 秒后自动关闭
        Timer {
            interval: 3000
            running: successPopup.visible
            onTriggered: successPopup.close()
        }
    }
}
