/**
 * @file    GlyphIcon.qml
 * @version 8.2.1
 * @date 2026-11-02
 * @author  GridYard Team
 * @brief   单色矢量符号图标
 *
 * 加载 icons/ 下的白色 16px 符号 SVG（U2-29 图标三源统一：文字符号当图标的
 * 替换件，形状统一由内置 SVG 承载），经 MultiEffect 全量着色为 iconColor。
 * 着色数学在 Qt 端是 gray × tint 的乘法混合（multieffect.frag 中
 * color.rgb = gray * colorizationColor.rgb * a + color.rgb * (1 - a)，
 * uniform a = colorizationColor.alphaF × colorization），白色源 gray 恒为 1，
 * 输出即纯 tint 色——换肤与深浅切换只需改 iconColor 绑定，无需多套色稿。
 */

import QtQuick
import QtQuick.Effects
import "../utils/Style.js" as Style

Item {
    id: glyphIcon

    // 符号 SVG（icons/ 下白色描边稿），如 "../icons/icon-close.svg"
    property url glyph: ""
    // 目标着色色：绑深浅双表即自动跟随系统深浅切换
    property color iconColor: Style.Color.textSecondary

    implicitWidth: 16
    implicitHeight: 16

    // 着色源：隐藏的白色符号位图，按显示尺寸重新栅格化保证锐利
    Image {
        id: glyphSource
        anchors.centerIn: parent
        width: glyphIcon.width
        height: glyphIcon.height
        source: glyphIcon.glyph
        sourceSize.width: width
        sourceSize.height: height
        fillMode: Image.Stretch
        visible: false
    }

    MultiEffect {
        anchors.fill: glyphSource
        source: glyphSource
        colorization: 1.0
        colorizationColor: glyphIcon.iconColor
    }
}
