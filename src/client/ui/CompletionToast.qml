/**
 * @file    CompletionToast.qml
 * @version 7.15.10
 * @date    2026-10-03
 * @author  GridYard Team
 * @brief   接收完成通知卡
 *
 * 非阻塞展示接收完成结果，提供打开所在位置的快捷操作，自动消失。
 * 从 Main.qml 拆出，暴露 openWith(fileName, filePath) 接口。
 *
 * Change Log:
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.15.4] GY   2026-10-03
 * * 自 Main.qml 拆出，暴露 openWith(fileName, filePath) 接口
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Popup {
    id: completionToast

    // 弹窗淡入动画时长（与拆出前一致）
    readonly property int kEnterDuration: 180
    property string _filePath: ""
    property string _fileName: ""

    function openWith(fileName, filePath) {
        completionToast._fileName = fileName
        completionToast._filePath = filePath
        completionToast.open()
    }

    x: parent ? parent.width - width - 24 : 0
    y: parent ? parent.height - height - 24 : 0
    width: Math.min(330, parent ? parent.width - 48 : 330)
    padding: 14
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation {
            property: "opacity"; from: 0; to: 1
            duration: completionToast.kEnterDuration
            easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"; from: 1; to: 0
            duration: Style.Motion.fast
        }
    }

    background: Rectangle {
        color: Style.Color.window
        radius: Style.Radius.md
        border.color: Style.Color.border
    }

    contentItem: ColumnLayout {
        spacing: Style.Space.xs

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.Space.sm

            Label {
                text: qsTr("接收完成")
                font.pixelSize: 14
                font.bold: true
                color: Style.Color.success
                Layout.fillWidth: true
            }

            ToolButton {
                text: "✕"
                font.pixelSize: 12
                onClicked: completionToast.close()
            }
        }

        Label {
            text: completionToast._fileName
            font.pixelSize: 13
            color: Style.Color.textMain
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("文件已保存到接收目录")
            font.pixelSize: 12
            color: Style.Color.textMuted
        }

        Button {
            text: qsTr("打开所在位置")
            highlighted: true
            Layout.alignment: Qt.AlignRight
            onClicked: {
                ConfigManager.openFolder(completionToast._filePath)
                completionToast.close()
            }
        }
    }

    Timer {
        interval: 6000
        running: completionToast.visible
        onTriggered: completionToast.close()
    }
}
