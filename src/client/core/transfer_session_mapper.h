/**
* @file    transfer_session_mapper.h
* @version 7.15.19
* @date    2026-10-04
* @author  GridYard Team
* @brief   传输会话与持久化记录的映射
*
* 承接会话行（QVariantMap）与 TransferRecord 之间的双向转换，
* 以及删除已接收本地文件的文件系统安全策略。纯映射与文件操作，
* 不持有网络对象也不关心会话状态机。
*
* Change Log:
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
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
* [v7.10.0] GY   2026-10-02
* * 自 TransferSessionManager 拆出记录映射与接收文件清理策略
*/

#pragma once

#include "history_records.h"

#include <QVariantMap>

class TransferSessionMapper {
public:
    // 删除已接收本地文件的结果
    enum class DeleteResult {
        Deleted,      // 删除成功
        NotEligible,  // 记录不满足删除条件（非接收成功记录）
        InvalidPath,  // 本地路径无效或文件不存在
        RemoveFailed  // 文件系统删除失败
    };

    // 纯静态工具类，禁止实例化
    TransferSessionMapper() = delete;

    // 将一条持久化历史记录恢复成 QML 可消费的会话行
    static QVariantMap sessionFromRecord(const TransferRecord &record);
    // 将已收敛为最终状态的会话行转成可持久化的记录快照
    static TransferRecord recordFromSession(const QVariantMap &session);
    // 按安全策略删除已接收的本地文件；失败时经 failedPath 回传路径
    static DeleteResult deleteReceivedFile(const QVariantMap &session, QString *failedPath = nullptr);
};
