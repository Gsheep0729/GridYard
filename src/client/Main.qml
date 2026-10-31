/**
 * @file    Main.qml
 * @version 8.2.1
 * @date 2026-11-01
 * @author  GridYard Team
 * @brief   GridYard 客户端根窗口
 *
 * 标题通过 AppController.applicationName/Version 绑定，
 * 关窗行为由 C++ 决策分发：弹窗询问、隐藏到后台或直接退出，
 * 常规询问附"记住我的选择"复选框，有传输进行中时弹窗附警示行。
 * 托盘退出复用关窗退出分支的同一决策与警示弹窗，两条退出路径防护一致；
 * 托盘菜单另提供打开接收目录（经 AppController 打开配置路径）与设置入口。
 * 左侧显示在线设备列表，右侧显示设备会话页。
 * 拖拽发送统一在本文件解码和裁决，弹窗与提示分层反馈。
 * 窗口尺寸/位置跨启动记忆：启动先恢复上次几何（C++ 侧已按当前屏幕可用区
 * 夹紧，落屏外位置弃用）再显示，几何变化去抖后落盘，最大化状态一并记忆。
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

    // 默认与最小尺寸：首次启动回落值；最小尺寸同时作为启动恢复的夹紧下限
    readonly property int _defaultWidth:   980
    readonly property int _defaultHeight:  725
    readonly property int _minimumWidth:   860
    readonly property int _minimumHeight:  620

    // 启动先隐藏：恢复上次几何（已按屏幕可用区夹紧）后再显示，避免可见跳变
    width:   _defaultWidth
    height:  _defaultHeight
    minimumWidth:  _minimumWidth
    minimumHeight: _minimumHeight
    visible: false
    // 开发者模式实例在名称后追加 " #N" 标识，正常实例后缀为空串
    title:   "%1%2 v%3".arg(AppController.applicationName)
                     .arg(AppController.instanceTitleSuffix)
                     .arg(AppController.applicationVersion)
    color: Style.Color.pageBg

    // 统一 Material 主题色为品牌蓝（在根窗口设置后向全部子控件传播）
    Material.theme: Material.Light
    Material.primary: Style.Color.primary
    Material.accent: Style.Color.primary

    // 窗口首次显示后记录常规标志位，供临时置顶后恢复使用；恢复上次几何后
    // 再显示；监听失败即刻提示
    Component.onCompleted: {
        _normalWindowFlags = mainWindow.flags
        restoreSavedGeometry()
        if (AppController.networkListenFailed) {
            mainWindow.showErrorToast(qsTr("网络监听失败，无法收发"))
        }
    }

    // 启动恢复：C++ 侧读取上次保存的几何并按当前屏幕可用区夹紧（多显示器
    // 拔插后落屏外的位置被放弃，交由窗口管理器自选），显示前应用避免跳变；
    // 最大化状态以记忆的普通态几何打底，显示后短延时再 showMaximized
    // （实测 mutter/Xwayland + xcb 下显示前设 visibility、显示后同步或
    // Qt.callLater 调用都会被 WM 丢弃，窗口仍以普通态出现），取消最大化
    // 即回到记忆尺寸
    function restoreSavedGeometry(): void {
        const geo = ConfigManager.restoreWindowGeometry(
            _defaultWidth, _defaultHeight, _minimumWidth, _minimumHeight)
        mainWindow.width = geo.width
        mainWindow.height = geo.height
        if (geo.hasPosition) {
            mainWindow.x = geo.x
            mainWindow.y = geo.y
        }
        mainWindow._windowedGeometry = Qt.rect(mainWindow.x, mainWindow.y,
                                               mainWindow.width, mainWindow.height)
        mainWindow._geometryRestoreDone = true
        mainWindow.visible = true
        if (geo.maximized) {
            maximizeRestoreTimer.restart()
        }
    }

    // 记录普通态几何并安排落盘：最大化期间几何来自 WM 的最大化布局不作记忆，
    // 保存时以冻结的最近普通态几何为准；几何变化去抖写入，SIGTERM/托盘退出/
    // 关窗退出等任一退出路径都不丢
    function noteWindowedGeometry(): void {
        if (mainWindow.visibility !== Window.Maximized
                && mainWindow.visibility !== Window.FullScreen) {
            mainWindow._windowedGeometry = Qt.rect(mainWindow.x, mainWindow.y,
                                                   mainWindow.width, mainWindow.height)
        }
        if (mainWindow._geometryRestoreDone) {
            geometrySaveTimer.restart()
        }
    }

    // 去抖落盘：普通态取当前几何，最大化取最近普通态几何 + maximized 标记；
    // 全屏布局不落盘，退出全屏后的几何再记
    function saveWindowGeometry(): void {
        if (mainWindow.visibility === Window.FullScreen) {
            return
        }
        const maximized = mainWindow.visibility === Window.Maximized
        const g = maximized ? mainWindow._windowedGeometry
                            : Qt.rect(mainWindow.x, mainWindow.y,
                                      mainWindow.width, mainWindow.height)
        ConfigManager.saveWindowGeometry(Math.round(g.x), Math.round(g.y),
                                         Math.round(g.width), Math.round(g.height),
                                         maximized)
    }

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
    // —— 窗口几何跨启动记忆（O-18）——
    // 启动恢复完成后才允许落盘，恢复过程中的赋值不触发写配置
    property bool _geometryRestoreDone: false
    // 最近一次普通态几何：最大化期间冻结，保存时随 maximized 标记一并落盘
    property rect _windowedGeometry: Qt.rect(x, y, width, height)

    // 几何变化即记录并去抖落盘（拖拽/缩放停止 0.8s 后才写配置，不逐帧刷盘）
    onXChanged: mainWindow.noteWindowedGeometry()
    onYChanged: mainWindow.noteWindowedGeometry()
    onWidthChanged: mainWindow.noteWindowedGeometry()
    onHeightChanged: mainWindow.noteWindowedGeometry()
    onVisibilityChanged: mainWindow.noteWindowedGeometry()

    Timer {
        id: geometrySaveTimer
        interval: 800
        repeat: false

        onTriggered: mainWindow.saveWindowGeometry()
    }

    // 最大化恢复延一刀：显示后同步/Qt.callLater 调用 showMaximized 实测被
    // WM 丢弃（详见 restoreSavedGeometry 注释），短延时重发即生效
    Timer {
        id: maximizeRestoreTimer
        interval: 150
        repeat: false

        onTriggered: mainWindow.showMaximized()
    }

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

    // 托盘退出与关窗退出分支同源决策：有活动传输时弹同款警示确认（不提供记忆，
    // 与关窗 A6 拦截一致），无活动传输直接退出，两条退出路径防护保持一致
    function requestTrayQuit(): void {
        const action = AppController.resolveTrayQuitAction()
        if (action === "confirm") {
            closeChoiceDialog.rememberAllowed = false
            bringMainWindowToFront()
            closeChoiceDialog.open()
        } else {
            requestApplicationQuit()
        }
    }

    // 托盘设置入口并入显示主窗口链：先唤回主窗口再打开设置弹窗，
    // 主窗口隐藏时也能改配置（如改接收路径），不必先进设置抄路径
    function openTraySettings(): void {
        showMainWindow()
        settingsDialog.open()
    }

    // 托盘发送剪贴板文本：先按门槛评估（无文本形态/超单条上限不予外发并给原因），
    // 发送动作前 Toast 回显内容前 20 字符防敏感内容静默外发；发送被拒（目标
    // 恰好离线等）时以失败提示覆盖回显提示，内容不会无反馈丢失
    function sendClipboardToDevice(deviceId: string): void {
        const text = AppController.chatController.clipboardText()
        const verdict = AppController.chatController.evaluateClipboardSend(text)
        if (!verdict.ok) {
            mainWindow.showErrorToast(verdict.reason)
            return
        }
        const info = AppController.peerDiscoveryViewModel.deviceById(deviceId)
        const deviceLabel = Object.keys(info).length > 0
                ? FormatUtils.displayName(info.alias, info.deviceName) : deviceId
        toastPopup.show(qsTr("将向「%1」发送剪贴板文本：%2")
                            .arg(deviceLabel).arg(verdict.echo), false)
        if (!AppController.chatController.sendText(deviceId, text)) {
            mainWindow.showErrorToast(qsTr("剪贴板发送失败，目标设备可能已离线"))
        }
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

    // 从任务卡重新唤起等待确认的接收请求：确认弹窗被直接关闭（Esc 等）后
    // 请求并不消失，会话保持等待确认状态，由任务卡上的确认入口找回
    function reopenReceiveConfirm(sessionId: string): void {
        const waiting = AppController.transferController.waitingConfirmReceiveSessions()
        for (let i = 0; i < waiting.length; i++) {
            if (waiting[i].sessionId === sessionId) {
                acceptDialog.openWith(waiting[i])
                return
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
        onQuitRequested: mainWindow.requestTrayQuit()
        // 打开接收目录不唤回主窗口：直达系统文件管理器里的配置接收路径
        onOpenReceiveFolderRequested: AppController.openReceiveFolder()
        onSettingsRequested: mainWindow.openTraySettings()
        // 发送剪贴板：回显与发送链在主窗口（Toast 实例在此）
        onSendClipboardRequested: (deviceId) => mainWindow.sendClipboardToDevice(deviceId)
    }

    // 选中设备：设备名、IP 与在线状态由视图模型按选中 ID 自动解析
    function selectDevice(deviceId: string): void {
        AppController.peerDiscoveryViewModel.selectedDeviceId = deviceId
    }

    // 拖拽发送：只做 URL 解码与非文件项过滤，在线裁决由 C++ createSendSession 兜底。
    // 目录或多文件拖入先经 sendConfirmDialog 回显绝对路径供确认（对齐 J-05 文件夹
    // 选择回显防护），误拖整个目录不再立即全量发送；单文件保持直发，与主流一致
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

        let hasDirectory = false
        for (let i = 0; i < paths.length; i++) {
            if (AppController.transferController.isDirectoryPath(paths[i])) {
                hasDirectory = true
                break
            }
        }
        if (paths.length > 1 || hasDirectory) {
            sendConfirmDialog.openWithPaths(deviceId, paths)
            return
        }

        AppController.transferController.createSendSession(deviceId, paths[0])
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

    // 文件夹选择弹窗：选中后先回显绝对路径供确认，再创建发送会话
    FolderDialog {
        id: folderDialog
        title: qsTr("选择要发送的文件夹")
        onAccepted: {
            // selectedFolder 也是 URL 格式，需要转成本地路径
            sendConfirmDialog.openWith(FormatUtils.localPathFromUrl(selectedFolder))
        }
    }

    // 发送确认弹窗：文件夹选择与目录/多文件拖拽共用。文件夹侧防护 GTK portal
    // 选择器的"打开"语义——选中高亮条目而非所在目录，导航进目录后点"打开"可能
    // 实际选中的是里面的子目录；拖拽侧防护误拖整个目录或多选文件直接全量发送。
    // 回显目标与绝对路径供用户核对，确认后才创建发送会话，避免错发而不自知
    Dialog {
        id: sendConfirmDialog
        title: sendConfirmDialog.singleFolder ? qsTr("确认发送文件夹")
                                              : qsTr("确认发送文件")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(480, parent ? parent.width - 48 : 480)
        standardButtons: Dialog.Yes | Dialog.No

        property string targetId: ""
        property string targetLabel: ""
        property var sendPaths: []

        // 单个目录时沿用 J-05 文件夹文案；多文件或混合内容逐项回显
        readonly property bool singleFolder: sendPaths.length === 1
            && AppController.transferController.isDirectoryPath(sendPaths[0])

        // 仅普通文件可同步统计体量（逐个 stat 开销可忽略）；含目录返回 -1 隐藏该行，
        // 不在 UI 线程递归遍历大目录，体量交由会话统计补齐
        readonly property int totalBytes: sendPaths.length > 0
            ? AppController.transferController.totalBytesForFiles(sendPaths) : -1

        // 文件夹选择弹窗入口：目标取当前选中设备，单路径包装成列表复用拖拽通路
        function openWith(path: string): void {
            openWithPaths(mainWindow._selId, [path])
        }

        function openWithPaths(deviceId: string, paths: var): void {
            const info = AppController.peerDiscoveryViewModel.deviceById(deviceId)
            targetId = deviceId
            targetLabel = FormatUtils.displayName(info.alias, info.deviceName)
            sendPaths = paths
            open()
        }

        contentItem: ColumnLayout {
            spacing: Style.Space.sm

            Label {
                Layout.fillWidth: true
                text: sendConfirmDialog.singleFolder
                      ? qsTr("将向「%1」发送所选文件夹内的全部内容：")
                            .arg(sendConfirmDialog.targetLabel)
                      : qsTr("将向「%1」发送以下 %2 项内容：")
                            .arg(sendConfirmDialog.targetLabel)
                            .arg(sendConfirmDialog.sendPaths.length)
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                // 长清单截尾展示：弹窗不滚动，只回显前若干项+总数供核对
                text: sendConfirmDialog.sendPaths.length > 8
                      ? sendConfirmDialog.sendPaths.slice(0, 8).join("\n")
                        + "\n" + qsTr("…等共 %1 项").arg(sendConfirmDialog.sendPaths.length)
                      : sendConfirmDialog.sendPaths.join("\n")
                color: Style.Color.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WrapAnywhere
            }

            Label {
                Layout.fillWidth: true
                visible: sendConfirmDialog.totalBytes >= 0
                text: qsTr("总大小：%1")
                      .arg(FormatUtils.formatBytes(sendConfirmDialog.totalBytes))
                color: Style.Color.textSecondary
                font.pixelSize: 12
            }
        }

        onAccepted: {
            for (let i = 0; i < sendConfirmDialog.sendPaths.length; i++) {
                AppController.transferController.createSendSession(
                    sendConfirmDialog.targetId, sendConfirmDialog.sendPaths[i])
            }
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

    // 网络监听失败常驻横幅：监听端口不可用时应用无法收发，全程保持可见
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        z: 20
        visible: AppController.networkListenFailed
        height: visible ? 30 : 0
        color: Style.Color.errorSoft

        Label {
            anchors.centerIn: parent
            text: qsTr("网络监听失败，无法收发；请检查端口占用后重启应用")
            color: Style.Color.error
            font.pixelSize: 12
        }
    }

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
            onReceiveConfirmRequested: (sessionId) => mainWindow.reopenReceiveConfirm(sessionId)
        }
    }

    // ======== 弹窗实例 ========

    AcceptDialog {
        id: acceptDialog
        // 发送方取消传输：弹窗已自动关闭，这里补一条提示说明原因
        onTransferStale: mainWindow.showErrorToast(qsTr("对方已取消本次传输"))
        // 直接关闭（Esc 等）不算答复：会话保持等待确认，提示用户去任务卡找回
        onRequestPostponed: (sessionId) =>
            mainWindow.showErrorToast(qsTr("传输请求已暂时收起，可在任务卡上重新打开确认"))
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
        target: AppController.peerDiscoveryViewModel
        function onDirectedPeerWentOffline(deviceId: string): void {
            // 邀请码导入的设备约 15 秒无回应即回落离线（定向心跳在跨 AP 环境
            // 不可达），给用户一句解释，避免被误读成导入失败
            mainWindow.showErrorToast(
                        qsTr("邀请添加的设备暂未回应，已转入最近见过；可重新导入其邀请码再次探测"))
        }
    }

    Connections {
        target: AppController.historyController
        function onOperationFailed(message: string): void {
            // 历史加载/删除/清空失败的统一出口：失败原先只发信号无人听，
            // 滚到顶部的翻页查询静默失败会被误读成"已到底没有更早消息"
            mainWindow.showErrorToast(message)
        }
    }

    Connections {
        target: AppController
        function onLocalHistoryOperationFailed(): void {
            mainWindow.showErrorToast(qsTr("本地保存失败，历史可能缺失"))
            trayIcon.showMessage(qsTr("本地历史"), qsTr("本地保存失败，历史可能缺失"))
        }
        function onWindowActivationRequested(): void {
            // 再次启动应用时的唤回请求：本实例还在运行，恢复窗口并置顶
            mainWindow.bringMainWindowToFront()
        }
    }

    // 底部轻提示 Toast：错误与成功共用
    Toast {
        id: toastPopup
    }
}
