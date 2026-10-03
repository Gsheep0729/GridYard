/**
 * @file    CloseConfirmDialog.qml
 * @version 7.15.17
 * @date    2026-10-04
 * @author  GridYard Team
 * @brief   关闭确认弹窗
 *
 * 关窗时询问隐藏到后台还是退出程序；托盘可用性由主窗口注入，
 * 用户选择经信号回传，打开时先发 prepareToShow 让主窗口前置聚焦。
 * 从 Main.qml 拆出。
 *
 * Change Log:
 * [v7.15.17] GY   2026-10-04
 * * 版本头对齐到 v7.15.17
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
 * * 自 Main.qml 拆出，托盘可用性注入，选择经信号回传
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../utils/Style.js" as Style

Dialog {
    id: closeConfirmDialog

    title: qsTr("关闭 GridYard")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 48 : 420)
    padding: 20

    // 托盘是否可用（由主窗口注入）
    property bool trayAvailable: false

    // 用户选择
    signal hideToTrayRequested()
    signal quitRequested()
    // 打开或重试聚焦前让主窗口前置（置顶逻辑留在主窗口）
    signal prepareToShow()

    onOpened: {
        closeConfirmDialog.prepareToShow()
        focusRetryTimer.restart()
    }

    // 短暂延迟后重试聚焦弹窗内容，避免窗口切换导致焦点丢失
    Timer {
        id: focusRetryTimer
        interval: 120
        repeat: false

        onTriggered: {
            closeConfirmDialog.prepareToShow()
            if (closeConfirmDialog.opened && closeConfirmDialog.contentItem) {
                closeConfirmDialog.contentItem.forceActiveFocus()
            }
        }
    }

    ColumnLayout {
        spacing: 14
        anchors.fill: parent

        Label {
            text: closeConfirmDialog.trayAvailable
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
            visible: closeConfirmDialog.trayAvailable
            text: qsTr("隐藏到后台")
            highlighted: true  // 误关窗口的主路径，突出显示
            onClicked: {
                closeConfirmDialog.close()
                closeConfirmDialog.hideToTrayRequested()
            }
        }
        Item {
            Layout.fillWidth: true
        }
        Button {
            text: qsTr("退出程序")
            onClicked: {
                closeConfirmDialog.close()
                closeConfirmDialog.quitRequested()
            }
        }
        Button {
            text: qsTr("取消")
            onClicked: closeConfirmDialog.close()
        }
    }
}
