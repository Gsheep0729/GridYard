/**
 * @file    Main.qml
 * @version 7.15.4
 * @date    2026-10-03
 * @author  GridYard Team
 * @brief   GridYard 客户端根窗口
 *
 * 标题通过 AppController.applicationName/Version 绑定，
 * 关窗时由用户选择隐藏到后台或退出程序。
 * 左侧显示在线设备列表，右侧显示设备会话页。
 * 拖拽发送统一在本文件解码和裁决，弹窗与提示分层反馈。
 *
 * Change Log:
 * [v7.15.4] GY   2026-10-03
 * * 底部轻提示拆出 ui/Toast.qml，暴露 show(message, isError) 接口
 * [v7.15.1] GY   2026-10-03
 * * 拖拽裁决下沉 C++，删除 QML 双数据源在线检查；接收弹窗过期改信号驱动
 * [v7.15.0] GY   2026-10-03
 * * relay 确认弹窗改接 relayConfirmRequested，策略判断下沉 C++ 后此处纯弹窗
 * [v7.13.1] GY   2026-10-02
 * * Toast 连续提示时重置自动关闭计时，修复第二条被旧计时截断
 * [v7.13.0] GY   2026-10-02
 * * 本机信息弹窗设备名改用 textMain，修复启动时 undefined 到 QColor 的告警
 * [v7.11.0] GY   2026-10-02
 * * 统一拖拽裁决与路径解码，完成通知改为非阻塞卡片，提示移到底部
 * [v7.9.0] GY   2026-07-26
 * * 接管 Relay 降级决策：自动中继直接重试，询问策略弹窗确认
 * [v6.8.1] GY   2026-06-28
 * * 补充 localPathFromUrl 和文件选择弹窗的行内注释
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
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.platform as Platform
import cqnu.gridyard.client 1.0
import "utils/FormatUtils.js" as FormatUtils
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

    // 统一 Material 控件主题色：默认粉紫与应用蓝色主色冲突，
    // 在根窗口设置后向全部子控件传播
    Material.theme: Material.Light
    Material.primary: Style.Color.primary
    Material.accent: Style.Color.primary

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

    // 当前选中设备：选择状态由视图模型持有，设备列表刷新时信息自动重算
    property string _selId: AppController.peerDiscoveryViewModel.selectedDeviceId
    property var _selInfo: {
        const vm = AppController.peerDiscoveryViewModel
        void vm.peers  // 引用设备列表建立依赖，在线状态或 IP 变化时重算
        return vm.deviceById(_selId)
    }
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

    // 底部 Toast 包装：错误停留更久，成功短暂反馈
    function showToast(message: string, isError: bool): void {
        toastPopup.show(message, isError)
    }

    function showErrorToast(message: string): void {
        mainWindow.showToast(message, true)
    }

    function showSuccessToast(message: string): void {
        mainWindow.showToast(message, false)
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

    // 选中设备：设备名、IP 与在线状态由视图模型按选中 ID 自动解析
    function selectDevice(deviceId: string): void {
        AppController.peerDiscoveryViewModel.selectedDeviceId = deviceId
    }

    // 将 FileDialog/FolderDialog 返回的 URL 转成本地路径，统一走 FormatUtils
    function localPathFromUrl(fileUrl: url): string {
        return FormatUtils.localPathFromUrl(fileUrl)
    }

    // 拖拽发送：只做 URL 解码与非文件项过滤，在线裁决由 C++ createSendSession 兜底
    // （离线设备查询不到端点，errorOccurred 已接 Toast 反馈）
    // DeviceCard / DeviceSessionView 只负责冒泡，不再各自处理路径和在线状态
    function handleDroppedFiles(deviceId: string, urls: var): void {
        const paths = []
        for (let i = 0; i < urls.length; i++) {
            const text = urls[i].toString()
            if (text.startsWith("file://")) {
                paths.push(FormatUtils.localPathFromUrl(urls[i]))
            }
            // 非文件 URI（如拖入文本、网页）静默忽略
        }
        if (paths.length === 0) {
            mainWindow.showSuccessToast(qsTr("请拖入文件或文件夹"))
            return
        }

        for (let i = 0; i < paths.length; i++) {
            AppController.transferController.createSendSession(deviceId, paths[i])
        }
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
                highlighted: true  // 误关窗口的主路径，突出显示
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
            // selectedFiles 返回 URL 列表，逐个转成本地绝对路径后发起发送
            let urls = fileDialog.selectedFiles
            for (let i = 0; i < urls.length; i++) {
                let path = mainWindow.localPathFromUrl(urls[i])
                AppController.transferController.createSendSession(mainWindow._selId, path)
            }
        }
    }

    // 文件夹选择弹窗：选中后转换为本地路径再创建发送会话
    FolderDialog {
        id: folderDialog
        title: qsTr("选择要发送的文件夹")
        onAccepted: {
            // selectedFolder 也是 URL 格式，需要转成本地路径
            let path = mainWindow.localPathFromUrl(selectedFolder)
            AppController.transferController.createSendSession(mainWindow._selId, path)
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
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredHeight: 56
                        Layout.preferredWidth: 56
                        radius: 28
                        color: Style.Color.primary
                        Label{
                            anchors.centerIn: parent
                            text: "我"
                            color: Style.Color.textOnAccent
                            font.pixelSize: 14
                            font.bold: true
                        }
                    }
                //中间信息列
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        spacing:2
                        Label {
                            Layout.fillWidth: true
                            text: ConfigManager.deviceName
                            color: Style.Color.textMain
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
            radius: 18
            color: Style.Color.primary

            property bool _hovered: false

            Label {
                anchors.centerIn: parent
                text: "我"
                color: Style.Color.textOnAccent
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
        selectedDeviceId: AppController.peerDiscoveryViewModel.selectedDeviceId

        onDeviceSelected: function(deviceId, deviceName, ipAddress, isOnline) {
            mainWindow.selectDevice(deviceId)
        }
        // 拖拽文件到设备列表项：先选中该设备，再统一裁决并创建发送会话
        onFilesDropped: function(deviceId, urls) {
            mainWindow.selectDevice(deviceId)
            mainWindow.handleDroppedFiles(deviceId, urls)
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
            visible: mainWindow._selId.length === 0

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
            visible: mainWindow._selId.length > 0
            deviceId: mainWindow._selId
            deviceName: mainWindow._selInfo.deviceName || ""
            ipAddress: mainWindow._selInfo.ipAddress || ""
            isOnline: mainWindow._selInfo.isOnline || false

            background: Rectangle { color: "transparent" }

            onSendFileRequested: fileDialog.open()
            onSendFolderRequested: folderDialog.open()
            onFilesDropped: function(urls) {
                mainWindow.handleDroppedFiles(mainWindow._selId, urls)
            }
        }
    }

    // ======== 弹窗（不变） ========

    AcceptDialog {
        id: acceptDialog
        // 发送方取消传输：弹窗已自动关闭，这里补一条提示说明原因
        onTransferStale: mainWindow.showErrorToast(qsTr("对方已取消本次传输"))
    }

    // 直连失败后的中继确认弹窗（AskBeforeRelay 策略）
    Dialog {
        id: relayConfirmDialog
        title: qsTr("直连失败")
        modal: true
        anchors.centerIn: parent
        width: Math.min(420, parent ? parent.width - 48 : 420)
        padding: 20

        property string _sessionId: ""

        contentItem: ColumnLayout {
            spacing: 14
            anchors.fill: parent

            Label {
                text: qsTr("与目标设备直连失败，是否通过中继服务器转发本次传输？转发速度可能受限于服务器带宽。")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("暂不处理将在约 2 分钟后自动取消传输，也可以随时在任务卡片上手动取消。")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.pixelSize: 12
                color: Style.Color.textMuted
            }
        }

        footer: RowLayout {
            spacing: 10
            anchors.margins: 16

            Item {
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("使用中继")
                onClicked: {
                    AppController.transferController.retryViaRelay(relayConfirmDialog._sessionId)
                    relayConfirmDialog.close()
                }
            }
            Button {
                text: qsTr("取消传输")
                onClicked: {
                    AppController.transferController.cancelSession(relayConfirmDialog._sessionId)
                    relayConfirmDialog.close()
                }
            }
        }
    }

    // 接收完成通知卡：非阻塞展示，提供打开所在位置的快捷操作
    Popup {
        id: completeToast
        property string _filePath: ""
        property string _fileName: ""

        x: parent ? parent.width - width - 24 : 0
        y: parent ? parent.height - height - 24 : 0
        width: Math.min(330, parent ? parent.width - 48 : 330)
        padding: 14
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        enter: Transition {
            NumberAnimation {
                property: "opacity"; from: 0; to: 1
                duration: mainWindow.kPopupEnterDuration
                easing.type: Easing.OutCubic
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"; from: 1; to: 0
                duration: Style.Motion.fast
            }
        }

        background: Rectangle {
            color: Style.Color.window
            radius: Style.Radius.md
            border.color: Style.Color.border
        }

        contentItem: ColumnLayout {
            spacing: Style.Space.xs

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.Space.sm

                Label {
                    text: qsTr("接收完成")
                    font.pixelSize: 14
                    font.bold: true
                    color: Style.Color.success
                    Layout.fillWidth: true
                }

                ToolButton {
                    text: "✕"
                    font.pixelSize: 12
                    onClicked: completeToast.close()
                }
            }

            Label {
                text: completeToast._fileName
                font.pixelSize: 13
                color: Style.Color.textMain
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("文件已保存到接收目录")
                font.pixelSize: 12
                color: Style.Color.textMuted
            }

            Button {
                text: qsTr("打开所在位置")
                highlighted: true
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    ConfigManager.openFolder(completeToast._filePath)
                    completeToast.close()
                }
            }
        }

        Timer {
            interval: 6000
            running: completeToast.visible
            onTriggered: completeToast.close()
        }
    }

    Connections {
        target: AppController.transferController
        function onReceiveRequestReceived(sessionId, senderDeviceId, senderName, fileName,
                                          fileSize, totalFiles, totalBytes,
                                          isDirectory, fileList) {
            // 有新传输请求时切到发送方会话（其信息由视图模型按 ID 解析）
            mainWindow.selectDevice(senderDeviceId)
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
            completeToast._fileName = fileName
            completeToast._filePath = filePath
            completeToast.open()
            trayIcon.showMessage(qsTr("传输完成"), qsTr("已完成一项文件传输"))
        }
        function onErrorOccurred(message: string): void { mainWindow.showErrorToast(message) }
        function onMessageOccurred(message: string): void { mainWindow.showSuccessToast(message) }
        function onRelayConfirmRequested(sessionId: string, deviceId: string): void {
            // 策略判断已在 C++ 完成：进入此分支即 AskBeforeRelay 档，只负责弹窗
            relayConfirmDialog._sessionId = sessionId
            relayConfirmDialog.open()
        }
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
            mainWindow.showErrorToast(qsTr("本地保存失败，历史可能缺失"))
            trayIcon.showMessage(qsTr("本地历史"), qsTr("本地保存失败，历史可能缺失"))
        }
    }

    // 底部轻提示 Toast：错误与成功共用
    Toast {
        id: toastPopup
    }
}
