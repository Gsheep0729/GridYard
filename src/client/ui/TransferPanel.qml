/**
 * @file    TransferPanel.qml
 * @version 6.6.2
 * @date    2026-06-18
 * @author  GridYard Team
 * @brief   传输面板
 *
 * 显示所有进行中的传输任务，每个任务显示进度条、速度、取消按钮。
 * 绑定 TransferSessionManager.sessions。
 * Main.qml 已迁移到 DeviceSessionView 内嵌 ListView，本组件保留作为独立
 * 传输面板的备选实现，便于后续在多窗口或调试场景复用。
 *
 * Change Log:
 * [v6.6.2] GY   2026-06-25
 * * 同步文件头版本与当前主版本
 * [v4.16.0] DuRuoxian   2026-06-18
 * * 同步接入 Style.js 样式常量，统一颜色 / 圆角 / 间距 / 动画时长
 * [v4.15.2] DuRuoxian   2026-06-17
 * * 统一清空记录确认弹窗样式和操作按钮层级
 * [v4.14.1] GY   2026-06-15
 * * 优化清空记录对话框布局和字体大小
 * [v4.14.0] GY   2026-06-15
 * * 增加清空传输记录及删除已接收本地文件选项
 * [v4.13.3] GY   2026-06-15
 * * 按会话保存文件夹根目录预览的展开状态
 * [v4.13.1] DuRuoxian   2026-06-15
 * * 传递 isDirectory 和 fileList 属性到任务卡片
 * [v4.10.1] DuRuoxian   2026-06-13
 * * 修复会话字段绑定
 * [v0.2.0] DuRuoxian   2026-06-02
 * * Stage 3：初始版本
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Frame {
    id: transferPanel

    property var expandedSessions: ({})  // 记录文件夹展开状态的字典，键为 sessionId
    property int viewMode: 0  // 0=任务列表, 1=历史视图

    function isSessionExpanded(sessionId: string): bool {
        return expandedSessions[sessionId] === true
    }

    // 使用 Object.assign 浅拷贝触发 QML 属性绑定更新
    function setSessionExpanded(sessionId: string, expanded: bool): void {
        const next = Object.assign({}, expandedSessions)
        next[sessionId] = expanded
        expandedSessions = next
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 标题栏
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Style.Space.md

            Label {
                text: qsTr("传输任务")
                font.pixelSize: 16
                font.bold: true
                color: Style.Color.textMain
            }

            Item { Layout.fillWidth: true }

            // 清空记录按钮：弹出下拉菜单选择仅清空或同时删除本地文件
            ToolButton {
                icon.name: "edit-clear-all-symbolic"
                text: qsTr("清空记录")
                display: AbstractButton.TextBesideIcon
                onClicked: clearMenu.open()

                contentItem: Label {
                    text: parent.text
                    font: parent.font
                    color: parent.down ? Style.Color.primaryPressed
                                       : (parent.hovered ? Style.Color.primary : Style.Color.textSecondary)
                }

                background: Rectangle {
                    implicitWidth: 64
                    implicitHeight: 32
                    color: parent.down ? Style.Color.border
                                       : (parent.hovered ? Style.Color.surfaceSoft : Style.Color.transparent)
                    radius: Style.Radius.sm

                    Behavior on color {
                        ColorAnimation { duration: Style.Motion.base; easing.type: Easing.OutCubic }
                    }
                }

                Menu {
                    id: clearMenu
                    y: parent.height

                    MenuItem {
                        text: qsTr("清空已结束记录")
                        onTriggered: AppController.transferController.clearFinishedSessions(false)
                    }

                    MenuItem {
                        text: qsTr("清空记录并删除已接收文件")
                        onTriggered: clearDeleteConfirmDialog.open()
                    }
                }
            }
        }

        TabBar {
            Layout.fillWidth: true
            currentIndex: transferPanel.viewMode
            onCurrentIndexChanged: transferPanel.viewMode = currentIndex
            TabButton { text: qsTr("任务") }  // 当前进行中的传输任务
            TabButton { text: qsTr("历史") }  // 已完成/失败的历史记录
        }

        // 任务列表：绑定 TransferController.sessions，仅 viewMode===0 时可见
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            visible: transferPanel.viewMode === 0
            spacing: Style.Space.sm

            model: AppController.transferController.sessions

            delegate: Item {
                id: sessionDelegate

                required property string sessionId
                required property string type
                required property string filePath
                required property string fileName
                required property string status
                required property int progress
                required property var bytesTransferred
                required property var totalBytes
                required property string createdAt
                required property string peerDeviceName
                required property bool isDirectory
                required property var fileList
                required property bool canDeleteLocalFile

                width: listView.width
                height: taskCard.height

                TransferTaskCard {
                    id: taskCard

                    width: sessionDelegate.width
                    sessionId: sessionDelegate.sessionId
                    taskType: sessionDelegate.type
                    taskName: sessionDelegate.fileName
                    isDirectory: sessionDelegate.isDirectory
                    fileList: sessionDelegate.fileList
                    canDeleteLocalFile: sessionDelegate.canDeleteLocalFile
                    status: sessionDelegate.status
                    progress: sessionDelegate.progress
                    bytesTransferred: sessionDelegate.bytesTransferred
                    totalBytes: sessionDelegate.totalBytes
                    createdAt: sessionDelegate.createdAt
                    peerDeviceName: sessionDelegate.peerDeviceName
                    expanded: transferPanel.isSessionExpanded(sessionDelegate.sessionId)
                    onExpansionRequested: function(expanded) {
                        transferPanel.setSessionExpanded(sessionDelegate.sessionId, expanded)
                    }
                }
            }

            // 空列表提示
            Label {
                anchors.centerIn: parent
                text: qsTr("暂无传输任务")
                color: Style.Color.textWeak
                font.pixelSize: 14
                visible: listView.count === 0
            }
        }

        // 历史视图：绑定 HistoryController，仅 viewMode===1 时可见
        TransferHistoryView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: transferPanel.viewMode === 1
        }
    }

    // 清空确认弹窗：警告用户操作不可撤销，确认后删除已接收本地文件
    Dialog {
        id: clearDeleteConfirmDialog
        title: qsTr("清空记录")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(400, parent.width - 32)
        padding: Style.Space.lg

        background: Rectangle {
            color: Style.Color.surface
            radius: Style.Radius.lg
            border.color: Style.Color.border
            border.width: 1
        }

        header: Label {
            text: clearDeleteConfirmDialog.title
            font.pixelSize: 16
            font.bold: true
            color: Style.Color.textMain
            padding: Style.Space.lg
            bottomPadding: 0
        }

        contentItem: ColumnLayout {
            spacing: Style.Space.md

            Label {
                Layout.fillWidth: true
                text: qsTr("确定清空已结束记录并删除已接收的本地文件吗？")
                wrapMode: Text.WordWrap
                font.pixelSize: 14
                color: Style.Color.textSecondary
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("发送方源文件不受影响，此操作不可撤销。")
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: Style.Color.textMuted
                font.italic: true
            }
        }

        footer: DialogButtonBox {
            background: Rectangle { color: Style.Color.transparent }
            alignment: Qt.AlignRight
            topPadding: Style.Space.xs
            bottomPadding: Style.Space.lg
            rightPadding: Style.Space.lg

            Button {
                text: qsTr("取消")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                flat: true
            }

            Button {
                text: qsTr("确认删除")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                highlighted: true
            }
        }

        onAccepted: AppController.transferController.clearFinishedSessions(true)
    }
}
