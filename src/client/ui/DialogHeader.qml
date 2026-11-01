/**
 * @file    DialogHeader.qml
 * @version 8.2.1
 * @date 2026-11-02
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

        // 关闭按钮：符号改用内置 SVG 图标（U2-29），不依赖系统图标主题
        ToolButton {
            // 读屏名：可见内容是关闭符号图标，读屏按"关闭"播报
            Accessible.name: qsTr("关闭")
            ToolTip.text: qsTr("关闭")
            ToolTip.delay: 500
            ToolTip.visible: hovered
            onClicked: dialogHeader.closeClicked()
            contentItem: GlyphIcon {
                glyph: "../icons/icon-close.svg"
                iconColor: Style.Color.textMain
                implicitWidth: 16
                implicitHeight: 16
            }
        }
    }

    // 分隔线左右内缩（U2-32）：与页眉内容同边距收线，不再从圆角左缘
    // 满幅画到右缘，消除线与圆角弧相交的硬拐点
    Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: Style.Space.lg
        Layout.rightMargin: Style.Space.lg
        height: 1
        color: Style.Color.border
    }
}
