/**
* @file    dir_serializer.h
* @version 6.6.2
* @date    2026-06-21
* @author  GridYard Team
* @brief   目录序列化工具（递归遍历 + SHA-256 计算）
*
* 提供静态方法将文件/目录路径转换为 FileItem 列表，
* 每个 FileItem 包含相对路径、文件大小和 SHA-256 哈希值。
* 用于多文件/目录传输功能，发送前遍历源路径生成文件清单。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v4.16.1] GY   2026-06-21
* * 优化封装性，补充注释
* [v0.4.1] GY   2026-05-23
* * Stage 4：初始实现，递归遍历 + SHA-256 计算
*/

#pragma once

#include <QList>
#include <QString>

namespace gy {

// 文件条目信息
struct FileItem {
    QString relativePath;   // 相对于根目录的路径
    qint64 sizeBytes;       // 文件大小（字节）
    QString sha256;         // hex 编码的 SHA-256
};

// 目录序列化工具类
class DirSerializer {
public:
    // 遍历路径（文件或目录），返回 FileItem 列表（含 SHA-256，用于传输校验）
    static QList<FileItem> serialize(const QString &path);

    // 同上，但不计算 SHA-256（只用于主线程统计文件数和总大小，避免在 UI 线程算哈希冻结界面）
    static QList<FileItem> serializeNoHash(const QString &path);

    // 计算单个文件 SHA-256
    static QString computeSha256(const QString &filePath);

private:
    // 递归遍历目录
    static void traverseDir(const QString &basePath,
                            const QString &currentPath,
                            QList<FileItem> &result);
    // 递归遍历目录（不计算 SHA-256）
    static void traverseDirNoHash(const QString &basePath,
                                   const QString &currentPath,
                                   QList<FileItem> &result);
};

} // namespace gy
