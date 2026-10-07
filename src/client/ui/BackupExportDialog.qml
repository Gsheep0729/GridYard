/**
 * @file    BackupExportDialog.qml
 * @version 7.23.0
 * @date 2026-10-07
 * @author  GridYard Team
 * @brief   用户数据导出对话框（备份与迁移）
 *
 * 按层勾选要写出的数据（设备身份 / 设备关系配置 / 聊天记录 / 传输历史），
 * 历史层打开时即显示条目数预估；保存路径默认指向文档目录，可经文件对话框修改。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style

Dialog {
    id: exportDialog

    title: qsTr("导出数据")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, parent ? parent.width - 48 : 520)
    padding: Style.Space.lg

    // 勾选状态：身份与设备关系默认勾选，历史层默认不勾（体积可能大）
    property bool _includeIdentity: true
    property bool _includeDevices: true
    property bool _includeChat: false
    property bool _includeTransfers: false
    // 导出数据就绪后的各层条目数与保存路径
    property var _summary: null
    property string _savePath: ""
    property string _resultText: ""
    property bool _resultError: false

    Connections {
        target: AppController.backupController

        function onExportPrepared(summary: var): void {
            exportDialog._summary = summary
            exportDialog._savePath = summary.defaultPath
        }

        function onExportFinished(success: bool, path: string, error: string): void {
            exportDialog._resultError = !success
            exportDialog._resultText = success
                                          ? qsTr("已导出到 %1").arg(path)
                                          : error
        }
    }

    onOpened: {
        _resultText = ""
        _summary = null
        AppController.backupController.prepareExport()
    }

    ColumnLayout {
        width: parent.width
        spacing: Style.Space.md

        Label {
            Layout.fillWidth: true
            text: qsTr("导出为明文 JSON 备份包，可在重装或换机后导入恢复。勾选要包含的数据层：")
            color: Style.Color.textSecondary
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        CheckBox {
            checked: true
            text: qsTr("设备身份（本机设备 ID 与设备名）")
            onToggled: exportDialog._includeIdentity = checked
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Style.Space.xl
            text: qsTr("此文件包含你的设备身份，请勿外传；泄露可被他人冒充。")
            color: Style.Color.warning
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }

        CheckBox {
            checked: true
            text: qsTr("设备关系与备注（好友、置顶、隐藏、备注，按设备）")
            onToggled: exportDialog._includeDevices = checked
        }

        CheckBox {
            enabled: exportDialog._summary !== null
            text: exportDialog._summary
                  ? qsTr("聊天记录（共 %1 条）").arg(exportDialog._summary.messages)
                  : qsTr("聊天记录（统计中…）")
            onToggled: exportDialog._includeChat = checked
        }

        CheckBox {
            enabled: exportDialog._summary !== null
            text: exportDialog._summary
                  ? qsTr("传输历史（共 %1 条）").arg(exportDialog._summary.transfers)
                  : qsTr("传输历史（统计中…）")
            onToggled: exportDialog._includeTransfers = checked
        }

        // 保存路径：默认文档目录，可手动编辑或经文件对话框选择
        Label {
            text: qsTr("保存位置")
            font.pixelSize: 13
            font.bold: true
            color: Style.Color.textSecondary
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.Space.sm

            TextField {
                id: pathField
                Layout.fillWidth: true
                text: exportDialog._savePath
                font.pixelSize: 12
                selectByMouse: true
                onTextChanged: exportDialog._savePath = text
            }

            Button {
                text: qsTr("浏览…")
                onClicked: saveFileDialog.open()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: exportDialog._resultText.length > 0
            text: exportDialog._resultText
            color: exportDialog._resultError ? Style.Color.error : Style.Color.success
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Style.Space.sm

            Button {
                text: qsTr("关闭")
                flat: true
                onClicked: exportDialog.close()
            }

            Button {
                highlighted: true
                enabled: exportDialog._summary !== null
                         && !AppController.backupController.busy
                         && exportDialog._savePath.trim().length > 0
                text: qsTr("导出")
                onClicked: AppController.backupController.writeExportFile(
                               exportDialog._savePath.trim(),
                               exportDialog._includeIdentity,
                               exportDialog._includeChat,
                               exportDialog._includeTransfers)
            }
        }
    }

    // 保存位置对话框：选中文件回填路径输入框
    FileDialog {
        id: saveFileDialog
        title: qsTr("选择备份保存位置")
        fileMode: FileDialog.SaveFile
        currentFile: "file://" + exportDialog._savePath
        onAccepted: pathField.text = FormatUtils.localPathFromUrl(selectedFile)
    }
}
