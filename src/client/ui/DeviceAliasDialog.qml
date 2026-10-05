/**
 * @file    DeviceAliasDialog.qml
 * @version 7.19.0
 * @date 2026-10-05
 * @author  GridYard Team
 * @brief   设置备注对话框
 *
 * 设备卡右键菜单"设置备注"的编辑入口：回车或点保存提交，Esc 取消；
 * 备注留空提交即清除。确认后经信号回传设备 ID 与备注，由 Main.qml
 * 调用视图模型落库并刷新列表。
 *
 * Change Log:
 * [v7.19.0] GY   2026-10-05
 * * 版本头对齐到 v7.19.0
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
 * [v7.17.4] GY   2026-10-04
 * * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 新增设置备注对话框，回车提交、Esc 取消、留空提交即清除
* [v7.17.2] GY   2026-10-04
* * 新增设置备注对话框，回车提交、Esc 取消、空值清除
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../utils/Style.js" as Style

Dialog {
    id: aliasDialog

    title: qsTr("设置备注")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(440, parent ? parent.width - 48 : 440)
    padding: 0

    // 待编辑设备信息（openFor 注入）
    property string deviceId: ""
    property string deviceName: ""

    // 用户确认备注（携带设备 ID 与备注文本，空串表示清除）
    signal aliasConfirmed(string deviceId, string alias)

    // 注入设备信息与当前备注后打开弹窗
    function openFor(targetDeviceId: string, targetDeviceName: string,
                     currentAlias: string): void {
        aliasDialog.deviceId = targetDeviceId
        aliasDialog.deviceName = targetDeviceName
        aliasInput.text = currentAlias
        aliasDialog.open()
    }

    // 提交当前输入：留空即清除备注，先关窗再上报，结果不受弹窗存续限制
    function submit(): void {
        aliasDialog.close()
        aliasDialog.aliasConfirmed(aliasDialog.deviceId, aliasInput.text.trim())
    }

    background: Rectangle {
        color: Style.Color.window
        radius: Style.Radius.md
        border.color: Style.Color.border
    }

    header: DialogHeader {
        title: qsTr("设置备注")
        subtitle: qsTr("备注只保存在本机，对方看不到；留空提交即清除备注。")
        onCloseClicked: aliasDialog.close()
    }

    contentItem: ColumnLayout {
        spacing: Style.Space.sm

        Label {
            Layout.leftMargin: Style.Space.xl
            Layout.rightMargin: Style.Space.xl
            text: qsTr("「%1」的备注").arg(aliasDialog.deviceName)
            font.pixelSize: 12
            color: Style.Color.textSecondary
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        TextField {
            id: aliasInput
            Layout.fillWidth: true
            Layout.leftMargin: Style.Space.xl
            Layout.rightMargin: Style.Space.xl
            Layout.bottomMargin: Style.Space.lg
            maximumLength: 30
            font.pixelSize: 14
            color: Style.Color.textMain
            selectByMouse: true
            // 占位提示改由 background 内自绘 Label 实现：Material 原生 placeholder
            // 不跟随垂直居中，在分数缩放下会沉到输入框下边框之外（v7.15.12 同款模式）
            placeholderText: ""

            background: Rectangle {
                radius: Style.Radius.sm
                color: Style.Color.window
                border.width: aliasInput.activeFocus ? 1.5 : 1
                border.color: aliasInput.activeFocus
                              ? Style.Color.primary : Style.Color.border

                // 占位提示：输入为空时显示，左边距对齐 Material 输入内边距（padding 属性
                // 在新 Material 样式下为 0，实测文本起点在框内 16px），垂直居中
                Label {
                    visible: aliasInput.text.length === 0
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    text: qsTr("输入备注名，留空清除")
                    color: Style.Color.textWeak
                    font.pixelSize: 14
                }

                Behavior on border.color {
                    ColorAnimation { duration: Style.Motion.base }
                }
            }

            // 回车提交（Esc 由 Dialog 默认关闭处理，等价取消）
            onAccepted: aliasDialog.submit()
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
            onClicked: aliasDialog.close()
        }

        Button {
            text: qsTr("保存")
            highlighted: true
            onClicked: aliasDialog.submit()
        }
    }
}
