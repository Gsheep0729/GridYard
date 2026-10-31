/**
 * @file    BackupImportDialog.qml
 * @version 7.23.0
 * @date 2026-10-07
 * @author  GridYard Team
 * @brief   用户数据导入对话框（备份与迁移）
 *
 * 选择备份文件后先解析出预览（各层条目数、与本机目录的冲突数、身份层动作），
 * 确认后在单事务内导入，任何一步失败整体回滚。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Dialog {
    id: importDialog

    title: qsTr("导入数据")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, parent ? parent.width - 48 : 520)
    padding: Style.Space.lg

    property string _filePath: ""
    property var _summary: null
    property string _resultText: ""
    property bool _resultError: false

    Connections {
        target: AppController.backupController

        function onImportAnalyzed(summary: var): void {
            importDialog._summary = summary
        }

        function onImportFinished(success: bool, summary: var, error: string): void {
            importDialog._resultError = !success
            if (!success) {
                importDialog._resultText = error
                return
            }
            // 设备目录已变化：刷新在线发现与本地历史，列表即时呈现导入结果
            AppController.peerDiscoveryViewModel.refresh()
            let text = qsTr("导入完成：设备 %1 条、聊天 %2 条（新增 %3）、传输 %4 条（新增 %5）。")
                .arg(summary.devices).arg(summary.messages).arg(summary.messagesAdded)
                .arg(summary.transfers).arg(summary.transfersAdded)
            if (summary.identityAction === "replace") {
                text += qsTr(" 设备身份已替换，重启后生效。")
            } else if (summary.identityAction === "reject") {
                text += qsTr(" 本机已有其他身份，备份中的设备身份未导入。")
            }
            importDialog._resultText = text
        }
    }

    onOpened: {
        _summary = null
        _resultText = ""
    }

    ColumnLayout {
        width: parent.width
        spacing: Style.Space.md

        Label {
            Layout.fillWidth: true
            text: qsTr("选择此前导出的备份文件，确认预览后导入。导入不会删除本机已有数据，重复导入自动去重。")
            color: Style.Color.textSecondary
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.Space.sm

            TextField {
                id: pathField
                Layout.fillWidth: true
                placeholderText: qsTr("备份文件路径")
                font.pixelSize: 12
                selectByMouse: true
                onTextChanged: {
                    importDialog._filePath = text
                    importDialog._summary = null
                }
            }

            Button {
                text: qsTr("浏览…")
                onClicked: openFileDialog.open()
            }
        }

        Button {
            Layout.alignment: Qt.AlignRight
            enabled: importDialog._filePath.trim().length > 0
                     && !AppController.backupController.busy
            text: qsTr("解析预览")
            onClicked: AppController.backupController.analyzeImportFile(
                           importDialog._filePath.trim())
        }

        // 预览区：各层条目数、冲突数与身份层动作
        ColumnLayout {
            Layout.fillWidth: true
            visible: importDialog._summary !== null
                     && importDialog._summary.error === undefined
            spacing: Style.Space.xs

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Style.Color.border
            }

            Label {
                text: importDialog._summary
                      ? qsTr("设备关系与备注：%1 条（其中 %2 台本机已有，将按字段合并）")
                        .arg(importDialog._summary.devices).arg(importDialog._summary.deviceConflicts)
                      : ""
                color: Style.Color.textMain
                font.pixelSize: 13
            }

            Label {
                text: importDialog._summary
                      ? qsTr("聊天记录：%1 条（其中 %2 条已存在，将自动跳过）")
                        .arg(importDialog._summary.messages).arg(importDialog._summary.messageConflicts)
                      : ""
                color: Style.Color.textMain
                font.pixelSize: 13
            }

            Label {
                text: importDialog._summary
                      ? qsTr("传输历史：%1 条（其中 %2 条已存在，将自动跳过）")
                        .arg(importDialog._summary.transfers).arg(importDialog._summary.transferConflicts)
                      : ""
                color: Style.Color.textMain
                font.pixelSize: 13
            }

            Label {
                Layout.fillWidth: true
                visible: importDialog._summary && importDialog._summary.invalid > 0
                text: importDialog._summary
                      ? qsTr("另有 %1 条格式异常的记录将被忽略。").arg(importDialog._summary.invalid)
                      : ""
                color: Style.Color.textWeak
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                visible: importDialog._summary && importDialog._summary.identityAction === "replace"
                text: qsTr("备份中的设备身份与本机不同，且本机尚无记录：导入后将替换本机身份，重启应用生效。")
                color: Style.Color.warning
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                visible: importDialog._summary && importDialog._summary.identityAction === "reject"
                text: qsTr("本机已有其他设备身份的使用记录，备份中的设备身份不会导入，其余数据正常导入。")
                color: Style.Color.textWeak
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }

        Label {
            Layout.fillWidth: true
            // error 键仅在解析失败时存在，取空串兜底避免 undefined 赋值告警
            readonly property string _parseError: importDialog._summary === null
                                                  ? "" : (importDialog._summary.error ?? "")
            visible: _parseError.length > 0
            text: _parseError
            color: Style.Color.error
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            visible: importDialog._resultText.length > 0
            text: importDialog._resultText
            color: importDialog._resultError ? Style.Color.error : Style.Color.success
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Style.Space.sm

            Button {
                text: qsTr("关闭")
                flat: true
                onClicked: importDialog.close()
            }

            Button {
                highlighted: true
                enabled: importDialog._summary !== null
                         && importDialog._summary.error === undefined
                         && !AppController.backupController.busy
                text: qsTr("确认导入")
                onClicked: AppController.backupController.performImport()
            }
        }
    }

    // 备份文件选择对话框
    FileDialog {
        id: openFileDialog
        title: qsTr("选择备份文件")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("GridYard 备份 (*.json)")]
        onAccepted: pathField.text = FormatUtils.localPathFromUrl(selectedFile)
    }
}
