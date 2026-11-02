/**
 * @file    RelayConfirmDialog.qml
 * @version 8.3.0
 * @date 2026-11-02
 * @author  GridYard Team
 * @brief   中继确认弹窗
 *
 * 直连失败且策略为询问后中继时展示，用户选择只经信号回传，
 * 策略判断已在 TransferSessionManager 内完成，本组件为纯弹窗。
 * 从 Main.qml 拆出，暴露 openFor(sessionId, fileName, totalBytes, isDirectory)
 * 与两个选择信号；文件名/大小由调用方注入（U2-21），让用户在决定是否
 * 走中继前知道待传内容的体量，文件名为空时文件行整体隐藏（旧契约兼容）。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material  // 按钮投影全局取消（U2-28）：按钮 Material.elevation 置 0
import QtQuick.Layouts
import "../utils/FormatUtils.js" as FormatUtils
import "../utils/Style.js" as Style

Dialog {
    id: relayConfirmDialog

    title: qsTr("直连失败")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, parent ? parent.width - 48 : 420)
    // 弹窗族统一（U2-07）：自绘背景 + DialogHeader 页眉，内容区左右 20、上 12，
    // 与页眉边距衔接；页眉 ✕ 不代做选择——关窗后任务按既有文案约 2 分钟自动失败
    padding: 0
    topPadding: Style.Space.md
    leftPadding: Style.Space.xl
    rightPadding: Style.Space.xl

    background: Rectangle {
        color: Style.Color.window
        radius: Style.Radius.md
        border.color: Style.Color.border
    }

    header: DialogHeader {
        title: relayConfirmDialog.title
        onCloseClicked: relayConfirmDialog.close()
    }

    property string sessionId: ""

    // 待传文件上下文（U2-21）：由 openFor 注入。fileName 为空（旧契约调用或
    // 会话查不到）时文件行整体隐藏；totalBytes 未回填（<=0，目录体量统计在途）
    // 时只隐大小标签，避免闪现 "0 B"（同任务卡口径）
    property string fileName: ""
    property real totalBytes: 0
    property bool isDirectory: false

    // 用户选择经信号回传，由使用方调用控制器入口
    signal relayChosen(string sessionId)
    signal cancelChosen(string sessionId)

    // 新增入参用无类型默认值（QML 不支持默认参数带类型注解），旧调用
    // openFor(sessionId) 依旧成立：文件行为空即隐藏，契约向后兼容
    function openFor(sessionId: string, fileName = "", totalBytes = 0, isDirectory = false): void {
        relayConfirmDialog.sessionId = sessionId
        relayConfirmDialog.fileName = fileName
        relayConfirmDialog.totalBytes = totalBytes
        relayConfirmDialog.isDirectory = isDirectory
        relayConfirmDialog.open()
    }

    // contentItem 的几何由弹窗管理：不得再锚定 parent，否则与弹窗布局冲突，
    // 说明文字会逃逸内容区压到按钮行并左端裁切（审批 C-J03 A/B 定案）
    contentItem: ColumnLayout {
        spacing: 14

        Label {
            text: qsTr("与目标设备直连失败，是否通过中继服务器转发本次传输？转发速度可能受限于服务器带宽。")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // 文件信息卡片（U2-21）：与接收弹窗元信息行同口径（U2-34），长文件名
        // 由 ElideMiddle 截断；卡片成组并带读屏名，裸 Text 读屏不可达（本构建实测）
        Rectangle {
            visible: relayConfirmDialog.fileName.length > 0
            Layout.fillWidth: true
            implicitHeight: relayFileLayout.implicitHeight + Style.Space.md * 2
            color: Style.Color.surfaceSoft
            radius: Style.Radius.sm
            border.color: Style.Color.border
            border.width: 1

            Accessible.role: Accessible.Grouping
            Accessible.name: relayConfirmDialog.isDirectory
                  ? qsTr("文件夹名：%1，大小 %2")
                        .arg(relayConfirmDialog.fileName)
                        .arg(FormatUtils.formatBytes(relayConfirmDialog.totalBytes))
                  : qsTr("文件名：%1，大小 %2")
                        .arg(relayConfirmDialog.fileName)
                        .arg(FormatUtils.formatBytes(relayConfirmDialog.totalBytes))

            ColumnLayout {
                id: relayFileLayout
                anchors.fill: parent
                anchors.margins: Style.Space.md
                spacing: Style.Space.sm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.Space.sm

                    FileTypeIcon {
                        fileName: relayConfirmDialog.fileName
                        isDirectory: relayConfirmDialog.isDirectory
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                    }

                    Label {
                        text: relayConfirmDialog.fileName
                        font.pixelSize: Style.Type.body
                        font.bold: true
                        color: Style.Color.textMain
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Label {
                        visible: relayConfirmDialog.totalBytes > 0
                        text: FormatUtils.formatBytes(relayConfirmDialog.totalBytes)
                        font.pixelSize: Style.Type.caption
                        color: Style.Color.textMuted
                    }
                }
            }
        }

        Label {
            text: qsTr("暂不处理将在约 2 分钟后自动失败，也可以随时在任务卡片上手动取消。")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.pixelSize: 12
            color: Style.Color.textMuted
        }
    }

    // footer 由弹窗布局定位、不是锚定项：承载项 + 有效锚定边距提供 16px 内边距
    footer: Item {
        implicitHeight: footerRow.implicitHeight + Style.Space.lg * 2

        RowLayout {
            id: footerRow
            anchors.fill: parent
            anchors.margins: Style.Space.lg
            spacing: 10

            Item {
                Layout.fillWidth: true
            }

            // 继续传输的主路径用主按钮强调（与关闭确认弹窗同口径），高度与字号
            // 钉进按钮族：32px + Type.body
            Button {
                Material.elevation: 0
                text: qsTr("使用中继")
                Accessible.name: text  // 读屏名：QQC2 内建只给 role 不给 name
                highlighted: true
                implicitHeight: 32
                font.pixelSize: Style.Type.body
                onClicked: {
                    relayConfirmDialog.relayChosen(relayConfirmDialog.sessionId)
                    relayConfirmDialog.close()
                }
            }

            SecondaryButton {
                text: qsTr("取消传输")
                Accessible.name: text  // 读屏名：QQC2 内建只给 role 不给 name
                onClicked: {
                    relayConfirmDialog.cancelChosen(relayConfirmDialog.sessionId)
                    relayConfirmDialog.close()
                }
            }
        }
    }
}
