/**
 * @file    RefreshIcon.qml
 * @version 7.15.16
 * @date    2026-10-04
 * @author  GridYard Team
 * @brief   自绘刷新图标
 *
 * 用 Canvas 画一段带箭头的圆弧替代 view-refresh 主题图标；
 * 用户主题缺 glyph 时图标库会渲染成实心色块，自绘后不再
 * 依赖系统图标主题。
 *
 * Change Log:
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
 * * 初始版本：自绘刷新图标，替代主题 view-refresh 图标
 */

import QtQuick
import "../utils/Style.js" as Style

Canvas {
    id: refreshIcon

    property color iconColor: Style.Color.textSecondary
    property real strokeWeight: 1.6  // 弧线宽度，随尺寸微调时可覆写

    implicitWidth: 16
    implicitHeight: 16
    antialiasing: true

    onIconColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.lineWidth = refreshIcon.strokeWeight
        ctx.strokeStyle = refreshIcon.iconColor
        ctx.fillStyle = refreshIcon.iconColor
        ctx.lineCap = "round"

        const cx = width / 2
        const cy = height / 2
        const r = Math.min(width, height) / 2 - strokeWeight
        if (r <= 0) {
            return
        }

        // 圆弧：顶部留约 50 度缺口，顺时针扫一圈
        const start = -Math.PI / 2 + 0.45
        const end = start + Math.PI * 2 - 0.9
        ctx.beginPath()
        ctx.arc(cx, cy, r, start, end)
        ctx.stroke()

        // 箭头：落在弧线终点，沿切线方向（顺时针）指向前方
        const tipX = cx + r * Math.cos(end)
        const tipY = cy + r * Math.sin(end)
        const tangentX = -Math.sin(end)
        const tangentY = Math.cos(end)
        const sideX = -tangentY
        const sideY = tangentX
        const headLen = strokeWeight + 3.2
        const headHalf = strokeWeight + 1.8
        ctx.beginPath()
        ctx.moveTo(tipX + tangentX * headLen, tipY + tangentY * headLen)
        ctx.lineTo(tipX + sideX * headHalf, tipY + sideY * headHalf)
        ctx.lineTo(tipX - sideX * headHalf, tipY - sideY * headHalf)
        ctx.closePath()
        ctx.fill()
    }
}
