/**
 * @file    Style.js
 * @version 7.19.0
 * @date 2026-10-05
 * @author  GY
 * @brief   QML 界面样式常量
 *
 * 颜色按语义分组：底色、边框、品牌主色、状态色、文字色。
 * 状态色成对提供实色与浅底色（Soft），供标签、横幅、卡片背景使用。
 */

const Color = {
    // 底色
    pageBg: "#F5F7FA",
    surface: "#EDEDED",
    surfaceLeft: "#EDEDED",   // 侧栏与会话页底色
    surfaceMid: "#F7F7F7",
    surfaceSoft: "#F9FAFB",
    window: "#FFFFFF",
    menuBackground: "#F2F3F5",  // 菜单底色：比白色弹窗/卡片深一档形成浮层层次
    menuShadow: "#471F2937",    // 菜单阴影（ARGB，约 28% 冷灰），全应用唯一阴影
    // 边框
    border: "#E5E7EB",
    borderSoft: "#EEF2F7",
    // 品牌主色
    primary: "#3B82F6",
    primarySoft: "#EFF6FF",
    primarySoftHover: "#DBEAFE",
    // 状态色（实色用于文字、圆点与标签底，Soft 用于大面积浅底）
    success: "#10B981",
    successSoft: "#ECFDF5",
    warning: "#F59E0B",
    warningSoft: "#FFFBEB",
    error: "#EF4444",
    errorHover: "#DC2626",
    errorPressed: "#B91C1C",
    errorSoft: "#FEF2F2",
    // 文字
    textMain: "#111827",
    textSecondary: "#4B5563",
    textMuted: "#6B7280",
    textWeak: "#9CA3AF",
    textOnAccent: "#FFFFFF",  // 彩色底（主按钮、状态标签）上的文字
    // 方向与侧栏
    receiveAccent: "#8B5CF6",  // 接收方向标识，与发送侧主色区分
    receiveAccentSoft: "#F5F3FF",  // 接收方向浅底（文件夹胶囊）
    receiveAccentSoftHover: "#EDE9FE",
    menubar: "#7F7F7F",
    menubarSelect: "#E1E1E1",
    menubarClicked: "#D5D5D5",
    transparent: "#00000000"
}

const Radius = {
    xs: 4,
    sm: 8,
    md: 10,
    lg: 12
}

const Space = {
    xs: 4,
    sm: 8,
    md: 12,
    lg: 16,
    xl: 20
}

const Motion = {
    fast: 120,
    base: 160,
    slow: 180
}
