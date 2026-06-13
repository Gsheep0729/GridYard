# GridYard v4.11.0 Release Notes

**发布日期**：2026-06-13

---

## 新增功能

- 文件夹传输保留顶层目录结构
- 支持空文件夹传输
- 接收文件重名自动避让（自动添加序号）
- 自动接收保存模式（配置项 `autoAcceptFiles`）

## 优化改进

- 优化文件夹传输协议，新增 `is_directory`、`root_name`、`empty_directories` 字段
- 接收端路径校验，防止目录穿越攻击
- 完善传输日志输出

## Bug 修复

- 修复传输记录不显示的问题（QML 属性绑定修复）
- 修复传输记录显示"未知设备"的问题（delegate 绑定方式修复）
- 修复传输完成弹窗按钮逻辑（"确定"只关闭，"打开文件所在位置"打开目录）
- 修复文件夹传输时目录结构丢失的问题

## 代码规范

- 所有文件添加 `@version` 字段
- Change Log 版本号统一为 `[vX.Y.Z]` 格式
- QML 文件移除 `tw_` 前缀，改为小写开头命名
- 更新 CLAUDE.md 中 QML 命名规范说明

## 开发心得

- 新增第二十五章：QML 属性绑定踩坑
- 新增第二十六章：modelData 在 Qt6 中不可用
- 新增第二十七章：传输完成弹窗的按钮角色问题
- 新增第二十八章：JavaScript 数组作为 ListView model 的绑定问题
- 新增第二十九章：ListView 空列表判断的调整

## 版本信息

| 项目 | 版本 |
|------|------|
| 程序版本 | v4.11.0 |
| CMakeLists.txt | 4.11.0 |
| main.cpp | 4.11.0 |

## 下载

- **Linux AppImage**：`GridYard-v4.11.0-x86_64.AppImage`（89MB）

---

**完整更新日志**：https://github.com/Gsheep0729/GridYard/compare/v4.10.0...v4.11.0
