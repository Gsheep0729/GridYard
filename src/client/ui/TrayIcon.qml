/**
 * @file    TrayIcon.qml
 * @version 8.2.1
 * @date 2026-11-01
 * @author  GridYard Team
 * @brief   系统托盘图标
 *
 * 托盘菜单与激活行为经信号上抛，主窗口决定显示、隐藏、打开接收目录、
 * 打开设置或退出；通知消息仍由主窗口经 showMessage 发出。
 * tooltip 反映传输活动：有活动会话时追加「传输中 N 项」。
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
    // 有活动传输时追加「传输中 N 项」，随 activeSessionCount 变化自动刷新
    tooltip: {
        const base = qsTr("GridYard") + AppController.instanceTitleSuffix
        const count = AppController.transferController.activeSessionCount
        return count > 0 ? base + qsTr(" · 传输中 %1 项").arg(count) : base
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
