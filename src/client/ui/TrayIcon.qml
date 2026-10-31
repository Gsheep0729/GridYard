/**
 * @file    TrayIcon.qml
 * @version 8.2.1
 * @date 2026-11-01
 * @author  GridYard Team
 * @brief   系统托盘图标
 *
 * 托盘菜单与激活行为经信号上抛，主窗口决定显示、隐藏、打开接收目录、
 * 打开设置或退出；通知消息统一经 notify* 入口发出，托盘不可用（available
 * 为 false）时在入口处统一跳过，调用点不再各自判空；通知点击按最近一次
 * 通知的类别路由：聊天消息点击上抛选中设备动作，传输完成点击上抛打开
 * 接收目录动作，其余类别无点击语义不动作。
 * 勿扰接收为纯数据开关，菜单项直写 ConfigManager，不经窗口动作链。
 * tooltip 反映传输活动：有活动会话时追加「传输中 N 项」，勿扰开启时追加「勿扰中」。
 * 发送剪贴板为二级菜单列出在线设备，触发经信号上抛由主窗口评估回显后发送。
 * 从 Main.qml 拆出。
 */

import QtQuick
import Qt.labs.platform as Platform
import "../utils/FormatUtils.js" as FormatUtils

Platform.SystemTrayIcon {
    id: tray

    signal showRequested()
    signal hideRequested()
    signal quitRequested()
    signal openReceiveFolderRequested()
    signal settingsRequested()
    signal sendClipboardRequested(string deviceId)
    // 通知点击动作信号（C-17）：SystemTrayIcon.messageClicked() 本身不带载荷，
    // 由 routeMessageClicked 按最近一次通知的类别翻译成具体动作上抛
    signal chatMessageClicked(string deviceId)
    signal transferCompletedClicked()

    // 在线设备快照：发送剪贴板子菜单的数据源，发现列表刷新时自动重算；
    // 离线设备不可作为剪贴板外发目标（聊天链路对离线目标同步拒绝）
    readonly property var onlinePeers: {
        const peers = AppController.peerDiscoveryViewModel.peers
        const online = []
        for (let i = 0; i < peers.length; i++) {
            if (peers[i].isOnline === true) {
                online.push(peers[i])
            }
        }
        return online
    }

    visible: true
    // 开发者模式实例的托盘提示带 " #N" 标识，正常实例后缀为空串；
    // 有活动传输时追加「传输中 N 项」，随 activeSessionCount 变化自动刷新；
    // 勿扰开启时追加「勿扰中」，不开菜单也能确认当前接收档位
    tooltip: {
        const base = qsTr("GridYard") + AppController.instanceTitleSuffix
        const count = AppController.transferController.activeSessionCount
        let tip = count > 0 ? base + qsTr(" · 传输中 %1 项").arg(count) : base
        if (ConfigManager.doNotDisturb) {
            tip += qsTr(" · 勿扰中")
        }
        return tip
    }
    icon.source: "qrc:/qt/qml/cqnu/gridyard/client/icons/gridyard.png"

    menu: Platform.Menu {
        Platform.MenuItem {
            text: qsTr("显示主窗口")
            onTriggered: tray.showRequested()
        }
        Platform.MenuItem {
            text: qsTr("隐藏到托盘")
            onTriggered: tray.hideRequested()
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("打开接收目录")
            onTriggered: tray.openReceiveFolderRequested()
        }
        // 勿扰接收开关：直写 ConfigManager（数据开关不经主窗口动作链），
        // 开启期间新传输请求由 C++ 按 UserRejected 自动拒绝并明示勿扰中
        Platform.MenuItem {
            text: qsTr("勿扰接收")
            checkable: true
            checked: ConfigManager.doNotDisturb
            onTriggered: ConfigManager.doNotDisturb = checked
        }
        // 发送剪贴板文本：二级菜单逐台列出在线设备（显示名备注优先），
        // 点击经信号上抛，主窗口读取剪贴板、门槛评估后回显外发；
        // 无在线设备时禁用占位，避免误选不可达目标
        Platform.Menu {
            id: clipboardMenu
            title: qsTr("发送剪贴板")

            Instantiator {
                model: tray.onlinePeers
                delegate: Platform.MenuItem {
                    required property var modelData
                    text: FormatUtils.displayName(modelData.alias, modelData.deviceName)
                    onTriggered: tray.sendClipboardRequested(modelData.deviceId)
                }
                onObjectAdded: (index, object) => clipboardMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => clipboardMenu.removeItem(object)
            }

            Platform.MenuItem {
                text: qsTr("无在线设备")
                enabled: false
                visible: tray.onlinePeers.length === 0
            }
        }
        Platform.MenuItem {
            text: qsTr("设置")
            onTriggered: tray.settingsRequested()
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("退出")
            onTriggered: tray.quitRequested()
        }
    }

    // —— 通知送达与点击路由（C-17）——
    // SystemTrayIcon.messageClicked() 不带载荷，无法区分点击的是哪条气泡：
    // notify* 入口记录最近一次通知的类别与目标设备，点击回调按类别上抛
    // 动作信号。仅聊天与传输完成两类有点击语义，其余类别与"无记录"同样
    // 保持无动作兜底（部分后端本身不回调 messageClicked，表现一致）。
    // 托盘不可用（available 为 false）时 Qt 对 showMessage 静默丢弃，
    // 判空统一收敛在 _notify 一处，调用点不再各自判 available。
    property string _lastMessageKind: ""
    property string _lastChatDeviceId: ""

    // 普通提示（后台运行、传输请求、本地失败等）：无点击语义
    function notifyInfo(title: string, message: string): void {
        _notify("", "", title, message)
    }
    // 聊天消息气泡：点击动作是选中发送方设备，载荷记录 deviceId
    function notifyChatMessage(deviceId: string, title: string, message: string): void {
        _notify("chat", deviceId, title, message)
    }
    // 传输完成气泡：点击动作是打开接收目录
    function notifyTransferCompleted(title: string, message: string): void {
        _notify("transferCompleted", "", title, message)
    }

    function _notify(kind: string, deviceId: string, title: string, message: string): void {
        _lastMessageKind = kind
        _lastChatDeviceId = deviceId
        if (available) {
            showMessage(title, message)
        }
    }

    // 通知点击路由：只认最近一次通知的类别，无记录/无动作类别直接吞掉
    function routeMessageClicked(): void {
        if (_lastMessageKind === "chat") {
            tray.chatMessageClicked(tray._lastChatDeviceId)
        } else if (_lastMessageKind === "transferCompleted") {
            tray.transferCompletedClicked()
        }
    }

    onMessageClicked: routeMessageClicked()

    onActivated: function(reason) {
        if (reason === Platform.SystemTrayIcon.Trigger
                || reason === Platform.SystemTrayIcon.DoubleClick) {
            tray.showRequested()
        }
    }
}
