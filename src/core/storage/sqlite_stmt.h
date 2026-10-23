/**
* @file    sqlite_stmt.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   SQLite3 预处理语句 RAII 封装——不依赖任何 Qt 类型
*
* 构造即预编译 SQL，析构自动释放语句。绑定参数失败不立即中断
* 调用链：首个错误被记录并延迟到 step() 以 expected 返回，保证
* 仓储层可用单一出口收错。
*/

#pragma once

#include "core/storage/sqlite_conn.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gy::storage {

// 预处理语句封装：prepare/bind/step/reset 全生命周期管理
class SqliteStmt {
public:
    // 预编译 SQL；编译失败被记录并延迟到 step() 返回
    SqliteStmt(SqliteConn &conn, const std::string &sql);
    // 析构时释放语句
    ~SqliteStmt();

    // 语句与连接一一绑定，禁止拷贝与移动
    SqliteStmt(const SqliteStmt &) = delete;
    SqliteStmt &operator=(const SqliteStmt &) = delete;

    // 绑定参数（1 起始下标，与 sqlite3_bind_* 一致）；文本按 UTF-8 透传拷贝
    SqliteStmt &bind(int idx, int64_t value);
    SqliteStmt &bind(int idx, int value);
    SqliteStmt &bind(int idx, const char *text);
    SqliteStmt &bind(int idx, const std::string &text);
    SqliteStmt &bind(int idx, const std::vector<uint8_t> &blob);
    // 绑定 NULL
    SqliteStmt &bindNull(int idx);

    // 执行一步：true 表示当前有行可读，false 表示遍历完成；
    // 预编译或绑定阶段的延迟错误在此返回；BUSY/LOCKED 时重试
    // （最多三次尝试，每次间隔 10 毫秒）后仍忙才返回错误
    StepResult step();

    // 读取当前行（0 起始下标）
    int64_t columnInt64(int col) const;
    double columnDouble(int col) const;
    std::string columnText(int col) const;  // UTF-8
    std::vector<uint8_t> columnBlob(int col) const;
    bool columnIsNull(int col) const;

    // 重置语句以便重新绑定参数再次执行（不清除已绑定值）
    void reset();
    // 清除所有已绑定的参数值
    void clearBindings();

private:
    // 记录首个延迟错误（后续错误不覆盖）
    void deferError(int code, const std::string &message);

    sqlite3_stmt *_stmt = nullptr;  // 预编译语句句柄
    bool _failed = false;           // 预编译或绑定是否已失败
    StorageError _deferred;         // 首个延迟错误
};

}  // namespace gy::storage
