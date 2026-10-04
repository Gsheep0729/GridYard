/**
 * @file    DeleteDeviceDialog.qml
 * @version 7.18.0
 * @date 2026-10-05
 * @author  GridYard Team
 * @brief   删除该聊天确认弹窗
 *
 * 设备卡右键菜单"删除该聊天"的确认入口：文案明示将删除该设备的聊天
 * 记录与传输历史、不影响已接收文件、对方再次出现按新设备处理，确认后
 * 经信号回传设备 ID，由 Main.qml 先取消进行中会话再调用删除接口。
 *
 * Change Log:
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
 * [v7.17.4] GY   2026-10-04
 * * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 新增删除该聊天确认弹窗，确认按钮沿用 DangerButton 危险语义
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../utils/Style.js" as Style

Dialog {
    id: deleteDeviceDialog

    title: qsTr("删除该聊天")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 48 : 420)
    padding: 20

    // 待删除设备信息（openFor 注入）
    property string deviceId: ""
    property string deviceName: ""

    // 用户确认删除（携带设备 ID）
    signal deleteConfirmed(string deviceId)

    // 注入设备信息后打开弹窗
    function openFor(targetDeviceId: string, targetDeviceName: string): void {
        deleteDeviceDialog.deviceId = targetDeviceId
        deleteDeviceDialog.deviceName = targetDeviceName
        deleteDeviceDialog.open()
    }

    ColumnLayout {
        spacing: 14
        anchors.fill: parent

        Label {
            text: qsTr("将删除与「%1」的聊天记录和传输历史，不影响已接收的文件，且对方再次出现时按新设备处理。")
                  .arg(deleteDeviceDialog.deviceName)
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
            onClicked: deleteDeviceDialog.close()
        }

        // 危险操作用语义化红色按钮，与清除缓存等不可逆操作一致
        DangerButton {
            text: qsTr("删除")
            onClicked: {
                deleteDeviceDialog.close()
                deleteDeviceDialog.deleteConfirmed(deleteDeviceDialog.deviceId)
            }
        }
    }
}
