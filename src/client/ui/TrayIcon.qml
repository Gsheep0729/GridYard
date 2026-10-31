/**
 * @file    TrayIcon.qml
 * @version 8.2.1
 * @date 2026-11-01
 * @author  GridYard Team
 * @brief   系统托盘图标
 *
 * 托盘菜单与激活行为经信号上抛，主窗口决定显示、隐藏、打开接收目录、
 * 打开设置或退出；通知消息仍由主窗口经 showMessage 发出。
 * 勿扰接收为纯数据开关，菜单项直写 ConfigManager，不经窗口动作链。
 * tooltip 反映传输活动：有活动会话时追加「传输中 N 项」，勿扰开启时追加「勿扰中」。
 * 从 Main.qml 拆出。
 */

import QtQuick
import Qt.labs.platform as Platform

Platform.SystemTrayIcon {
    id: tray

    signal showRequested()
    signal hideRequested()
    signal quitRequested()
    signal openReceiveFolderRequested()
    signal settingsRequested()

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

    onActivated: function(reason) {
        if (reason === Platform.SystemTrayIcon.Trigger
                || reason === Platform.SystemTrayIcon.DoubleClick) {
            tray.showRequested()
        }
    }
}
