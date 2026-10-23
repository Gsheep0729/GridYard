/**
* @file    database_core.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 SQLite 连接、迁移与事务代管者实现
*
* 损坏恢复语义与 Qt 版一致：首次打开失败且错误属于文件损坏时，
* 把原库连同 WAL/SHM 边车改名保留到 .corrupt 时间戳文件后重建
* 空库并重新迁移，向上透出重建标志与备份路径。
*/

#include "core/storage/database_core.h"

#include "core/storage/iso_time.h"
#include "core/storage/migration_core.h"
#include "core/storage/sqlite_tx.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace gy::storage {
namespace {

// 判断文件是否存在（异常一律视为不存在）
bool fileExists(const std::string &path)
{
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

// 小写化错误文本，供损坏关键字匹配
std::string toLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}  // namespace

// 打开数据库、配置连接参数并执行迁移
bool DatabaseCore::initialize(const std::string &databasePath, std::string *errorMessage)
{
    _available = false;
    _rebuiltFromBackup = false;  // 重建状态按本次 initialize 重新计算
    _rebuiltBackupPath.clear();
    _conn.reset();
    if (errorMessage) {
        errorMessage->clear();
    }

    const std::filesystem::path filePath{databasePath};
    std::error_code dirError;
    // 首次使用时自动创建数据库目录
    std::filesystem::create_directories(filePath.parent_path(), dirError);
    if (!filePath.parent_path().empty()
        && !std::filesystem::is_directory(filePath.parent_path(), dirError)) {
        if (errorMessage) {
            *errorMessage = "无法创建数据库目录";
        }
        return false;
    }

    _databasePath = databasePath;
    const bool existingDatabase = fileExists(_databasePath);

    if (openMainConnection(errorMessage)) {
        _available = true;
        return true;
    }

    // 首次打开失败后判断是否为文件损坏，尝试备份重建
    const std::string firstError = errorMessage ? *errorMessage : std::string{};
    _conn.reset();  // 备份损坏库前必须释放 SQLite 文件句柄

    if (existingDatabase && isCorruptionError(firstError)) {
        std::string backupError;
        std::string backupPath;
        if (!backupCorruptDatabase(backupPath, &backupError)) {
            if (errorMessage) {
                *errorMessage = backupError;
            }
            return false;
        }

        // 备份成功后重新创建空库并迁移
        if (errorMessage) {
            errorMessage->clear();
        }
        if (openMainConnection(errorMessage)) {
            _available = true;
            // 重建成功必须向上可见，界面据此提示历史已被清零重置
            _rebuiltFromBackup = true;
            _rebuiltBackupPath = backupPath;
            return true;
        }
    }

    return false;
}

// 判断数据库是否已成功初始化
bool DatabaseCore::isAvailable() const
{
    return _available;
}

// 获取当前 Schema 版本
int DatabaseCore::schemaVersion() const
{
    if (!_conn.has_value()) {
        return -1;
    }
    std::string errorMessage;
    return MigrationCore::schemaVersion(*_conn, &errorMessage);
}

// 在短事务中执行跨表持久化任务
bool DatabaseCore::runInTransaction(const TransactionTask &task, std::string *errorMessage) const
{
    if (!_conn.has_value()) {
        if (errorMessage) {
            *errorMessage = "本地历史不可用";
        }
        return false;
    }

    SqliteTx tx{*_conn};
    if (!tx.isActive()) {
        if (errorMessage) {
            *errorMessage = "事务开启失败";
        }
        return false;
    }

    if (!task(*_conn, errorMessage)) {
        tx.rollback();  // 业务步骤失败时撤销本次跨表变更
        return false;
    }

    if (auto commit = tx.commit(); !commit.has_value()) {
        if (errorMessage) {
            *errorMessage = commit.error().message;
        }
        return false;  // 提交失败时事务封装已回滚
    }
    return true;
}

// 在单个事务内依次执行多个步骤，全部成功才提交
bool DatabaseCore::runSteps(const std::vector<TransactionTask> &steps,
                            std::string *errorMessage) const
{
    if (!_conn.has_value()) {
        if (errorMessage) {
            *errorMessage = "本地历史不可用";
        }
        return false;
    }

    SqliteTx tx{*_conn};
    if (!tx.isActive()) {
        if (errorMessage) {
            *errorMessage = "事务开启失败";
        }
        return false;
    }

    for (const auto &step : steps) {
        if (!step(*_conn, errorMessage)) {
            tx.rollback();
            return false;
        }
    }

    if (auto commit = tx.commit(); !commit.has_value()) {
        if (errorMessage) {
            *errorMessage = commit.error().message;
        }
        return false;
    }
    return true;
}

// 判断本次 initialize 是否从损坏备份重建了数据库
bool DatabaseCore::lastInitializeRebuilt() const
{
    return _rebuiltFromBackup;
}

// 获取本次重建前损坏库的备份路径
const std::string &DatabaseCore::rebuiltBackupPath() const
{
    return _rebuiltBackupPath;
}

// 主连接访问入口
SqliteConn &DatabaseCore::connection()
{
    return *_conn;
}

// 打开主连接并完成参数配置与迁移
bool DatabaseCore::openMainConnection(std::string *errorMessage)
{
    auto opened = SqliteConn::open(_databasePath);
    if (!opened.has_value()) {
        if (errorMessage) {
            *errorMessage = opened.error().message;
        }
        return false;
    }
    _conn.emplace(std::move(opened).value());

    if (!MigrationCore::migrate(*_conn, errorMessage)) {
        _conn.reset();
        return false;
    }
    return true;
}

// 将损坏数据库备份到同目录的 .corrupt 时间戳文件
bool DatabaseCore::backupCorruptDatabase(std::string &backupPath,
                                         std::string *errorMessage) const
{
    const std::string timestamp = toCompactStamp(nowUnixMillis());
    const std::string backupFilePath = _databasePath + ".corrupt-" + timestamp;
    std::error_code renameError;
    std::filesystem::rename(_databasePath, backupFilePath, renameError);
    if (renameError) {
        if (errorMessage) {
            *errorMessage = "无法备份损坏数据库";
        }
        return false;
    }

    const char *sidecars[] = {"-wal", "-shm"};
    for (const char *suffix : sidecars) {
        const std::string sidecarPath = _databasePath + suffix;
        if (!fileExists(sidecarPath)) {
            continue;
        }
        // WAL/SHM 属于同一个 SQLite 文件组，能备份则一起保留供事后排查。
        std::error_code ignored;
        std::filesystem::rename(sidecarPath, backupFilePath + suffix, ignored);
    }

    backupPath = backupFilePath;
    return true;
}

// 判断底层错误是否属于 SQLite 文件损坏
bool DatabaseCore::isCorruptionError(const std::string &errorMessage)
{
    const std::string normalized = toLower(errorMessage);
    return normalized.find("file is not a database") != std::string::npos
           || normalized.find("database disk image is malformed") != std::string::npos
           || normalized.find("not a database") != std::string::npos
           || normalized.find("malformed") != std::string::npos;
}

}  // namespace gy::storage
