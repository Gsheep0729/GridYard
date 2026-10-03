/**
 * @file    DangerButton.qml
 * @version 7.15.6
 * @date    2026-10-03
 * @author  GridYard Team
 * @brief   危险操作按钮
 *
 * 红底反白的三态按钮，颜色取自 Style 语义色 error 系列，
 * 供删除确认、清除缓存等不可逆操作使用，避免各处手写红按钮样式。
 *
 * Change Log:
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 */

import QtQuick
import QtQuick.Controls
import "../utils/Style.js" as Style

Button {
    id: dangerButton

    contentItem: Label {
        text: dangerButton.text
        font: dangerButton.font
        color: Style.Color.textOnAccent
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 88
        implicitHeight: 32
        radius: Style.Radius.xs
        color: dangerButton.down ? Style.Color.errorPressed
                                 : (dangerButton.hovered ? Style.Color.errorHover
                                                         : Style.Color.error)
    }
}
