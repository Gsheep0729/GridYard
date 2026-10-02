.pragma library

/**
 * @file    FormatUtils.js
 * @version 7.11.0
 * @date    2026-10-02
 * @author  GY
 * @brief   界面展示格式化工具
 *
 * Change Log:
 * [v7.11.0] GY   2026-10-02
 * * 收拢拖拽与文件对话框共用的 URL 转路径逻辑，补充传输状态中文映射
 * [v6.6.2] GY   2026-06-25
 * * 同步文件头版本与当前主版本
 * [v4.12.0] DuRuoxian   2026-06-14
 * * 抽离文件大小和时间格式化逻辑
 */

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + " B"
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB"
    if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB"
    return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GB"
}

function formatTime(timeString) {
    if (!timeString) return ""
    const date = new Date(timeString)
    return date.toLocaleTimeString(Qt.locale(), "HH:mm")
}

// 将 FileDialog/FolderDialog 或拖拽返回的 URL 转成本地绝对路径
// URL 对中文/空格做 percent-encode，直接截断会残留编码字符，必须先 decode
function localPathFromUrl(fileUrl) {
    const text = fileUrl.toString()
    if (text.startsWith("file:///")) {
        const path = Qt.platform.os === "windows" ? text.substring(8) : text.substring(7)
        return decodeURIComponent(path)
    }
    if (text.startsWith("file://")) {
        return "//" + decodeURIComponent(text.substring(7))
    }
    return decodeURIComponent(text)
}

// 传输状态中文映射：内部状态字符串转用户可读文案
function transferStatusText(status) {
    switch (status) {
    case "completed":   return qsTr("已完成")
    case "failed":      return qsTr("失败")
    case "cancelled":   return qsTr("已取消")
    case "rejected":    return qsTr("已拒绝")
    default:            return status
    }
}
