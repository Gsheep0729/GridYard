/**
 * @file    MultiTargetDialog.qml
 * @version 7.25.0
 * @date 2026-10-08
 * @author  GridYard Team
 * @brief   多选群发目标选择弹窗
 *
 * 列出在线设备（好友置顶、含搜索框），复选目标后逐台发起独立 1:1 传输；
 * 上次勾选的设备集合被记住，下次打开默认勾上仍在线的部分。内容源支持
 * 既有文件/文件夹选择器，也支持直接粘贴路径。
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import cqnu.gridyard.client 1.0
import "../utils/Style.js" as Style
import "../utils/FormatUtils.js" as FormatUtils

Dialog {
    id: multiDialog

    title: qsTr("发送给多台设备")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(540, parent ? parent.width - 48 : 540)
    padding: Style.Space.lg

    // 待发送的路径：经文件/文件夹选择器注入，或在弹窗内直接输入
    property string _filePath: ""
    // deviceId -> 是否勾选
    property var _checked: ({})
    property string _searchText: ""

    function openFor(path: string): void {
        _filePath = path
        _checked = {}
        _searchText = ""
        // 记住上次选择：默认勾上仍在线的设备，内容路径预填上次值
        const lastSet = AppController.transferController.lastMultiTargets()
        const peers = AppController.peerDiscoveryViewModel.peers
        for (let i = 0; i < peers.length; i++) {
            if (peers[i].isOnline && lastSet.includes(peers[i].deviceId)) {
                _checked[peers[i].deviceId] = true
            }
        }
        _checked = Object.assign({}, _checked)  // 触发依赖更新
        manualPathInput.text = AppController.transferController.lastMultiPath()
        open()
    }

    readonly property int _checkedCount: Object.keys(_checked).filter(k => _checked[k]).length

    // 候选列表：在线设备（视图模型顺序已是好友置顶），按关键字过滤
    readonly property var _candidates: {
        const vm = AppController.peerDiscoveryViewModel
        const keyword = _searchText.trim().toLowerCase()
        const list = []
        const peers = vm.peers
        for (let i = 0; i < peers.length; i++) {
            const peer = peers[i]
            if (!peer.isOnline) {
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

    ColumnLayout {
        width: parent.width
        spacing: Style.Space.md

        // ===== 内容源区：未确定路径时展示 =====
        ColumnLayout {
            Layout.fillWidth: true
            visible: multiDialog._filePath.length === 0
            spacing: Style.Space.sm

            Label {
                Layout.fillWidth: true
                text: qsTr("选择要群发的文件或文件夹：")
                color: Style.Color.textSecondary
                font.pixelSize: 13
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.Space.sm

                Button {
                    text: qsTr("选择文件…")
                    onClicked: multiInnerFileDialog.open()
                }

                Button {
                    text: qsTr("选择文件夹…")
                    onClicked: multiInnerFolderDialog.open()
                }
            }

            TextField {
                id: manualPathInput
                Layout.fillWidth: true
                placeholderText: qsTr("或直接粘贴文件/文件夹路径")
                font.pixelSize: 12
                selectByMouse: true
            }

            Button {
                id: confirmContentButton
                Layout.alignment: Qt.AlignRight
                enabled: manualPathInput.text.trim().length > 0
                text: qsTr("确定内容")
                onClicked: {
                    const raw = manualPathInput.text.trim()
                    const path = raw.indexOf("file://") === 0
                                 ? decodeURIComponent(raw.substring(7)) : raw
                    multiDialog._filePath = path
                }
            }
        }

        // ===== 已确定内容：文件名 + 目标列表 =====
        Label {
            Layout.fillWidth: true
            visible: multiDialog._filePath.length > 0
            text: qsTr("把「%1」发给选中的设备：每台都会收到一次普通的传输请求，互相不知道还有谁收到，可逐台查看进度与结果。")
                  .arg(multiDialog._filePath.split("/").pop())
            color: Style.Color.textSecondary
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        TextField {
            id: searchInput
            Layout.fillWidth: true
            visible: multiDialog._filePath.length > 0
            placeholderText: qsTr("搜索在线设备")
            font.pixelSize: 13
            selectByMouse: true
            onTextChanged: multiDialog._searchText = text
        }

        ListView {
            id: candidateList
            Layout.fillWidth: true
            visible: multiDialog._filePath.length > 0
            Layout.preferredHeight: Math.min(300, Math.max(multiDialog._candidates.length * 62 + 8, 62))
            clip: true
            model: multiDialog._candidates

            delegate: ItemDelegate {
                id: candidateItem
                width: candidateList.width
                height: 58

                required property var modelData

                background: Rectangle {
                    color: candidateItem.hovered ? Style.Color.surfaceSoft
                                                 : Style.Color.transparent
                    radius: Style.Radius.sm
                }

                contentItem: RowLayout {
                    spacing: Style.Space.sm

                    CheckBox {
                        checked: multiDialog._checked[modelData.deviceId] === true
                        onToggled: {
                            if (checked) {
                                multiDialog._checked[modelData.deviceId] = true
                            } else {
                                delete multiDialog._checked[modelData.deviceId]
                            }
                            multiDialog._checked = Object.assign({}, multiDialog._checked)
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
                    // 点整行切换勾选，与复选框行为一致
                    const id = candidateItem.modelData.deviceId
                    if (multiDialog._checked[id] === true) {
                        delete multiDialog._checked[id]
                    } else {
                        multiDialog._checked[id] = true
                    }
                    multiDialog._checked = Object.assign({}, multiDialog._checked)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: multiDialog._filePath.length > 0 && multiDialog._candidates.length === 0
            text: qsTr("没有在线设备。群发只发给当前在线的设备，对方上线后可单独发送。")
            color: Style.Color.textWeak
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Style.Space.sm

            Button {
                text: qsTr("取消")
                flat: true
                onClicked: multiDialog.close()
            }

            Button {
                highlighted: true
                enabled: multiDialog._filePath.length > 0 && multiDialog._checkedCount > 0
                text: qsTr("发送（%1）").arg(multiDialog._checkedCount)
                onClicked: {
                    const targets = Object.keys(multiDialog._checked)
                                        .filter(k => multiDialog._checked[k])
                    AppController.transferController.saveMultiTargets(targets)
                    AppController.transferController.createMultiSendSessions(
                                targets, multiDialog._filePath)
                    mainWindow.showSuccessToast(qsTr("已向 %1 台设备发起传输").arg(targets.length))
                    multiDialog.close()
                }
            }
        }
    }

    FileDialog {
        id: multiInnerFileDialog
        title: qsTr("选择要群发的文件")
        fileMode: FileDialog.OpenFile
        onAccepted: {
            multiDialog._filePath = FormatUtils.localPathFromUrl(selectedFile)
        }
    }

    FolderDialog {
        id: multiInnerFolderDialog
        title: qsTr("选择要群发的文件夹")
        onAccepted: {
            multiDialog._filePath = FormatUtils.localPathFromUrl(selectedFolder)
        }
    }
}
