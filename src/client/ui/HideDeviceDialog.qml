/**
 * @file    HideDeviceDialog.qml
 * @version 7.17.4
 * @date 2026-10-04
 * @author  GridYard Team
 * @brief   不显示该聊天确认弹窗
 *
 * 设备卡右键菜单"不显示该聊天"的确认入口：说明隐藏后对方发来新消息
 * 会自动恢复显示、也可在设置页手动恢复，确认后经信号回传设备 ID，
 * 由 Main.qml 调用视图模型落库。从 Main.qml 拆出的确认弹窗先例之一。
 *
 * Change Log:
 * [v7.17.4] GY   2026-10-04
 * * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 新增不显示该聊天确认弹窗，说明自动恢复语义与设置页恢复入口
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../utils/Style.js" as Style

Dialog {
    id: hideDeviceDialog

    title: qsTr("不显示该聊天")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 48 : 420)
    padding: 20

    // 待隐藏设备信息（openFor 注入）
    property string deviceId: ""
    property string deviceName: ""

    // 用户确认隐藏（携带设备 ID）
    signal hideConfirmed(string deviceId)

    // 注入设备信息后打开弹窗
    function openFor(targetDeviceId: string, targetDeviceName: string): void {
        hideDeviceDialog.deviceId = targetDeviceId
        hideDeviceDialog.deviceName = targetDeviceName
        hideDeviceDialog.open()
    }

    ColumnLayout {
        spacing: 14
        anchors.fill: parent

        Label {
            text: qsTr("将不在设备列表中显示「%1」。隐藏后对方发来新消息会自动恢复显示，也可以在设置页的「已隐藏设备」中恢复。")
                  .arg(hideDeviceDialog.deviceName)
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    footer: RowLayout {
        spacing: 10
        anchors.margins: 16

        Item {
            Layout.fillWidth: true
        }

        Button {
            text: qsTr("取消")
            onClicked: hideDeviceDialog.close()
        }

        Button {
            text: qsTr("不显示")
            highlighted: true
            onClicked: {
                hideDeviceDialog.close()
                hideDeviceDialog.hideConfirmed(hideDeviceDialog.deviceId)
            }
        }
    }
}
