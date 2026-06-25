/**
 * @file    TransferHistoryView.qml
 * @version 6.6.2
 * @date    2026-06-25
 * @author  GridYard Team
 * @brief   当前设备的传输历史视图
 *
 * 展示当前设备的传输历史筛选、刷新、删除和清空入口。
 *
 * Change Log:
 * [v6.6.2] GY   2026-06-25
 * * 补齐文件头注释，说明组件职责
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/FormatUtils.js" as FormatUtils
import "../utils/Style.js" as Style

Frame {
    id: root

    property string peerDeviceId: ""  // 可选：按设备 ID 筛选，空字符串表示全部设备
    property string selectedStatus: ""  // 可选：按状态筛选（completed/failed/cancelled/rejected）

    // 按当前筛选条件查询传输历史，每次筛选变化或手动刷新时调用
    function refresh(): void {
        AppController.historyController.queryTransfers({
            "peerDeviceId": root.peerDeviceId,
            "status": root.selectedStatus
        })
    }

    Component.onCompleted: refresh()  // 组件加载时自动查询第一页

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.Space.md

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("传输历史")
                font.pixelSize: 16
                font.bold: true
                color: Style.Color.textMain
            }

            Item { Layout.fillWidth: true }

            // 状态筛选下拉框：将显示文本映射为查询状态值
            ComboBox {
                id: statusFilter
                model: [qsTr("全部"), qsTr("完成"), qsTr("失败"), qsTr("取消"), qsTr("拒绝")]
                onActivated: {
                    root.selectedStatus = ["", "completed", "failed", "cancelled", "rejected"][currentIndex]
                    root.refresh()
                }
            }

            // 手动刷新按钮
            ToolButton {
                text: qsTr("刷新")
                onClicked: root.refresh()
            }

            // 清空全部历史按钮：弹出确认对话框
            ToolButton {
                text: qsTr("清空历史")
                onClicked: clearDialog.open()
            }
        }

        // 加载指示器：异步查询进行中时显示
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: AppController.historyController.loading
            visible: running
        }

        // 历史记录列表：绑定 HistoryController.transfers
        ListView {
            id: historyList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Style.Space.sm
            model: AppController.historyController.transfers

            delegate: Rectangle {
                required property string recordId
                required property string peerName
                required property int direction
                required property string displayName
                required property int fileCount
                required property var totalBytes
                required property string status
                required property string startedAt
                required property string errorMessage

                width: historyList.width
                implicitHeight: cardContent.implicitHeight + Style.Space.md * 2
                color: Style.Color.surface
                radius: Style.Radius.sm
                border.color: Style.Color.border

                ColumnLayout {
                    id: cardContent
                    anchors.fill: parent
                    anchors.margins: Style.Space.md
                    spacing: Style.Space.xs

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: "%1 · %2".arg(peerName).arg(direction === 1 ? qsTr("发送") : qsTr("接收"))
                            font.bold: true
                            color: Style.Color.textMain
                            elide: Text.ElideRight
                        }
                        ToolButton {
                            text: qsTr("删除")
                            onClicked: AppController.historyController.deleteTransfer(recordId)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: displayName
                        color: Style.Color.textSecondary
                        elide: Text.ElideRight
                    }
                    Label {
                        text: qsTr("%1 个文件 · %2 · %3").arg(fileCount)
                              .arg(FormatUtils.formatBytes(totalBytes))
                              .arg(status)
                        color: Style.Color.textMuted
                        font.pixelSize: 12
                    }
                    Label {
                        visible: errorMessage.length > 0
                        Layout.fillWidth: true
                        text: errorMessage
                        color: Style.Color.error
                        wrapMode: Text.WrapAnywhere
                        font.pixelSize: 12
                    }
                    Label {
                        text: FormatUtils.formatTime(startedAt)
                        color: Style.Color.textWeak
                        font.pixelSize: 11
                    }
                }
            }

            // 空列表提示：查询无结果且加载完成时显示
            Label {
                anchors.centerIn: parent
                visible: historyList.count === 0 && !AppController.historyController.loading
                text: qsTr("还没有传输历史")
                color: Style.Color.textWeak
            }
        }
    }

    // 清空确认弹窗：仅删除本地历史记录，不影响已接收文件或发送源文件
    Dialog {
        id: clearDialog
        modal: true
        title: qsTr("清空传输历史")
        standardButtons: Dialog.Cancel | Dialog.Ok
        anchors.centerIn: Overlay.overlay
        contentItem: Label {
            text: qsTr("仅删除本地历史记录，不会删除已接收文件或发送源文件。")
            wrapMode: Text.Wrap
        }
        onAccepted: AppController.historyController.clearAllTransfers()
    }
}
