/**
 * @file    FormatUtils.js
 * @version 7.20.3
 * @date 2026-10-05
 * @author  GY
 * @brief   界面展示格式化工具
 */

function formatBytes(bytes) {
    // 入参缺失或非数值时返回占位符：任务卡绑定始终求值，仅靠 visible 遮挡，
    // 切换设备/清理会话的瞬间会把 undefined 送进来，无守卫会闪现 "NaN GB"
    if (bytes === undefined || bytes === null || isNaN(bytes)) return "-"
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

// 日期与时刻一起展示（ISO 文本转本地时区），供已隐藏设备等列表使用
function formatDateTime(timeString) {
    if (!timeString) return ""
    const date = new Date(timeString)
    return date.toLocaleString(Qt.locale(), "yyyy-MM-dd HH:mm")
}

// 将 FileDialog/FolderDialog 或拖拽返回的 URL 转成本地绝对路径
// URL 对中文/空格做 percent-encode，直接截断会残留编码字符，必须先 decode
// 行为与 QUrl::toLocalFile 对齐（file:/// 三斜杠剥离、file:// 双斜杠保留为
// UNC 形态），保留在 JS 侧是拖拽/文件对话框的 URL 解码属纯表现层转换，
// Phase2-B 下沉时明确只把在线裁决收回 C++，此处为既定口径
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

// 设备显示名：本地备注优先于对方广播名（备注为空回落原名），
// 设备卡、会话页标题与通知等取设备名处统一使用
function displayName(alias, deviceName) {
    const remark = alias ? String(alias).trim() : ""
    if (remark.length > 0) {
        return remark
    }
    return deviceName ? String(deviceName) : ""
}

// 离线设备最后在线的相对描述：一分钟内"刚刚在线"，一小时内"N 分钟前在线"，
// 一天内"N 小时前在线"，更早回落到"MM-DD 在线"；入参空或无效返回空串。
// .pragma library 中 qsTr 不可靠（项目无翻译场景），直接返回中文
function relativeSeen(lastSeenAtIso) {
    if (!lastSeenAtIso) {
        return ""
    }
    const seen = new Date(lastSeenAtIso)
    if (isNaN(seen.getTime())) {
        return ""
    }
    const diffMinutes = Math.floor((Date.now() - seen.getTime()) / 60000)
    if (diffMinutes < 1) {
        return "刚刚在线"
    }
    if (diffMinutes < 60) {
        return diffMinutes + " 分钟前在线"
    }
    const diffHours = Math.floor(diffMinutes / 60)
    if (diffHours < 24) {
        return diffHours + " 小时前在线"
    }
    const month = String(seen.getMonth() + 1).padStart(2, "0")
    const day = String(seen.getDate()).padStart(2, "0")
    return month + "-" + day + " 在线"
}

// 端口合法范围（用户可配置端口须避开 0-1023 特权段）
const kMinPort = 1024
const kMaxPort = 65535

// 判断用户输入端口是否在合法配置范围内
function isValidPort(port) {
    return port >= kMinPort && port <= kMaxPort
}

// 传输状态中文映射：内部状态字符串转用户可读文案（全应用唯一词表；
// .pragma library 中 qsTr 不可靠，项目无翻译场景，直接返回中文）
function transferStatusText(status) {
    switch (status) {
    case "connecting":      return "连接中..."
    case "waiting_confirm": return "等待确认"
    case "awaiting_relay":  return "等待中继确认"
    case "transferring":    return "传输中"
    case "completed":       return "已完成"
    case "failed":          return "失败"
    case "rejected":        return "已拒绝"
    case "cancelled":       return "已取消"
    default:                return status
    }
}
