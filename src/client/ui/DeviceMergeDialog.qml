/**
 * @file    DeviceMergeDialog.qml
 * @version 7.24.0
 * @date 2026-10-08
 * @author  GridYard Team
 * @brief   设备关联向导（治理设备 ID 变化）
 *
 * 对方重装或换机后新设备 ID 出现，旧条目成孤儿：在本机新条目上发起关联，
 * 两步选择旧设备并预览将迁移的管理标记与历史条数，确认后单事务合并。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style
import "../utils/FormatUtils.js" as FormatUtils

Dialog {
    id: mergeDialog

    title: qsTr("关联到已有设备")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(540, parent ? parent.width - 48 : 540)
    padding: Style.Space.lg

    // 发起关联的新设备（右键菜单打开时注入）
    property string _newDeviceId: ""
    property string _newDeviceName: ""
    // 向导状态：1 选旧设备，2 预览确认
    property int _step: 1
    property string _oldDeviceId: ""
    property string _oldDeviceName: ""
    property string _searchText: ""
    // 两边的历史条数（预览页展示）
    property var _oldCounts: null
    property var _newCounts: null
    property bool _includeHistory: true
    property string _resultText: ""
    property bool _resultError: false

    function openFor(deviceId: string, deviceName: string): void {
        _newDeviceId = deviceId
        _newDeviceName = deviceName
        _step = 1
        _oldDeviceId = ""
        _oldDeviceName = ""
        _searchText = ""
        _oldCounts = null
        _newCounts = null
        _includeHistory = true
        _resultText = ""
        open()
    }

    // 第一步候选列表：除自身外的全部设备（含最近见过与离线条目），按关键字过滤
    readonly property var _candidates: {
        const vm = AppController.peerDiscoveryViewModel
        const keyword = _searchText.trim().toLowerCase()
        const list = []
        const peers = vm.peers
        for (let i = 0; i < peers.length; i++) {
            const peer = peers[i]
            if (peer.deviceId === _newDeviceId) {
                continue
            }
            if (keyword.length > 0) {
                const name = String(peer.deviceName).toLowerCase()
                const alias = String(peer.alias).toLowerCase()
                const ip = String(peer.ipAddress).toLowerCase()
                if (name.indexOf(keyword) < 0 && alias.indexOf(keyword) < 0
                        && ip.indexOf(keyword) < 0) {
                    continue
                }
            }
            list.push(peer)
        }
        return list
    }

    Connections {
        target: AppController.peerDiscoveryViewModel

        function onDeviceHistoryCounted(deviceId: string, messages: int, transfers: int): void {
            const counts = { "messages": messages, "transfers": transfers }
            if (deviceId === mergeDialog._oldDeviceId) {
                mergeDialog._oldCounts = counts
            } else if (deviceId === mergeDialog._newDeviceId) {
                mergeDialog._newCounts = counts
            }
        }

        function onDeviceMerged(success: bool, newDeviceId: string, oldDeviceId: string): void {
            if (newDeviceId !== mergeDialog._newDeviceId
                    || oldDeviceId !== mergeDialog._oldDeviceId) {
                return
            }
            mergeDialog._resultError = !success
            mergeDialog._resultText = success
                                          ? qsTr("已关联：旧条目已并入本设备。")
                                          : qsTr("关联失败，本机数据未改动，请重试。")
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Style.Space.md

        // ===== 第一步：选择旧设备条目 =====
        ColumnLayout {
            Layout.fillWidth: true
            visible: mergeDialog._step === 1
            spacing: Style.Space.sm

            Label {
                Layout.fillWidth: true
                text: qsTr("对方重装或换机后会以新设备身份出现。选择这台设备（%1）之前的旧条目，把它的备注、好友等信息并过来。")
                      .arg(mergeDialog._newDeviceName)
                color: Style.Color.textSecondary
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            TextField {
                id: searchInput
                Layout.fillWidth: true
                placeholderText: qsTr("搜索设备名、备注或 IP")
                font.pixelSize: 13
                selectByMouse: true
                onTextChanged: mergeDialog._searchText = text
            }

            ListView {
                id: candidateList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(280, mergeDialog._candidates.length * 76 + 8)
                clip: true
                model: mergeDialog._candidates

                delegate: ItemDelegate {
                    id: candidateItem
                    width: candidateList.width
                    height: 68
                    highlighted: mergeDialog._oldDeviceId === modelData.deviceId

                    required property var modelData

                    background: Rectangle {
                        color: candidateItem.highlighted
                               ? Style.Color.primarySoft : (candidateItem.hovered
                               ? Style.Color.surfaceSoft : Style.Color.transparent)
                        radius: Style.Radius.sm
                    }

                    contentItem: RowLayout {
                        spacing: Style.Space.sm

                        Label {
                            text: FormatUtils.displayName(modelData.alias, modelData.deviceName)
                                  .charAt(0)
                            font.pixelSize: 15
                            font.bold: true
                            color: Style.Color.textOnAccent
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            background: Rectangle {
                                color: modelData.isOnline ? Style.Color.primary
                                                          : Style.Color.textWeak
                                radius: 18
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                text: FormatUtils.displayName(modelData.alias, modelData.deviceName)
                                font.pixelSize: 14
                                font.bold: true
                                color: Style.Color.textMain
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.ipAddress
                                font.pixelSize: 12
                                color: Style.Color.textSecondary
                                elide: Text.ElideRight
                            }
                        }

                        Label {
                            visible: modelData.favorite
                            text: "★"
                            font.pixelSize: 14
                            color: Style.Color.warning
                        }
                    }

                    onClicked: {
                        mergeDialog._oldDeviceId = modelData.deviceId
                        mergeDialog._oldDeviceName =
                                FormatUtils.displayName(modelData.alias, modelData.deviceName)
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: mergeDialog._candidates.length === 0
                text: qsTr("没有可选择的设备条目。")
                color: Style.Color.textWeak
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Style.Space.sm

                Button {
                    text: qsTr("取消")
                    flat: true
                    onClicked: mergeDialog.close()
                }

                Button {
                    highlighted: true
                    enabled: mergeDialog._oldDeviceId.length > 0
                    text: qsTr("下一步")
                    onClicked: {
                        mergeDialog._step = 2
                        mergeDialog._resultText = ""
                        AppController.peerDiscoveryViewModel.countDeviceHistory(
                                    mergeDialog._oldDeviceId)
                        AppController.peerDiscoveryViewModel.countDeviceHistory(
                                    mergeDialog._newDeviceId)
                    }
                }
            }
        }

        // ===== 第二步：预览与确认 =====
        ColumnLayout {
            Layout.fillWidth: true
            visible: mergeDialog._step === 2
            spacing: Style.Space.sm

            Label {
                Layout.fillWidth: true
                text: qsTr("把「%1」的信息并入「%2」：").arg(mergeDialog._oldDeviceName)
                      .arg(mergeDialog._newDeviceName)
                color: Style.Color.textMain
                font.pixelSize: 14
                font.bold: true
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: {
                    const info = AppController.peerDiscoveryViewModel.deviceById(
                                     mergeDialog._oldDeviceId)
                    const lines = []
                    lines.push(info.alias && info.alias.length > 0
                               ? qsTr("· 备注：%1").arg(info.alias) : qsTr("· 备注：无"))
                    lines.push(info.favorite ? qsTr("· 好友标记：是")
                                             : qsTr("· 好友标记：否"))
                    lines.push(info.pinned ? qsTr("· 置顶：是") : qsTr("· 置顶：否"))
                    lines.push(info.hidden ? qsTr("· 不显示标记：是") : qsTr("· 不显示标记：否"))
                    return lines.join("\n")
                }
                color: Style.Color.textSecondary
                font.pixelSize: 13
            }

            CheckBox {
                id: historyCheck
                checked: true
                text: {
                    const oldText = mergeDialog._oldCounts
                                    ? qsTr("%1 条聊天 / %2 条传输")
                                      .arg(mergeDialog._oldCounts.messages)
                                      .arg(mergeDialog._oldCounts.transfers)
                                    : qsTr("统计中…")
                    const newText = mergeDialog._newCounts
                                    ? qsTr("%1 条聊天 / %2 条传输")
                                      .arg(mergeDialog._newCounts.messages)
                                      .arg(mergeDialog._newCounts.transfers)
                                    : qsTr("统计中…")
                    return qsTr("同时迁移聊天与传输历史（旧：%1；新：%2；重复消息自动跳过）")
                           .arg(oldText).arg(newText)
                }
                font.pixelSize: 13
                onToggled: mergeDialog._includeHistory = checked
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("旧条目将在合并后移除；此操作不影响已接收的本地文件。")
                color: Style.Color.textWeak
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                visible: mergeDialog._resultText.length > 0
                text: mergeDialog._resultText
                color: mergeDialog._resultError ? Style.Color.error : Style.Color.success
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Style.Space.sm

                Button {
                    text: qsTr("上一步")
                    flat: true
                    enabled: mergeDialog._resultText.length === 0
                    onClicked: mergeDialog._step = 1
                }

                Button {
                    highlighted: true
                    enabled: mergeDialog._resultText.length === 0
                    text: qsTr("确认关联")
                    onClicked: AppController.peerDiscoveryViewModel.mergeDevice(
                                   mergeDialog._newDeviceId, mergeDialog._oldDeviceId,
                                   mergeDialog._includeHistory)
                }
            }
        }
    }
}
