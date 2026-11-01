/**
 * @file    EmptyState.qml
 * @version 8.2.1
 * @date 2026-11-02
 * @author  GridYard Team
 * @brief   统一空态
 *
 * 五处空态（设备列表/会话页/传输历史/关联向导/主页占位）收敛为同一套语言：
 * icon(可选) / title 14 bold textMuted / desc 12 textMuted / action(可选)。
 * desc 之外的补充说明行经默认属性追加在其后，由调用方按同档式逐行给出。
 * 调用方必须给定宽度（显式 width 或 Layout.fillWidth）：子行宽度绑
 * parent.width，不给宽度空态会塌缩为 0×0。（U2-15）
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "../utils/Style.js" as Style

Column {
    id: emptyState

    property Component icon
    property string title: ""
    property string desc: ""
    property Component action
    // desc 之外的补充说明行：调用方直接写子 Label，落入 notesCol
    default property alias notes: notesCol.data

    spacing: Style.Space.sm

    Loader {
        id: iconLoader
        active: emptyState.icon !== null
        sourceComponent: emptyState.icon
        anchors.horizontalCenter: parent.horizontalCenter
    }

    Label {
        width: parent.width
        visible: emptyState.title.length > 0
        text: emptyState.title
        color: Style.Color.textMuted
        font.pixelSize: Style.Type.body
        font.bold: true
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }

    Label {
        width: parent.width
        visible: emptyState.desc.length > 0
        text: emptyState.desc
        color: Style.Color.textMuted
        font.pixelSize: Style.Type.caption
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }

    Column {
        id: notesCol
        width: parent.width
        spacing: Style.Space.xs
    }

    Loader {
        id: actionLoader
        active: emptyState.action !== null
        sourceComponent: emptyState.action
        anchors.horizontalCenter: parent.horizontalCenter
    }
}
