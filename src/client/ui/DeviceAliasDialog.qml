/**
 * @file    DeviceAliasDialog.qml
 * @version 7.19.1
 * @date 2026-11-02
 * @author  GridYard Team
 * @brief   设置备注对话框
 *
 * 设备卡右键菜单"设置备注"的编辑入口：回车或点保存提交，Esc 取消；
 * 备注留空提交即清除。确认后经信号回传设备 ID 与备注，由 Main.qml
 * 调用视图模型落库并刷新列表。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material  // 按钮投影全局取消（U2-28）：按钮 Material.elevation 置 0
import QtQuick.Layouts
import "../utils/Style.js" as Style

Dialog {
    id: aliasDialog

    title: qsTr("设置备注")
    modal: true
    anchors.centerIn: Overlay.overlay
    // 弹窗宽度三档（U2-31）：确认/提示与单字段小表单归窄档 420
    width: Math.min(420, parent ? parent.width - 48 : 420)
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

    // footer 由弹窗布局定位、不是锚定项（同 CloseConfirmDialog G-01 清理口径）：
    // 承载行 + 有效锚定边距提供 16px 内边距
    footer: Item {
        implicitHeight: aliasFooterRow.implicitHeight + Style.Space.lg * 2

        RowLayout {
            id: aliasFooterRow
            anchors.fill: parent
            anchors.margins: Style.Space.lg
            spacing: 10

            Item {
                Layout.fillWidth: true
            }

            Button {
                Material.elevation: 0
                text: qsTr("取消")
                onClicked: aliasDialog.close()
            }

            Button {
                Material.elevation: 0
                text: qsTr("保存")
                highlighted: true
                onClicked: aliasDialog.submit()
            }
        }
    }
}
