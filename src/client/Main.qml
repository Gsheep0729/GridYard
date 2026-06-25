/**
* @file    Main.qml
* @version 6.7.0
* @date    2026-06-28
* @author  GridYard Team
* @brief   GridYard 客户端根窗口
*
* 标题通过 AppController.applicationName/Version 绑定，
* 关窗时由用户选择隐藏到后台或退出程序。
* 左侧显示在线设备列表，右侧显示设备会话页。
*
* Change Log:
* [v6.7.0] GY   2026-06-28
* * 关闭按钮触发时短暂置顶主窗口，确保立即回到桌面最上层
* * 关闭确认弹窗打开后重试恢复并聚焦主窗口
* [v6.6.3] GY   2026-06-28
* * 发送和接收传输后停留在统一会话流，不再切换传输页签
* [v6.6.2] GY   2026-06-25
* * 约束主窗口最小尺寸并限制侧栏设备名宽度，避免整体布局压缩遮挡
* * 创建传输任务后自动显示在当前设备会话中
* [v6.5.0] GY   2026-06-25
* * 关闭窗口时增加隐藏后台/退出程序确认，修复托盘后台无法退出
* * 接入系统托盘、后台运行与非阻塞通知
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
import Qt.labs.platform as Platform
import cqnu.gridyard.client 1.0
import "utils/Style.js" as Style

ApplicationWindow {
    id: mainWindow

    width:   980
    height:  725
    minimumWidth: 860
    minimumHeight: 620
    visible: true
    title:   "%1 v%2".arg(AppController.applicationName)
                     .arg(AppController.applicationVersion)
    color: Style.Color.pageBg

    // 窗口首次显示后记录常规标志位，供临时置顶后恢复使用
    Component.onCompleted: {
        _normalWindowFlags = mainWindow.flags
    }

    onClosing: function(close) {
        if (_allowWindowClose) {
            return  // 已获准退出，不拦截
        }
        close.accepted = false
        mainWindow.bringMainWindowToFront()
        closeChoiceDialog.open()
        closeDialogFocusTimer.restart()
    }

    // 当前选中的目标设备信息
    property string _targetDeviceId: ""
    property string _targetDeviceName: ""
    property string _targetIpAddress: ""
    property bool _targetIsOnline: false
    property bool _allowWindowClose: false  // 标记用户已确认退出，允许窗口关闭
    readonly property int kPopupEnterDuration: 180  // 弹窗淡入动画时长
    // 记录常规窗口标志，临时置顶后恢复
    property int _normalWindowFlags: 0

    // 最小化或隐藏状态下恢复到普通窗口并激活到前台
    function showMainWindow(): void {
        mainWindow.visible = true
        if (mainWindow.visibility === Window.Minimized
                || mainWindow.visibility === Window.Hidden) {
            mainWindow.visibility = Window.Windowed
        }
        mainWindow.show()
        mainWindow.raise()
        mainWindow.requestActivate()
    }

    // 临时添加置顶标志将窗口拉到最前，配合定时器自动取消置顶
    function bringMainWindowToFront(): void {
        mainWindow.flags = _normalWindowFlags | Qt.WindowStaysOnTopHint
        mainWindow.visible = true
        if (mainWindow.visibility === Window.Minimized
                || mainWindow.visibility === Window.Hidden) {
            mainWindow.visibility = Window.Windowed
        }
        mainWindow.show()
        mainWindow.raise()
        mainWindow.requestActivate()
        releaseTopMostTimer.restart()
    }

    // 置顶窗口并聚焦关闭确认弹窗，配合定时器恢复焦点
    function focusCloseChoiceDialog(): void {
        mainWindow.bringMainWindowToFront()
        closeDialogFocusTimer.restart()
    }

    function hideToTray(): void {
        mainWindow.hide()
        if (trayIcon.available) {
            trayIcon.showMessage(qsTr("GridYard"), qsTr("应用仍在后台运行"))
        }
    }

    function requestApplicationQuit(): void {
        _allowWindowClose = true
        AppController.quit()
    }

    // 关闭确认弹窗聚焦定时器：短暂延迟后聚焦弹窗内容，避免窗口切换导致焦点丢失
    Timer {
        id: closeDialogFocusTimer
        interval: 120
        repeat: false

        onTriggered: {
            mainWindow.showMainWindow()
            if (closeChoiceDialog.opened && closeChoiceDialog.contentItem) {
                closeChoiceDialog.contentItem.forceActiveFocus()
            }
        }
    }

    // 置顶释放定时器：短暂置顶后恢复常规窗口标志，避免窗口永远悬浮
    Timer {
        id: releaseTopMostTimer
        interval: 260
        repeat: false

        onTriggered: {
            mainWindow.flags = mainWindow._normalWindowFlags
            mainWindow.showMainWindow()
        }
    }

    Platform.SystemTrayIcon {
        id: trayIcon
        visible: true
        tooltip: qsTr("GridYard")
        icon.source: "qrc:/qt/qml/cqnu/gridyard/client/icons/gridyard.png"
        menu: Platform.Menu {
            Platform.MenuItem {
                text: qsTr("显示主窗口")
                onTriggered: mainWindow.showMainWindow()
            }
            Platform.MenuItem {
                text: qsTr("隐藏到托盘")
                onTriggered: mainWindow.hideToTray()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("退出")
                onTriggered: mainWindow.requestApplicationQuit()
            }
        }
        onActivated: function(reason) {
            if (reason === Platform.SystemTrayIcon.Trigger
                    || reason === Platform.SystemTrayIcon.DoubleClick) {
                mainWindow.showMainWindow()
            }
        }
    }

    function selectDevice(deviceId: string, deviceName: string,
                          ipAddress: string, isOnline: bool): void {
        // 更新右侧会话页绑定的目标设备信息
        _targetDeviceId = deviceId
        _targetDeviceName = deviceName
        _targetIpAddress = ipAddress
        _targetIsOnline = isOnline
    }

    // 设备列表刷新后重新同步选中设备的在线状态和 IP
    function refreshSelectedDevice(): void {
        if (_targetDeviceId.length === 0) return
        const peers = AppController.peerDiscoveryViewModel.peers
        for (let i = 0; i < peers.length; i++) {
            if (peers[i].deviceId === _targetDeviceId) {
                selectDevice(peers[i].deviceId, peers[i].deviceName,
                             peers[i].ipAddress, peers[i].isOnline)
                return
            }
        }
        _targetIsOnline = false
    }

    Dialog {
        id: closeChoiceDialog
        title: qsTr("关闭 GridYard")
        modal: true
        anchors.centerIn: parent
        width: Math.min(420, parent ? parent.width - 48 : 420)
        padding: 20

        onOpened: mainWindow.focusCloseChoiceDialog()

        ColumnLayout {
            spacing: 14
            anchors.fill: parent

            Label {
                text: trayIcon.available
                      ? qsTr("要将 GridYard 隐藏到后台继续接收消息和传输，还是直接退出程序？")
                      : qsTr("当前系统托盘不可用，是否退出 GridYard？")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        footer: RowLayout {
            spacing: 10
            anchors.margins: 16

            Button {
                visible: trayIcon.available
                text: qsTr("隐藏到后台")
                onClicked: {
                    closeChoiceDialog.close()
                    mainWindow.hideToTray()
                }
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("退出程序")
                onClicked: {
                    closeChoiceDialog.close()
                    mainWindow.requestApplicationQuit()
                }
            }
            Button {
                text: qsTr("取消")
                onClicked: closeChoiceDialog.close()
            }
        }
    }

    // 文件选择弹窗：支持多选，选中后为每个文件创建发送会话
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
                AppController.transferController.createSendSession(mainWindow._targetDeviceId, path)
            }
        }
    }

    // 文件夹选择弹窗：选中后去掉 file:// 前缀再创建发送会话
    FolderDialog {
        id: folderDialog
        title: qsTr("选择要发送的文件夹")
        onAccepted: {
            let path = selectedFolder.toString()
            if (path.startsWith("file://")) path = path.substring(7)
            AppController.transferController.createSendSession(mainWindow._targetDeviceId, path)
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
                        onClicked: AppController.peerDiscoveryViewModel.refresh()
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

            HoverHandler {
                onHoveredChanged: avatarBtn._hovered = hovered
            }

            TapHandler {
                onTapped: deviceInfoPopup.open()
            }
        }

        // 设备名
        Label {
            anchors.top: avatarBtn.bottom
            anchors.topMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 10
            text: ConfigManager.deviceName
            font.pixelSize: 10
            color: Style.Color.textSecondary
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
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

            HoverHandler {
                onHoveredChanged: {
                    menuBtn._hovered = hovered
                    if (!hovered) {
                        menuBtn._pressed = false
                    }
                }
            }

            TapHandler {
                onPressedChanged: menuBtn._pressed = pressed
                onTapped: menuPopup.open()
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
        // 拖拽文件到设备列表项时，先选中该设备再创建发送会话
        onFileDropped: function(deviceId, filePath) {
            console.log("拖拽文件到设备:", deviceId, filePath)
            const peers = AppController.peerDiscoveryViewModel.peers
            for (let i = 0; i < peers.length; i++) {
                if (peers[i].deviceId === deviceId) {
                    mainWindow.selectDevice(peers[i].deviceId, peers[i].deviceName,
                                               peers[i].ipAddress, peers[i].isOnline)
                    break
                }
            }
            AppController.transferController.createSendSession(deviceId, filePath)
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
            id: sessionView

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
                AppController.transferController.createSendSession(mainWindow._targetDeviceId, filePath)
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
        target: AppController.transferController
        function onReceiveRequestReceived(sessionId, senderDeviceId, senderName, fileName,
                                          fileSize, totalFiles, totalBytes,
                                          isDirectory, fileList) {
            const peers = AppController.peerDiscoveryViewModel.peers
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
            trayIcon.showMessage(qsTr("传输请求"),
                                 qsTr("%1 想发送 %2 个文件").arg(senderName).arg(totalFiles))
        }
        function onTransferCompleted(sessionId: string, fileName: string, filePath: string): void {
            completeDialog._fileName = fileName
            completeDialog._filePath = filePath
            completeDialog.open()
            trayIcon.showMessage(qsTr("传输完成"), qsTr("已完成一项文件传输"))
        }
        function onErrorOccurred(message: string): void { errorLabel.text = message; errorPopup.open() }
        function onMessageOccurred(message: string): void { successLabel.text = message; successPopup.open() }
    }

    Connections {
        target: AppController.peerDiscoveryViewModel
        function onPeersChanged(): void { mainWindow.refreshSelectedDevice() }
    }

    Connections {
        target: AppController.chatController
        function onIncomingMessageReceived(deviceId: string, senderName: string, preview: string): void {
            trayIcon.showMessage(senderName, preview)
        }
    }

    Connections {
        target: AppController
        function onLocalHistoryOperationFailed(): void {
            errorLabel.text = qsTr("本地保存失败，历史可能缺失")
            errorPopup.open()
            trayIcon.showMessage(qsTr("本地历史"), errorLabel.text)
        }
    }

    Popup {
        id: errorPopup
        anchors.centerIn: parent
        width: 300
        height: errorLabel.implicitHeight + 48
        modal: false
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
        modal: false
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
