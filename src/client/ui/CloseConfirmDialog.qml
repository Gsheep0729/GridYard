/**
 * @file    CloseConfirmDialog.qml
 * @version 7.18.0
 * @date 2026-10-05
 * @author  GridYard Team
 * @brief   关闭确认弹窗
 *
 * 关窗时询问隐藏到后台还是退出程序；托盘可用性由主窗口注入，
 * 用户选择经信号回传，打开时先发 prepareToShow 让主窗口前置聚焦。
 * 有进行中的传输时显示警示行，提示退出程序会中断它们。
 * 常规询问时提供"记住我的选择"复选框，勾选后写入关窗行为配置；
 * 活动传输拦截等场景下主窗口置 rememberAllowed 为 false 隐藏复选框。
 * 从 Main.qml 拆出。
 *
 * Change Log:
 * [v7.18.0] GY   2026-10-05
 * * 新增"记住我的选择"复选框：勾选后点击隐藏或退出即上报写入关窗行为配置，
 *   仅托盘可用且常规询问时显示，活动传输拦截场景不提供记忆入口，
 *   每次打开重置为不勾选
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
 * [v7.17.4] GY   2026-10-04
 * * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
 * [v7.15.19] GY   2026-10-04
 * * 版本头对齐到 v7.15.19
 * [v7.15.18] GY   2026-10-04
 * * 新增传输进行中警示行：活动会话数大于 0 时显示，退出将中断它们；
 *   隐藏到后台分支不受影响，后台继续传输
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
    // 当前进行中的传输数量（由主窗口注入，0 时隐藏警示行）
    property int activeTransferCount: 0
    // 是否显示"记住我的选择"复选框（由主窗口按 C++ 决策注入：常规询问时允许；
    // 活动传输拦截场景用户偏好已表达过，这只是 A6 防护，不提供记忆入口）
    property bool rememberAllowed: false

    // 用户选择，携带复选框勾选状态供主窗口上报写入关窗行为配置
    signal hideToTrayRequested(bool rememberSelection)
    signal quitRequested(bool rememberSelection)
    // 打开或重试聚焦前让主窗口前置（置顶逻辑留在主窗口）
    signal prepareToShow()

    onOpened: {
        closeConfirmDialog.prepareToShow()
        // 复选框每次打开重置为不勾选，上次勾选不默认延续
        rememberCheckBox.checked = false
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

        // 传输进行中警示：只针对退出分支，隐藏到后台后传输继续
        Label {
            visible: closeConfirmDialog.activeTransferCount > 0
            text: qsTr("有 %1 个传输任务正在进行，退出将中断它们")
                  .arg(closeConfirmDialog.activeTransferCount)
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            color: Style.Color.warning
        }

        // "记住我的选择"复选框：仅托盘可用且常规询问时显示——托盘不可用时
        // 关窗即退出，记住没有意义；默认不勾选，打开时重置
        CheckBox {
            id: rememberCheckBox
            visible: closeConfirmDialog.trayAvailable && closeConfirmDialog.rememberAllowed
            text: qsTr("记住我的选择，下次不再询问")
            font.pixelSize: 13
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
                closeConfirmDialog.hideToTrayRequested(rememberCheckBox.checked)
            }
        }
        Item {
            Layout.fillWidth: true
        }
        Button {
            text: qsTr("退出程序")
            onClicked: {
                closeConfirmDialog.close()
                closeConfirmDialog.quitRequested(rememberCheckBox.checked)
            }
        }
        Button {
            text: qsTr("取消")
            onClicked: closeConfirmDialog.close()
        }
    }
}
