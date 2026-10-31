/**
 * @file    Main.qml
 * @version 7.25.0
 * @date 2026-10-08
 * @author  GridYard Team
 * @brief   GridYard 客户端根窗口
 *
 * 标题通过 AppController.applicationName/Version 绑定，
 * 关窗行为由 C++ 决策分发：弹窗询问、隐藏到后台或直接退出，
 * 常规询问附"记住我的选择"复选框，有传输进行中时弹窗附警示行。
 * 左侧显示在线设备列表，右侧显示设备会话页。
 * 拖拽发送统一在本文件解码和裁决，弹窗与提示分层反馈。
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
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
    // 开发者模式实例在名称后追加 " #N" 标识，正常实例后缀为空串
    title:   "%1%2 v%3".arg(AppController.applicationName)
                     .arg(AppController.instanceTitleSuffix)
                     .arg(AppController.applicationVersion)
    color: Style.Color.pageBg

    // 统一 Material 主题色为品牌蓝（在根窗口设置后向全部子控件传播）
    Material.theme: Material.Light
    Material.primary: Style.Color.primary
    Material.accent: Style.Color.primary

    // 窗口首次显示后记录常规标志位，供临时置顶后恢复使用
    Component.onCompleted: _normalWindowFlags = mainWindow.flags

    onClosing: function(close) {
        if (_allowWindowClose) {
            return  // 已获准退出，不拦截
        }
        close.accepted = false
        // 关窗行为决策下沉 C++：hide 直接隐藏到后台，exit 直接退出，
        // 记住退出但仍有活动传输时改返回 confirm，弹警示确认（A6 防护不随记忆豁免）
        const action = AppController.resolveWindowCloseAction()
        if (action === "hide") {
            hideToTray()
        } else if (action === "exit") {
            requestApplicationQuit()
        } else {
            // ask 为常规询问（可显示记忆复选框），confirm 为活动传输拦截（不再提供记忆）
            closeChoiceDialog.rememberAllowed = action === "ask"
            bringMainWindowToFront()
            closeChoiceDialog.open()
        }
    }

    // 当前选中设备：选择状态由视图模型持有，设备列表刷新时信息自动重算
    property string _selId: AppController.peerDiscoveryViewModel.selectedDeviceId
    property var _selInfo: {
        const vm = AppController.peerDiscoveryViewModel
        void vm.peers  // 引用设备列表建立依赖，在线状态或 IP 变化时重算
        return vm.deviceById(_selId)
    }
    property bool _allowWindowClose: false  // 标记用户已确认退出，允许窗口关闭
    // 记录常规窗口标志，临时置顶后恢复
    property int _normalWindowFlags: 0
    // 待显接收请求队列：确认弹窗一次只展示一个请求，其余按到达序排队
    property var _pendingReceiveRequests: []

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
        showMainWindow()
        releaseTopMostTimer.restart()
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
    function showErrorToast(message: string): void {
        toastPopup.show(message, true)
    }

    function showSuccessToast(message: string): void {
        toastPopup.show(message, false)
    }

    // 确认弹窗关闭后串行弹出下一个等待确认的接收请求：队列按到达序取出，
    // 已被后端终结（超时/对方取消）的排队项经 Controller 快照过滤跳过，无更多则不弹
    function showNextReceiveRequest(): void {
        if (acceptDialog.opened) {
            return
        }
        const waiting = AppController.transferController.waitingConfirmReceiveSessions()
        while (mainWindow._pendingReceiveRequests.length > 0) {
            const next = mainWindow._pendingReceiveRequests.shift()
            for (let i = 0; i < waiting.length; i++) {
                if (waiting[i].sessionId === next.sessionId) {
                    acceptDialog.openWith(next)
                    return
                }
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

    TrayIcon {
        id: trayIcon
        onShowRequested: mainWindow.showMainWindow()
        onHideRequested: mainWindow.hideToTray()
        onQuitRequested: mainWindow.requestApplicationQuit()
    }

    // 选中设备：设备名、IP 与在线状态由视图模型按选中 ID 自动解析
    function selectDevice(deviceId: string): void {
        AppController.peerDiscoveryViewModel.selectedDeviceId = deviceId
    }

    // 拖拽发送：只做 URL 解码与非文件项过滤，在线裁决由 C++ createSendSession 兜底
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

    CloseConfirmDialog {
        id: closeChoiceDialog
        trayAvailable: trayIcon.available
        // 传输进行中警示：活动会话数由后端属性驱动，弹窗展示当前值
        activeTransferCount: AppController.transferController.activeSessionCount
        // 勾选"记住我的选择"后把所选动作写入关窗行为配置，下次关窗不再询问
        onHideToTrayRequested: (rememberSelection) => {
            if (rememberSelection) {
                AppController.setCloseWindowAction("hide")
            }
            mainWindow.hideToTray()
        }
        onQuitRequested: (rememberSelection) => {
            if (rememberSelection) {
                AppController.setCloseWindowAction("exit")
            }
            mainWindow.requestApplicationQuit()
        }
        onPrepareToShow: mainWindow.bringMainWindowToFront()
    }

    // 文件选择弹窗：支持多选，选中后为每个文件创建发送会话
    FileDialog {
        id: fileDialog
        title: qsTr("选择要发送的文件")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("所有文件 (*)")]
        onAccepted: {
            // selectedFiles 返回 URL 列表，逐个转成本地绝对路径后发起发送
            for (let i = 0; i < fileDialog.selectedFiles.length; i++) {
                const path = FormatUtils.localPathFromUrl(fileDialog.selectedFiles[i])
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
            AppController.transferController.createSendSession(
                        mainWindow._selId, FormatUtils.localPathFromUrl(selectedFolder))
        }
    }

    SettingsDialog { id: settingsDialog }

    // 不显示该聊天确认：确认后落库隐藏，列表随过滤规则即时移除该卡
    HideDeviceDialog {
        id: hideDeviceDialog
        onHideConfirmed: (deviceId) =>
            AppController.peerDiscoveryViewModel.setDeviceHidden(deviceId, true)
    }

    // 删除该聊天确认：先取消该设备全部进行中会话（复用 N1-A/N1-F 取消能力）再删，
    // 删除接口连带清理聊天与传输历史并同步内存列表
    DeleteDeviceDialog {
        id: deleteDeviceDialog
        onDeleteConfirmed: (deviceId) => {
            AppController.transferController.cancelDeviceSessions(deviceId)
            AppController.peerDiscoveryViewModel.deleteDeviceWithHistory(deviceId)
        }
    }

    // 设备关联向导：新设备条目上发起与旧条目的合并（备注/好友等可选连带历史）
    DeviceMergeDialog {
        id: mergeDialog
    }

    // 多选群发：内容源与目标选择合并在一个弹窗内完成
    MultiTargetDialog {
        id: multiTargetDialog
    }

    // 设置备注弹窗：确认后落库并刷新列表，空备注即清除
    DeviceAliasDialog {
        id: aliasDialog
        onAliasConfirmed: (deviceId, alias) =>
            AppController.peerDiscoveryViewModel.setDeviceAlias(deviceId, alias)
    }

    // ======== 三栏主体 ========

    // 左侧：工具栏 + 设备列表（设备选择/拖拽/设置入口经信号上抛）
    Sidebar {
        id: sidebar

        onDeviceSelected: (deviceId) => mainWindow.selectDevice(deviceId)
        onContextActionRequested: (deviceId, deviceName, action) => {
            // 菜单意图只做路由：置顶取反走视图模型，隐藏/删除经确认弹窗，
            // 备注打开编辑弹窗（当前备注随设备信息注入，空值提交即清除）
            if (action === "pin") {
                AppController.peerDiscoveryViewModel.setDevicePinned(deviceId, true)
            } else if (action === "unpin") {
                AppController.peerDiscoveryViewModel.setDevicePinned(deviceId, false)
            } else if (action === "favorite") {
                AppController.peerDiscoveryViewModel.setDeviceFavorite(deviceId, true)
            } else if (action === "unfavorite") {
                AppController.peerDiscoveryViewModel.setDeviceFavorite(deviceId, false)
            } else if (action === "hide") {
                hideDeviceDialog.openFor(deviceId, deviceName)
            } else if (action === "rename") {
                const info = AppController.peerDiscoveryViewModel.deviceById(deviceId)
                aliasDialog.openFor(deviceId, deviceName, info.alias || "")
            } else if (action === "delete") {
                deleteDeviceDialog.openFor(deviceId, deviceName)
            } else if (action === "merge") {
                mergeDialog.openFor(deviceId, deviceName)
            }
        }
        onDeviceFilesDropped: (deviceId, urls) => {
            mainWindow.selectDevice(deviceId)  // 拖拽先选中再统一裁决
            mainWindow.handleDroppedFiles(deviceId, urls)
        }
        onSettingsRequested: settingsDialog.open()
    }

    // 右侧：会话页（填充剩余宽度）
    Rectangle {
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: Style.Color.surfaceLeft

        HomePlaceholder {
            anchors.fill: parent
            visible: mainWindow._selId.length === 0
        }

        // 设备会话页（显示名备注优先于广播名，备注变化随列表刷新即时生效）
        DeviceSessionView {
            id: sessionView
            anchors.fill: parent
            visible: mainWindow._selId.length > 0
            deviceId: mainWindow._selId
            deviceName: FormatUtils.displayName(mainWindow._selInfo.alias,
                                                mainWindow._selInfo.deviceName)
            remoteName: mainWindow._selInfo.deviceName || ""
            ipAddress: mainWindow._selInfo.ipAddress || ""
            isOnline: mainWindow._selInfo.isOnline || false

            background: Rectangle { color: "transparent" }

            onSendFileRequested: fileDialog.open()
            onSendFolderRequested: folderDialog.open()
            onSendMultiRequested: {
                // 目标多选弹窗内先确定内容（选择器或粘贴路径），再勾选目标
                multiTargetDialog.openFor("")
            }
            onFilesDropped: (urls) => mainWindow.handleDroppedFiles(mainWindow._selId, urls)
        }
    }

    // ======== 弹窗实例 ========

    AcceptDialog {
        id: acceptDialog
        // 发送方取消传输：弹窗已自动关闭，这里补一条提示说明原因
        onTransferStale: mainWindow.showErrorToast(qsTr("对方已取消本次传输"))
        // 接受/拒绝/关窗都汇入 closed：串行弹出下一个仍待确认的请求
        onClosed: mainWindow.showNextReceiveRequest()
    }

    // 直连失败后的中继确认弹窗（AskBeforeRelay 策略）
    RelayConfirmDialog {
        id: relayConfirmDialog
        onRelayChosen: (sessionId) => AppController.transferController.retryViaRelay(sessionId)
        onCancelChosen: (sessionId) => AppController.transferController.cancelSession(sessionId)
    }

    // 接收完成通知卡：非阻塞展示，提供打开所在位置的快捷操作
    CompletionToast {
        id: completeToast
    }

    Connections {
        target: AppController.transferController
        function onReceiveRequestReceived(sessionId, senderDeviceId, senderName, fileName,
                                          fileSize, totalFiles, totalBytes,
                                          isDirectory, fileList) {
            // 有新传输请求时切到发送方会话；设备不在发现/历史列表时不切换（行为同拆分前）
            const peerInfo = AppController.peerDiscoveryViewModel.deviceById(senderDeviceId)
            if (Object.keys(peerInfo).length > 0) {
                mainWindow.selectDevice(senderDeviceId)
            }
            const info = { sessionId: sessionId, senderName: senderName,
                           fileName: fileName, fileSize: fileSize,
                           totalFiles: totalFiles, totalBytes: totalBytes,
                           isDirectory: isDirectory, fileList: fileList }
            // 串行装配：弹窗空闲立即展示，否则入待显队列，避免覆盖正在展示的请求
            if (acceptDialog.opened) {
                mainWindow._pendingReceiveRequests.push(info)
            } else {
                acceptDialog.openWith(info)
            }
            // 通知里的设备名同样备注优先
            trayIcon.showMessage(qsTr("传输请求"),
                                 qsTr("%1 想发送 %2 个文件")
                                 .arg(FormatUtils.displayName(peerInfo.alias, senderName))
                                 .arg(totalFiles))
        }
        function onTransferCompleted(sessionId: string, fileName: string, filePath: string): void {
            completeToast.openWith(fileName, filePath)
            trayIcon.showMessage(qsTr("传输完成"), qsTr("已完成一项文件传输"))
        }
        function onErrorOccurred(message: string): void { mainWindow.showErrorToast(message) }
        function onMessageOccurred(message: string): void { mainWindow.showSuccessToast(message) }
        function onRelayConfirmRequested(sessionId: string, deviceId: string): void {
            // 策略判断已在 C++ 完成：进入此分支即 AskBeforeRelay 档，只负责弹窗
            relayConfirmDialog.openFor(sessionId)
        }
    }

    Connections {
        target: AppController.chatController
        function onIncomingMessageReceived(deviceId: string, senderName: string, preview: string): void {
            // 通知里的设备名同样备注优先（设备不在列表时回落广播名）
            const peerInfo = AppController.peerDiscoveryViewModel.deviceById(deviceId)
            trayIcon.showMessage(FormatUtils.displayName(peerInfo.alias, senderName), preview)
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
