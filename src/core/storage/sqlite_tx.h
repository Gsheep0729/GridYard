/**
* @file    sqlite_tx.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   SQLite3 RAII 事务封装（BEGIN IMMEDIATE/COMMIT/ROLLBACK）
*
* 构造即开启事务，析构时若未提交则自动回滚；回滚失败只写标准错误
* 日志、绝不抛出异常，保证异常路径下的栈展开安全。
*/

#pragma once

#include "core/storage/sqlite_conn.h"

namespace gy::storage {

// 事务封装：一个对象对应一次显式事务
class SqliteTx {
public:
    // 构造时执行 BEGIN IMMEDIATE；失败被记录，经 isActive/commit 报告
    explicit SqliteTx(SqliteConn &conn);
    // 析构时未提交则自动回滚（失败仅写日志，不抛出）
    ~SqliteTx();

    // 事务与连接一一绑定，禁止拷贝与移动
    SqliteTx(const SqliteTx &) = delete;
    SqliteTx &operator=(const SqliteTx &) = delete;

    // 事务是否已成功开启且尚未提交
    bool isActive() const;

    // 提交事务；BEGIN 阶段的延迟错误在此一并返回
    SqlResult commit();
    // 手动回滚（幂等；失败只写日志不抛出）
    void rollback() noexcept;

private:
    // 执行回滚并吞掉错误，供析构与手动回滚共用
    void rollbackQuietly() noexcept;

    SqliteConn &_conn;          // 不拥有：目标连接
    bool _begun = false;        // BEGIN 是否成功
    bool _committed = false;    // 是否已提交
    StorageError _beginError;   // BEGIN 失败时的延迟错误
};

}  // namespace gy::storage
