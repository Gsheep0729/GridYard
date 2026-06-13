.pragma library

/**
 * @file    FormatUtils.js
 * @version 4.12.0
 * @date    2026-06-14
 * @author  GridYard Team
 * @brief   界面展示格式化工具
 *
 * Change Log:
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
