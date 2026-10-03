/**
 * @file    FormatUtils.js
 * @version 7.15.18
 * @date    2026-10-04
 * @author  GY
 * @brief   界面展示格式化工具
 *
 * Change Log:
 * [v7.15.18] GY   2026-10-04
 * * 版本头对齐到 v7.15.18
 * [v7.15.17] GY   2026-10-04
 * * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
 * [v7.15.13] GY   2026-10-04
 * * 版本头对齐到 v7.15.13
 * [v7.15.12] GY   2026-10-03
 * 版本头对齐到 v7.15.12
 * [v7.15.11] GY   2026-10-03
 * * 版本头对齐到 v7.15.11
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v7.15.3] GY   2026-10-03
 * * transferStatusText 扩为覆盖全部会话状态的唯一词表，不再使用 qsTr
 * [v7.11.0] GY   2026-10-02
 * * 收拢拖拽与文件对话框共用的 URL 转路径逻辑，补充传输状态中文映射
 * [v6.6.2] GY   2026-06-25
 * * 同步文件头版本与当前主版本
 * [v4.12.0] DuRuoxian   2026-06-14
 * * 抽离文件大小和时间格式化逻辑
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
