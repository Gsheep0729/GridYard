/**
 * @file    RelayConfirmDialog.qml
 * @version 8.3.0
 * @date 2026-11-02
 * @author  GridYard Team
 * @brief   中继确认弹窗
 *
 * 直连失败且策略为询问后中继时展示，用户选择只经信号回传，
 * 策略判断已在 TransferSessionManager 内完成，本组件为纯弹窗。
 * 从 Main.qml 拆出，暴露 openFor(sessionId) 与两个选择信号。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material  // 按钮投影全局取消（U2-28）：按钮 Material.elevation 置 0
import QtQuick.Layouts
import "../utils/Style.js" as Style

Dialog {
    id: relayConfirmDialog

    title: qsTr("直连失败")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 48 : 420)
    // 弹窗族统一（U2-07）：自绘背景 + DialogHeader 页眉，内容区左右 20、上 12，
    // 与页眉边距衔接；页眉 ✕ 不代做选择——关窗后任务按既有文案约 2 分钟自动失败
    padding: 0
    topPadding: Style.Space.md
    leftPadding: Style.Space.xl
    rightPadding: Style.Space.xl

    background: Rectangle {
        color: Style.Color.window
        radius: Style.Radius.md
        border.color: Style.Color.border
    }

    header: DialogHeader {
        title: relayConfirmDialog.title
        onCloseClicked: relayConfirmDialog.close()
    }

    property string sessionId: ""

    // 用户选择经信号回传，由使用方调用控制器入口
    signal relayChosen(string sessionId)
    signal cancelChosen(string sessionId)

    function openFor(sessionId) {
        relayConfirmDialog.sessionId = sessionId
        relayConfirmDialog.open()
    }

    // contentItem 的几何由弹窗管理：不得再锚定 parent，否则与弹窗布局冲突，
    // 说明文字会逃逸内容区压到按钮行并左端裁切（审批 C-J03 A/B 定案）
    contentItem: ColumnLayout {
        spacing: 14

        Label {
            text: qsTr("与目标设备直连失败，是否通过中继服务器转发本次传输？转发速度可能受限于服务器带宽。")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("暂不处理将在约 2 分钟后自动失败，也可以随时在任务卡片上手动取消。")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.pixelSize: 12
            color: Style.Color.textMuted
        }
    }

    // footer 由弹窗布局定位、不是锚定项：承载项 + 有效锚定边距提供 16px 内边距
    footer: Item {
        implicitHeight: footerRow.implicitHeight + Style.Space.lg * 2

        RowLayout {
            id: footerRow
            anchors.fill: parent
            anchors.margins: Style.Space.lg
            spacing: 10

            Item {
                Layout.fillWidth: true
            }

            // 继续传输的主路径用主按钮强调（与关闭确认弹窗同口径），高度与字号
            // 钉进按钮族：32px + Type.body
            Button {
                Material.elevation: 0
                text: qsTr("使用中继")
                Accessible.name: text  // 读屏名：QQC2 内建只给 role 不给 name
                highlighted: true
                implicitHeight: 32
                font.pixelSize: Style.Type.body
                onClicked: {
                    relayConfirmDialog.relayChosen(relayConfirmDialog.sessionId)
                    relayConfirmDialog.close()
                }
            }

            SecondaryButton {
                text: qsTr("取消传输")
                Accessible.name: text  // 读屏名：QQC2 内建只给 role 不给 name
                onClicked: {
                    relayConfirmDialog.cancelChosen(relayConfirmDialog.sessionId)
                    relayConfirmDialog.close()
                }
            }
        }
    }
}
