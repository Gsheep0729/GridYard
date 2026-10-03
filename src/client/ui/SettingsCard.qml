/**
 * @file    SettingsCard.qml
 * @version 7.15.13
 * @date    2026-10-04
 * @author  GridYard Team
 * @brief   设置对话框的分组卡片外壳
 *
 * 统一承载设置分组的底色、圆角、边框与内边距，卡片内容通过默认属性
 * 注入内部列布局，卡片高度随内容自适应。
 *
 * Change Log:
 * [v7.15.13] GY   2026-10-04
 * * 版本头对齐到 v7.15.13
 * [v7.15.12] GY   2026-10-03
 * 版本头对齐到 v7.15.12
 * [v7.15.11] GY   2026-10-03
 * * 版本头对齐到 v7.15.11
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.13.0] GY   2026-10-02
 * * 抽取设置对话框六张卡片的重复外壳
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
