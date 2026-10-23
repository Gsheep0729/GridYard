/**
* @file    rendezvous_log.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   协调节点服务的最小日志封装
*
* 独立服务进程没有事件循环与分类日志设施，日志直写标准流并立即刷新，
* 保持 systemd/journal 收集场景下行序稳定。正文由调用方自行拼接
* "[模块]" 前缀，文本格式与旧 Qt 版保持同风格。
*/

#pragma once

#include <cstdio>
#include <string>

namespace gy::rendezvous {

// 常规运行信息写标准输出
inline void logInfo(const std::string &message)
{
    std::fprintf(stdout, "%s\n", message.c_str());
    std::fflush(stdout);
}

// 异常与拒绝类告警写标准错误
inline void logWarn(const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    std::fflush(stderr);
}

}  // namespace gy::rendezvous
