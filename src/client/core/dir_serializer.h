/**
* @file    dir_serializer.h
* @version 4.10.0
* @date    2026-06-13
* @author  GY
* @brief   目录序列化工具，递归遍历目录生成文件列表并计算 SHA-256
*
* 提供静态方法将文件/目录路径转换为 FileItem 列表，
* 每个 FileItem 包含相对路径、文件大小和 SHA-256 哈希值。
* 用于 Stage 4 多文件/目录传输功能。
*
* Change Log:
* [v4.1.0] GY   2026-06-04
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
    // 遍历路径（文件或目录），返回 FileItem 列表
    static QList<FileItem> serialize(const QString &path);

    // 计算单个文件 SHA-256
    static QString computeSha256(const QString &filePath);

private:
    // 递归遍历目录
    static void traverseDir(const QString &basePath,
                            const QString &currentPath,
                            QList<FileItem> &result);
};

} // namespace gy
