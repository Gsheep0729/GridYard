/**
 * @file    DialogHeader.qml
 * @version 7.17.2
 * @date    2026-10-04
 * @author  GridYard Team
 * @brief   弹窗页眉
 *
 * 标题 + 副标题 + 关闭按钮 + 分隔线的标准页眉，供各对话框作为
 * header 使用；关闭按钮只发 closeClicked 信号，由使用方决定关闭哪个弹窗。
 *
 * Change Log:
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
 * * 版本头对齐到 v7.15.18
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
 * * 关闭按钮 tooltip 加延迟，避免鼠标扫过时即时闪现
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
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
            ToolTip.delay: 500
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
