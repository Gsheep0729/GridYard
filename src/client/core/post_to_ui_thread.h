/**
* @file    post_to_ui_thread.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   跨线程回调投递的 Qt 桥接助手
*
* 后台线程（存储任务线程、Reactor 接入回调等）把结果投递回锚点对象所在
* 线程的统一入口。以 QPointer 存活检查兜底：锚点对象在事件送达前析构时
* 静默丢弃回调，杜绝悬垂调用；QueuedConnection 投递以锚点为接收者，
* 锚点销毁后其未决事件由 Qt 事件系统一并回收。
*/

#pragma once

#include <functional>

#include <QMetaObject>
#include <QObject>
#include <QPointer>

namespace gy::bridge {

// 把回调投递到锚点对象所属线程（通常为主/UI 线程）执行；
// 锚点已析构时回调被静默丢弃
inline void postToUiThread(QObject *anchor, std::function<void()> task)
{
    if (!anchor) {
        return;
    }
    QPointer<QObject> guard{anchor};
    QMetaObject::invokeMethod(anchor, [guard, task = std::move(task)]() {
        if (guard) {
            task();  // 锚点存活才执行，析构后静默丢弃
        }
    }, Qt::QueuedConnection);
}

}  // namespace gy::bridge
