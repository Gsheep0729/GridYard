/**
 * @file    TrayIcon.qml
 * @version 7.15.11
 * @date    2026-10-03
 * @author  GridYard Team
 * @brief   系统托盘图标
 *
 * 托盘菜单与激活行为经信号上抛，主窗口决定显示、隐藏或退出；
 * 通知消息仍由主窗口经 showMessage 发出。
 * 从 Main.qml 拆出。
 *
 * Change Log:
 * [v7.15.11] GY   2026-10-03
 * * 版本头对齐到 v7.15.11
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.15.4] GY   2026-10-03
 * * 自 Main.qml 拆出，菜单与激活行为经信号上抛
 */

import QtQuick
import Qt.labs.platform as Platform

Platform.SystemTrayIcon {
    id: tray

    signal showRequested()
    signal hideRequested()
    signal quitRequested()

    visible: true
    tooltip: qsTr("GridYard")
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
