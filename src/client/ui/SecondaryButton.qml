/**
 * @file    SecondaryButton.qml
 * @version 8.2.1
 * @date 2026-10-30
 * @author  GridYard Team
 * @brief   次要操作按钮
 *
 * 白底细边的胶囊按钮，悬停与按下用主色浅底反馈；禁用态收起底色只留灰字，
 * 与可点状态一眼可分。用于弹窗中非最高优先级的动作，替代 Material 未高亮
 * 按钮的灰底默认皮肤，避免"能点/不能点"长成同一族。
 */

import QtQuick
import QtQuick.Controls
import "../utils/Style.js" as Style

Button {
    id: secondaryButton

    font.pixelSize: 13  // 全族统一字号，调用处不再各自覆写

    contentItem: Label {
        text: secondaryButton.text
        font: secondaryButton.font
        color: secondaryButton.enabled ? Style.Color.textMain : Style.Color.textWeak
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitHeight: 32
        radius: height / 2
        color: !secondaryButton.enabled ? Style.Color.transparent
               : (secondaryButton.down ? Style.Color.primarySoftHover
                  : (secondaryButton.hovered ? Style.Color.primarySoft
                                             : Style.Color.window))
        border.width: secondaryButton.enabled ? 1 : 0
        border.color: Style.Color.border
    }
}
