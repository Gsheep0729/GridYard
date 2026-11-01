/**
 * @file    Style.js
 * @version 8.3.0
 * @date 2026-11-02
 * @author  GY
 * @brief   QML 界面样式常量
 *
 * 颜色按语义分组：底色、边框、品牌主色、状态色、文字色。
 * 状态色成对提供实色与浅底色（Soft），供标签、横幅、卡片背景使用。
 * 深浅双表（C-10）：Light/Dark 两套同键色值，Color 访问器按
 * Qt.styleHints.colorScheme 派发；getter 在绑定求值时读取 colorScheme
 * 建立依赖，系统深浅切换后所有取色绑定自动重算，QML 侧无需逐处接线。
 * 深色 statusWarn/Ok/Err 成对色按 WCAG 2.x 相对亮度公式复算 ≥4.5（见表内注）。
 */

// —— 浅色表（既有 43 色；primary 按 U2-44 拍板压深 #3B82F6→#2563EB，白字 5.17:1）——
const Light = {
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
    // Switch 关闭态圆点描边（U2-09 收编淡紫轨道）：白圆点在 border 轨道上收一圈
    // 保持可辨；关闭态轨道复用 border，开启态轨道走 primary，不另设键
    switchKnobBorder: "#D1D5DB",
    // 设备卡悬停底色（U2-06 削弱）：对实测列表底 #F7F7F7 各通道差 (16,12,4)，
    // 对比度 1.12:1（旧 #DFE6F0 达 1.17:1 反超选中，点了像没点上），灰度帧差
    // ≈12 仍高于不可辨线（surfaceLeft 帧差 10）；冷味保留，弱于选中 primarySoftSelected
    hoverSoft: "#E7EBF3",
    // 品牌主色（U2-44：#2563EB 上白字 5.17:1 达 AA，旧值 #3B82F6 仅 3.68:1）
    primary: "#2563EB",
    primarySoft: "#EFF6FF",
    primarySoftHover: "#DBEAFE",
    // 设备卡选中底色（U2-06 强化）：比 primarySoftHover 再深一档，对列表底
    // #F7F7F7 1.20:1（旧选中 primarySoft 仅 1.02:1），压过削弱后的悬停 1.12:1
    primarySoftSelected: "#D6E4FF",
    // 状态色（实色用于文字、圆点与标签底，Soft 用于大面积浅底）
    success: "#10B981",
    successSoft: "#ECFDF5",
    successHover: "#D1FAE5",
    warning: "#F59E0B",
    warningSoft: "#FFFBEB",
    error: "#EF4444",
    errorHover: "#DC2626",
    errorPressed: "#B91C1C",
    errorSoft: "#FEF2F2",
    // 状态徽标成对色：浅底 + 深字，白字压实色不达 4.5 时改用这一族
    // （实测对比度：警示 4.84、成功 5.21、失败 5.91）
    statusWarnBg: "#FFFBEB",
    statusWarnFg: "#B45309",
    statusOkBg: "#ECFDF5",
    statusOkFg: "#047857",
    statusErrBg: "#FEF2F2",
    statusErrFg: "#B91C1C",
    // Toast 图标字（U2-17）：压在 textOnAccent 16% 圆底上的徽标字，取状态
    // 徽标同款深字（与 statusOkFg/statusErrFg 同值），原状态实色过浅不可辨
    toastGlyphOk: "#047857",
    toastGlyphErr: "#B91C1C",
    // 浅底上的加深前景：主色/接收紫的实色压浅底不足 4.5，深一档可达 6 以上
    primaryStrong: "#1D4ED8",
    receiveAccentStrong: "#6D28D9",
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

// —— 深色表（与浅色表同键；深色下浮起面比平铺面亮一档，与浅色反向同层）——
// statusWarn/Ok/Err 成对色按 WCAG 2.x 公式复算（0.2126R+0.7152G+0.0722B，sRGB
// 反伽马）：警示 9.34、成功 9.64、失败 7.46，均 ≥4.5
const Dark = {
    // 底色：pageBg 为框架底，surface/surfaceLeft 为平铺面板（侧栏/会话页），
    // 设备列表、文件夹卡、window 逐档上浮；浮层 menuBackground 比 window 深一档
    pageBg: "#0F172A",
    surface: "#1E293B",
    surfaceLeft: "#1E293B",
    surfaceMid: "#24314A",
    surfaceSoft: "#26334A",
    window: "#2D3A54",
    menuBackground: "#25324A",
    menuShadow: "#99000000",    // 深色下改纯黑 60%：冷灰影压不住近黑底（实机复核项）
    // 边框
    border: "#3C4C69",
    borderSoft: "#33425C",
    // Switch 关闭态圆点描边（U2-09）：白圆点收一圈淡边；关闭态轨道复用 border，
    // 开启态轨道走 primary，与浅色表同键
    switchKnobBorder: "#94A3B8",
    // 设备卡悬停底色：比列表底 surfaceMid 亮一档带冷味（1.14:1），低于选中的
    // 1.37:1（U2-06 倒挂修复后选中换 primarySoftSelected，悬停值无需再动）
    hoverSoft: "#2A3A56",
    // 品牌主色：深浅同值（白字 5.17:1 两表同过），Material.accent/选中条/气泡底随表生效
    primary: "#2563EB",
    primarySoft: "#1D3054",
    primarySoftHover: "#2A4270",
    // 设备卡选中底色（U2-06）：对 surfaceMid 1.37:1（旧 primarySoft 仅 1.01:1），
    // 压过悬停 1.14:1；深色 textMuted 在其上 4.62:1 仍过 AA
    primarySoftSelected: "#2B4574",
    // 状态色：语义实色深色下上浮一档，保圆点/文字在深底可读（error/success 作
    // Toast 与危险按钮实底压白字时低于 AA，与浅色同源的既有问题，随 U2-05 复测）
    success: "#34D399",
    successSoft: "#0E2B20",
    successHover: "#163D2E",
    warning: "#FBBF24",
    warningSoft: "#2E2309",
    error: "#F87171",
    errorHover: "#EF4444",
    errorPressed: "#DC2626",
    errorSoft: "#331A1D",
    // 状态徽标成对色（深色实测：警示 9.34、成功 9.64、失败 7.46）；statusErrBg
    // 与 errorSoft 拉开一档，失败卡上徽标不再与卡底同色（批 1 验收遗留第 1 条）
    statusWarnBg: "#3A2D0B",
    statusWarnFg: "#FCD34D",
    statusOkBg: "#0C2E22",
    statusOkFg: "#6EE7B7",
    statusErrBg: "#3F2328",
    statusErrFg: "#FCA5A5",
    // Toast 图标字（U2-17）：深色状态实色上浮，白圆底合成后偏浅，状态前景
    // （深底浅字方向）压上反而糊，白字对合成圆底仍最清晰，随 textOnAccent 同值
    toastGlyphOk: "#FFFFFF",
    toastGlyphErr: "#FFFFFF",
    // 深底上的上浮前景：与浅色 primaryStrong 同语义（方向标签/链接字色）
    primaryStrong: "#93C5FD",
    receiveAccentStrong: "#C4B5FD",
    // 文字（textWeak 与浅色同为装饰档，不参与信息可读性）
    textMain: "#F1F5F9",
    textSecondary: "#CBD5E1",
    textMuted: "#A9B6C6",
    textWeak: "#6B7F99",
    textOnAccent: "#FFFFFF",  // 彩色底（主按钮、状态标签）上的文字
    // 方向与侧栏
    receiveAccent: "#A78BFA",  // 接收方向标识，与发送侧主色区分
    receiveAccentSoft: "#251E42",  // 接收方向浅底（文件夹胶囊）
    receiveAccentSoftHover: "#312958",
    menubar: "#97A5B8",
    menubarSelect: "#33425C",
    menubarClicked: "#3A4A66",
    transparent: "#00000000"
}

// 双表访问器：Color 的每个键派发到当前 colorScheme 对应表。getter 在绑定求值
// 时读取 Qt.styleHints.colorScheme（Qt 6.5+，工程基线 6.10 满足）即建立响应
// 依赖，深浅切换后取色绑定整体重算。键集与两表恒等，新增颜色键只改两表，
// 访问器无需改动
const Color = {}
Object.keys(Light).forEach(function (key) {
    Object.defineProperty(Color, key, {
        enumerable: true,
        get: function () {
            return Qt.styleHints.colorScheme === Qt.Dark ? Dark[key] : Light[key]
        }
    })
})

// 字号梯度（U2-27 六档）：现存 11 档像素值向此收敛，13 并入 14、11 并入 10；
// 全量 219 处替换登记为长期项，仅随触及文件顺带收敛
const Type = {
    badge: 10,      // 状态徽标与最小注记
    caption: 12,    // 辅助行：时间戳、副行、计数
    body: 14,       // 正文与控件文字
    heading: 16,    // 区块小标题
    title: 20,      // 弹窗标题
    display: 48     // 品牌/空态大字
}

const Radius = {
    xs: 4,
    badge: 6,   // 状态徽标专用档（U2-30）：比 xs 柔和、比 sm 收敛
    sm: 8,
    md: 10,
    lg: 12,
    xl: 16      // 大弹窗/浮层圆角（U2-26 允许新增的一档）
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
