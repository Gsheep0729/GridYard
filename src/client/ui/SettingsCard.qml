/**
 * @file    SettingsCard.qml
 * @version 7.19.0
 * @date 2026-10-05
 * @author  GridYard Team
 * @brief   设置对话框的分组卡片外壳
 *
 * 统一承载设置分组的底色、圆角、边框与内边距，卡片内容通过默认属性
 * 注入内部列布局，卡片高度随内容自适应。
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "../utils/Style.js" as Style

Rectangle {
    id: settingsCard

    default property alias content: contentLayout.data  // 卡片内容行

    Layout.fillWidth: true
    implicitHeight: contentLayout.implicitHeight + Style.Space.lg * 2
    color: Style.Color.surface
    radius: Style.Radius.lg
    border.color: Style.Color.border
    border.width: 1

    ColumnLayout {
        id: contentLayout

        anchors.fill: parent
        anchors.margins: Style.Space.lg
        spacing: Style.Space.md
    }
}
