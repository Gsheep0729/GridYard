/**
 * @file    TransferHistoryView.qml
 * @version 7.18.0
 * @date 2026-10-05
 * @author  GridYard Team
 * @brief   传输历史视图
 *
 * 展示传输历史筛选、刷新、删除和清空入口；通过 peerDeviceId
 * 筛选指定设备的历史，空字符串时显示全部设备。
 *
 * Change Log:
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
 * [v7.17.4] GY   2026-10-04
 * * 底部新增加载更多入口与已到底提示
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
 * [v7.15.19] GY   2026-10-04
 * * 版本头对齐到 v7.15.19
 * [v7.15.18] GY   2026-10-04
 * * 版本头对齐到 v7.15.18
 * [v7.15.17] GY   2026-10-04
 * * 版本头对齐到 v7.15.17
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
 * * 刷新图标改为自绘，悬停 tooltip 统一加延迟避免即时闪现
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.15.3] GY   2026-10-03
* * 状态筛选文案改调 FormatUtils 唯一词表
* [v7.11.0] GY   2026-10-02
 * * 状态列改为中文文案，筛选与设备变化后自动刷新，补齐加载与空状态
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

    // 按当前筛选条件查询传输历史第一页，每次筛选变化或手动刷新时调用
    function refresh(): void {
        AppController.historyController.queryTransfers({
            "peerDeviceId": root.peerDeviceId,
            "status": root.selectedStatus
        })
    }

    // 翻页加载更早的历史，游标与筛选条件由 HistoryController 依据当前列表推导
    function loadMore(): void {
        AppController.historyController.loadMoreTransfers({
            "peerDeviceId": root.peerDeviceId,
            "status": root.selectedStatus
        })
    }

    onPeerDeviceIdChanged: refresh()
    Component.onCompleted: refresh()  // 组件加载时自动查询第一页

    // 状态筛选映射：显示文本取自 FormatUtils 唯一词表 -> 查询状态值
    readonly property var _statusOptions: [
        { label: qsTr("全部"), value: "" },
        { label: FormatUtils.transferStatusText("completed"), value: "completed" },
        { label: FormatUtils.transferStatusText("failed"), value: "failed" },
        { label: FormatUtils.transferStatusText("cancelled"), value: "cancelled" },
        { label: FormatUtils.transferStatusText("rejected"), value: "rejected" }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.Space.md

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("传输历史")
                font.pixelSize: 14
                font.bold: true
                color: Style.Color.textMain
            }

            Item { Layout.fillWidth: true }

            // 状态筛选下拉框
            ComboBox {
                model: root._statusOptions
                textRole: "label"
                onActivated: {
                    root.selectedStatus = root._statusOptions[currentIndex].value
                    root.refresh()
                }
            }

            // 手动刷新按钮：图标自绘，不依赖系统图标主题
            ToolButton {
                id: refreshHistoryButton
                padding: 10
                contentItem: RefreshIcon {
                    iconColor: refreshHistoryButton.hovered
                               ? Style.Color.primary : Style.Color.textSecondary
                }
                ToolTip.text: qsTr("刷新历史列表")
                ToolTip.delay: 500
                ToolTip.visible: hovered
                onClicked: root.refresh()
            }

            // 清空全部历史按钮：弹出确认对话框
            ToolButton {
                text: qsTr("清空历史")
                ToolTip.delay: 500
                ToolTip.visible: hovered
                ToolTip.text: qsTr("删除全部传输历史记录，不影响文件")
                onClicked: clearDialog.open()
            }
        }

        // 历史记录列表：绑定 HistoryController.transfers
        ListView {
            id: historyList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Style.Space.sm
            model: AppController.historyController.transfers

            // 查询进行中时顶部显示加载行，避免用户误以为无数据
            header: Item {
                width: historyList.width
                height: AppController.historyController.loading ? 28 : 0
                visible: height > 0

                RowLayout {
                    anchors.centerIn: parent
                    spacing: Style.Space.sm

                    BusyIndicator {
                        implicitWidth: 16
                        implicitHeight: 16
                        running: AppController.historyController.loading
                    }

                    Label {
                        text: qsTr("正在加载...")
                        font.pixelSize: 12
                        color: Style.Color.textWeak
                    }
                }
            }

            // 底部翻页区：还有更早记录时给"加载更多"入口，取到底后显示提示；
            // 查询进行中收起（顶部已有加载指示），空列表不占位
            footer: Item {
                width: historyList.width
                height: historyList.count > 0 && !AppController.historyController.loading ? 44 : 0
                visible: height > 0

                Button {
                    anchors.centerIn: parent
                    visible: AppController.historyController.hasMoreTransfers
                    text: qsTr("加载更多")
                    flat: true
                    font.pixelSize: 13
                    onClicked: root.loadMore()
                }

                Label {
                    anchors.centerIn: parent
                    visible: !AppController.historyController.hasMoreTransfers
                    text: qsTr("已到底")
                    font.pixelSize: 12
                    color: Style.Color.textWeak
                }
            }

            delegate: Rectangle {
                id: historyCard

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
                color: Style.Color.surfaceSoft
                radius: Style.Radius.sm
                border.color: Style.Color.border

                ColumnLayout {
                    id: cardContent
                    anchors.fill: parent
                    anchors.margins: Style.Space.md
                    spacing: Style.Space.xs

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Style.Space.sm

                        // 方向标识：发送/接收用带色标签区分
                        Rectangle {
                            Layout.preferredWidth: directionTag.implicitWidth + 10
                            Layout.preferredHeight: directionTag.implicitHeight + 4
                            radius: Style.Radius.xs
                            color: historyCard.direction === 1
                                   ? Style.Color.primary : Style.Color.receiveAccent

                            Label {
                                id: directionTag
                                anchors.centerIn: parent
                                text: historyCard.direction === 1 ? qsTr("发送") : qsTr("接收")
                                font.pixelSize: 10
                                font.bold: true
                                color: Style.Color.textOnAccent
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: historyCard.peerName
                            font.bold: true
                            color: Style.Color.textMain
                            elide: Text.ElideRight
                        }

                        Label {
                            text: FormatUtils.formatTime(historyCard.startedAt)
                            font.pixelSize: 11
                            color: Style.Color.textWeak
                        }

                        ToolButton {
                            text: qsTr("删除")
                            ToolTip.text: qsTr("删除这条记录")
                            ToolTip.delay: 500
                            ToolTip.visible: hovered
                            onClicked: AppController.historyController.deleteTransfer(historyCard.recordId)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: historyCard.displayName
                        color: Style.Color.textSecondary
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Style.Space.sm

                        Label {
                            text: qsTr("%1 个文件 · %2").arg(historyCard.fileCount)
                                  .arg(FormatUtils.formatBytes(historyCard.totalBytes))
                            color: Style.Color.textMuted
                            font.pixelSize: 12
                        }

                        Label {
                            text: FormatUtils.transferStatusText(historyCard.status)
                            color: historyCard.status === "completed"
                                   ? Style.Color.success : Style.Color.error
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        visible: historyCard.errorMessage.length > 0
                        Layout.fillWidth: true
                        text: historyCard.errorMessage
                        color: Style.Color.error
                        wrapMode: Text.WrapAnywhere
                        font.pixelSize: 12
                    }
                }
            }

            // 空列表提示：查询无结果且加载完成时显示
            ColumnLayout {
                anchors.centerIn: parent
                visible: historyList.count === 0 && !AppController.historyController.loading
                spacing: Style.Space.xs

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.selectedStatus.length > 0
                          ? qsTr("没有符合筛选条件的历史") : qsTr("还没有传输历史")
                    color: Style.Color.textWeak
                    font.pixelSize: 14
                    font.bold: true
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("传输完成后会自动记录在这里")
                    color: Style.Color.textMuted
                    font.pixelSize: 12
                }
            }
        }
    }

    // 清空确认弹窗：仅删除本地历史记录，不影响已接收文件或发送源文件
    Dialog {
        id: clearDialog
        modal: true
        title: qsTr("清空传输历史")
        width: 380
        standardButtons: Dialog.Cancel | Dialog.Ok
        anchors.centerIn: Overlay.overlay
        contentItem: Label {
            text: qsTr("仅删除本地历史记录，不会删除已接收文件或发送源文件。")
            wrapMode: Text.Wrap
        }
        onAccepted: AppController.historyController.clearAllTransfers()
    }
}
