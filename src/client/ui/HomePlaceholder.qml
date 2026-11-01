/**
 * @file    HomePlaceholder.qml
 * @version 8.1.0
 * @date 2026-11-02
 * @author  GridYard Team
 * @brief   未选中设备的空状态占位
 *
 * 会话区没有选中设备时展示的品牌占位与引导文案。
 * 从 Main.qml 拆出，纯视觉组件。
 */

import QtQuick
import QtQuick.Controls
import "../utils/Style.js" as Style

Item {
    id: homePlaceholder

    // 排版统一走 EmptyState（U2-15）：品牌字标走 icon 槽，title/desc 收敛到
    // 统一的 14 bold / 12 textMuted 档式
    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 420)
        spacing: Style.Space.lg
        icon: Label {
            text: "GridYard"
            color: Style.Color.primary
            font.pixelSize: Style.Type.display
            font.bold: true
            opacity: 0.28
        }
        title: qsTr("选择一台设备开始会话")
        desc: qsTr("发送文件，之后也会在这里查看聊天消息")
    }
}
