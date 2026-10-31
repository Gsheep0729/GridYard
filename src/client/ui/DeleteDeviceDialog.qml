/**
 * @file    DeleteDeviceDialog.qml
 * @version 7.20.1
 * @date 2026-10-05
 * @author  GridYard Team
 * @brief   删除该设备确认弹窗
 *
 * 设备卡右键菜单"删除该设备"的确认入口：文案明示将删除该设备的聊天
 * 记录与传输历史、不影响已接收文件，并说明对方再次上线会以全新设备
 * 身份重新出现，确认后经信号回传设备 ID，由 Main.qml 先取消进行中
 * 会话再调用删除接口。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../utils/Style.js" as Style

Dialog {
    id: deleteDeviceDialog

    title: qsTr("删除该设备")
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
            text: qsTr("将删除与「%1」的聊天记录和传输历史，不影响已接收的文件。")
                  .arg(deleteDeviceDialog.deviceName)
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // 扫描语义说明：网络里的设备删不掉，删除只清除本地记录
        Label {
            text: qsTr("对方再次上线时，会以全新设备身份重新出现。")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            color: Style.Color.textSecondary
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
