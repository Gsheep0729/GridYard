/**
 * @file    Main.qml
 * @version 7.15.19
 * @date    2026-10-04
 * @author  GridYard Team
 * @brief   GridYard 客户端根窗口
 *
 * 标题通过 AppController.applicationName/Version 绑定，
 * 关窗时由用户选择隐藏到后台或退出程序，有传输进行中时弹窗附警示行。
 * 左侧显示在线设备列表，右侧显示设备会话页。
 * 拖拽发送统一在本文件解码和裁决，弹窗与提示分层反馈。
 *
  * Change Log:
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 关闭确认弹窗注入活动传输计数，传输进行中时显示退出将中断的警示行
* [v7.15.17] GY   2026-10-04
* * 接收请求改串行装配：弹窗占用时入待显队列，关闭后经 Controller 快照取下一个等待确认的请求
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
 * [v7.15.13] GY   2026-10-04
 * * 版本头对齐到 v7.15.13
 * [v7.15.12] GY   2026-10-03
 * 版本头对齐到 v7.15.12
 * [v7.15.11] GY   2026-10-03
 * * 版本头对齐到 v7.15.11
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.15.4] GY   2026-10-03
 * * 托盘图标拆出 ui/TrayIcon.qml，菜单行为经信号上抛
 * * 关闭确认弹窗拆出 ui/CloseConfirmDialog.qml，空态占位拆出 ui/HomePlaceholder.qml
 * * 左侧设备栏拆出 ui/Sidebar.qml（工具栏 + 设备列表 + 两个弹窗），信号上抛
 * * 中继确认弹窗拆出 ui/RelayConfirmDialog.qml，用户选择经信号回传
 * * 接收完成通知卡拆出 ui/CompletionToast.qml，暴露 openWith 接口
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
import QtQuick.Dialogs
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
        mainWindow.bringMainWindowToFront()
        closeChoiceDialog.open()
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
        onHideToTrayRequested: mainWindow.hideToTray()
        onQuitRequested: mainWindow.requestApplicationQuit()
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

    // ======== 三栏主体 ========

    // 左侧：工具栏 + 设备列表（设备选择/拖拽/设置入口经信号上抛）
    Sidebar {
        id: sidebar

        onDeviceSelected: (deviceId) => mainWindow.selectDevice(deviceId)
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
            if (Object.keys(AppController.peerDiscoveryViewModel.deviceById(senderDeviceId)).length > 0) {
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
            trayIcon.showMessage(qsTr("传输请求"),
                                 qsTr("%1 想发送 %2 个文件").arg(senderName).arg(totalFiles))
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
