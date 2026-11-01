/**
 * @file    AppSwitch.qml
 * @version 8.2.1
 * @date 2026-11-02
 * @author  GridYard Team
 * @brief   统一开关控件
 *
 * 收编 Material 原生 Switch：关闭态轨道由淡紫 #E7E0EC 换为 border 冷灰、
 * 开启态轨道走品牌蓝，白圆点带描边滑移（U2-09）。只覆写 indicator 外观，
 * checked/onToggled 契约与可访问性角色不动。
 */

import QtQuick
import QtQuick.Controls
import "../utils/Style.js" as Style

Switch {
    id: control

    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 20
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? Style.Color.primary : Style.Color.border

        Behavior on color {
            ColorAnimation { duration: Style.Motion.base }
        }

        Rectangle {
            width: 16
            height: 16
            radius: 8
            anchors.verticalCenter: parent.verticalCenter
            x: control.checked ? parent.width - width - 2 : 2
            color: Style.Color.textOnAccent
            border.color: Style.Color.switchKnobBorder
            border.width: 1

            Behavior on x {
                NumberAnimation {
                    duration: Style.Motion.base
                    easing.type: Easing.OutCubic
                }
            }
        }
    }
}
