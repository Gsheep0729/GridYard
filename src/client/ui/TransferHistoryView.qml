import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/FormatUtils.js" as FormatUtils
import "../utils/Style.js" as Style

Frame {
    id: root

    property string peerDeviceId: ""
    property string selectedStatus: ""

    function refresh(): void {
        AppController.history.queryTransfers({
            "peerDeviceId": root.peerDeviceId,
            "status": root.selectedStatus
        })
    }

    Component.onCompleted: refresh()

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

            ComboBox {
                id: statusFilter
                model: [qsTr("全部"), qsTr("完成"), qsTr("失败"), qsTr("取消"), qsTr("拒绝")]
                onActivated: {
                    root.selectedStatus = ["", "completed", "failed", "cancelled", "rejected"][currentIndex]
                    root.refresh()
                }
            }

            ToolButton {
                text: qsTr("刷新")
                onClicked: root.refresh()
            }

            ToolButton {
                text: qsTr("清空历史")
                onClicked: clearDialog.open()
            }
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: AppController.history.loading
            visible: running
        }

        ListView {
            id: historyList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Style.Space.sm
            model: AppController.history.transfers

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
                            onClicked: AppController.history.deleteTransfer(recordId)
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

            Label {
                anchors.centerIn: parent
                visible: historyList.count === 0 && !AppController.history.loading
                text: qsTr("还没有传输历史")
                color: Style.Color.textWeak
            }
        }
    }

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
        onAccepted: AppController.history.clearAllTransfers()
    }
}
