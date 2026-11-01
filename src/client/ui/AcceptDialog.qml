/**
 * @file    AcceptDialog.qml
 * @version 8.2.1
 * @date 2026-11-02
 * @author  GridYard Team
 * @brief   接收确认弹窗
 *
 * 显示发送方设备名、文件名、文件大小。
 * 用户点击"接受"或"拒绝"后调用 TransferSessionManager。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material  // 按钮投影全局取消（U2-28）：按钮 Material.elevation 置 0
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/FormatUtils.js" as FormatUtils
import "../utils/Style.js" as Style

Dialog {
    id: acceptDialog

    title: acceptDialog.isDirectory ? qsTr("接收文件夹") : qsTr("接收文件")
    modal: true
    anchors.centerIn: parent
    // 弹窗宽度三档（U2-31）：确认/提示类归窄档 420，元信息行 elide 兜长名
    width: 420

    // 会话信息（由 TransferSessionManager 的 receiveRequestReceived 信号填充）
    property string sessionId: ""
    property string senderName: ""
    property string fileName: ""
    property real   fileSize: 0
    property int    totalFiles: 1
    property real   totalBytes: 0
    property bool   isDirectory: false  // 为 true 时展示目录预览而非单文件大小
    property var    fileList: []  // 文件夹场景下的根目录条目预览列表

    // 会话已过期（发送方取消或超时）时通知主窗口提示
    signal transferStale()

    // 直接关闭（Esc 等）不作为答复：会话保持等待确认，由主窗口提示
    // 可从任务卡重新唤起确认弹窗
    signal requestPostponed(string sessionId)

    // 用接收请求的字段装配弹窗并打开（info 为请求信号的字段集合）
    function openWith(info: var): void {
        acceptDialog.sessionId = info.sessionId
        acceptDialog.senderName = info.senderName
        acceptDialog.fileName = info.fileName
        acceptDialog.fileSize = info.fileSize
        acceptDialog.totalFiles = info.totalFiles
        acceptDialog.totalBytes = info.totalBytes
        acceptDialog.isDirectory = info.isDirectory
        acceptDialog.fileList = info.fileList
        acceptDialog.open()
    }

    contentItem: ColumnLayout {
        spacing: Style.Space.lg

        // 读屏语义：弹窗本体不是 Item，Accessible 附挂不上（Qt 6.11.1 实测
        // 附挂只会告警且接口为空），由内容根承载 Dialog 角色；名字对齐标题，
        // 描述带发送方，读屏开场即可听到"来自谁"
        Accessible.role: Accessible.Dialog
        Accessible.name: acceptDialog.title
        Accessible.description: qsTr("来自 ") + acceptDialog.senderName
                                + qsTr(" 的传输请求")

        // 发送方提示
        Label {
            text: qsTr("来自 ") + acceptDialog.senderName + qsTr(" 的传输请求")
            font.pixelSize: 16
            font.bold: true
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Style.Color.textMain
        }

        // 文件信息卡片：元信息一行带类型图标（U2-34），文件夹场景补条目数
        // 与总大小，单文件场景只显示文件大小
        Rectangle {
            objectName: "acceptFileInfoCard"
            Layout.fillWidth: true
            implicitHeight: fileInfoLayout.implicitHeight + 24
            color: Style.Color.surfaceSoft
            radius: Style.Radius.sm
            border.color: Style.Color.border
            border.width: 1

            // 读屏语义：卡内行文本读屏不可达（本 Qt 构建裸 Text 无可访问接口），
            // 整卡成组并把决策所需关键字段并进读屏名
            Accessible.role: Accessible.Grouping
            Accessible.name: acceptDialog.isDirectory
                  ? qsTr("文件夹名：%1，共 %2 项，总大小 %3")
                        .arg(acceptDialog.fileName)
                        .arg(acceptDialog.totalFiles)
                        .arg(FormatUtils.formatBytes(acceptDialog.totalBytes))
                  : qsTr("文件名：%1，大小 %2")
                        .arg(acceptDialog.fileName)
                        .arg(FormatUtils.formatBytes(acceptDialog.fileSize))

            ColumnLayout {
                id: fileInfoLayout
                anchors.fill: parent
                anchors.margins: Style.Space.md
                spacing: Style.Space.sm

                // 元信息一行（U2-34）：图标 + 文件名 + 大小/条目，替代原四组
                // "键：值"行——单文件场景自此也出文件类型图标，与文件夹预览
                // 列表每条目带图标一致；长文件名由 ElideMiddle 截断
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.Space.sm

                    FileTypeIcon {
                        fileName: acceptDialog.fileName
                        isDirectory: acceptDialog.isDirectory
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                    }

                    Label {
                        text: acceptDialog.fileName
                        font.pixelSize: 14
                        font.bold: true
                        color: Style.Color.textMain
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Label {
                        text: acceptDialog.isDirectory
                              ? qsTr("%1 项").arg(acceptDialog.totalFiles) + " · "
                                + FormatUtils.formatBytes(acceptDialog.totalBytes)
                              : FormatUtils.formatBytes(acceptDialog.fileSize)
                        font.pixelSize: 12
                        color: Style.Color.textMuted
                    }
                }

                Label {
                    visible: acceptDialog.isDirectory
                    text: qsTr("文件夹内容：")
                    font.bold: true
                    color: Style.Color.textSecondary
                    font.pixelSize: 13
                    Layout.topMargin: Style.Space.xs
                }

                ListView {
                    Layout.fillWidth: true
                    // 限制预览高度避免弹窗过长，最多显示 180px 内的条目
                    Layout.preferredHeight: acceptDialog.isDirectory
                                            ? Math.min(contentHeight, 180) : 0
                    visible: acceptDialog.isDirectory
                    clip: true
                    spacing: Style.Space.xs
                    model: acceptDialog.fileList  // 根目录条目预览，由 TransferSessionManager 构建

                    delegate: RowLayout {
                        id: previewRow
                        required property string modelData
                        width: ListView.view.width
                        spacing: Style.Space.xs

                        FileTypeIcon {
                            fileName: previewRow.modelData
                            isDirectory: previewRow.modelData.endsWith("/")
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                        }

                        Label {
                            text: previewRow.modelData
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            color: Style.Color.textMuted
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        // 提示信息
        Label {
            objectName: "acceptQuestionText"
            text: acceptDialog.isDirectory
                  ? qsTr("是否接受此文件夹？")
                  : qsTr("是否接受此文件？")
            font.pixelSize: 14
            color: Style.Color.textSecondary
            Layout.alignment: Qt.AlignHCenter

            // 读屏语义：决策问句以静态文本可达
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    footer: DialogButtonBox {
        background: Rectangle { color: "transparent" }
        alignment: Qt.AlignRight
        topPadding: 4
        bottomPadding: 16
        rightPadding: 16

        // 拒绝按钮不挂 RejectRole：Dialog 的 rejected 信号同时承载 Esc 等
        // 直接关闭路径，两者语义必须分开（直接关闭按"稍后再说"处理）
        Button {
            text: qsTr("拒绝")
            Accessible.name: text  // 读屏名：QQC2 内建只给 role 不给 name
            flat: true
            onClicked: {
                AppController.transferController.rejectReceiveSession(acceptDialog.sessionId)
                acceptDialog.close()
            }
        }

        Button {
            id: acceptButton
            Material.elevation: 0
            text: qsTr("接受")
            Accessible.name: text  // 读屏名：QQC2 内建只给 role 不给 name
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            highlighted: true
        }
    }

    onAccepted: {
        // 用户接受
        AppController.transferController.acceptReceiveSession(sessionId)
    }

    onRejected: {
        // 直接关闭（Esc/宿主窗口隐藏）不算答复：会话保持等待确认，
        // 任务卡上的确认入口可重新唤起弹窗，避免误按直接拒绝对端
        acceptDialog.requestPostponed(acceptDialog.sessionId)
    }

    // 会话过期感知：等待确认的会话被后端终结（对方取消/超时/断连）时，
    // 接受/拒绝都会被静默忽略，因此收到 sessionStale 即关闭并提示用户
    Connections {
        target: AppController.transferController

        function onSessionStale(sessionId: string): void {
            if (!acceptDialog.opened || sessionId !== acceptDialog.sessionId) {
                return
            }
            acceptDialog.close()
            acceptDialog.transferStale()
        }
    }
}
