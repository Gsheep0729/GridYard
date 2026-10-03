/**
 * @file    DialogHeader.qml
 * @version 7.15.3
 * @date    2026-10-03
 * @author  GridYard Team
 * @brief   弹窗页眉
 *
 * 标题 + 副标题 + 关闭按钮 + 分隔线的标准页眉，供各对话框作为
 * header 使用；关闭按钮只发 closeClicked 信号，由使用方决定关闭哪个弹窗。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../utils/Style.js" as Style

ColumnLayout {
    id: dialogHeader

    property string title: ""
    property string subtitle: ""
    signal closeClicked()

    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: Style.Space.lg
        spacing: Style.Space.md

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: dialogHeader.title
                font.pixelSize: 17
                font.bold: true
                color: Style.Color.textMain
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                visible: dialogHeader.subtitle.length > 0
                text: dialogHeader.subtitle
                font.pixelSize: 12
                color: Style.Color.textMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        // 文字关闭按钮：不依赖系统图标主题，避免缺图标时渲染异常
        ToolButton {
            text: "✕"
            font.pixelSize: 14
            ToolTip.text: qsTr("关闭")
            ToolTip.visible: hovered
            onClicked: dialogHeader.closeClicked()
        }
    }

    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: Style.Color.border
    }
}
