/**
* @file    dir_serializer.cpp
* @version 6.6.2
* @date    2026-06-21
* @author  GridYard Team
* @brief   目录序列化工具实现
*
* 实现递归遍历目录、计算文件 SHA-256 哈希值、生成 FileItem 列表。
* 用于传输前的文件清单生成，支持多层目录结构和空文件夹。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v4.16.1] GY   2026-06-21
* * 优化封装性，补充注释
* [v0.4.1] GY   2026-05-23
* * Stage 4：初始实现
*/

#include "dir_serializer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace gy {

// 遍历路径（文件或目录），返回 FileItem 列表
QList<FileItem> DirSerializer::serialize(const QString &path)
{
    QList<FileItem> result;
    QFileInfo info(path);

    if (!info.exists()) {
        return result;
    }

    if (info.isFile()) {
        // 单文件场景：直接生成一个 FileItem 条目
        FileItem item;
        item.relativePath = info.fileName();  // 单文件的相对路径即文件名本身
        item.sizeBytes = info.size();
        item.sha256 = computeSha256(path);
        result.append(item);
    } else if (info.isDir()) {
        traverseDir(path, QString(), result);  // 目录场景：递归遍历所有子条目
    }

    return result;
}

// 计算单个文件的 SHA-256 哈希值
QString DirSerializer::computeSha256(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return QString();  // 读取失败返回空字符串，调用方需处理校验缺失场景
    }

    return hash.result().toHex();  // 返回 hex 编码的哈希值，便于协议 JSON 传输
}

// 递归遍历目录，收集文件信息
void DirSerializer::traverseDir(const QString &basePath,
                                const QString &currentPath,
                                QList<FileItem> &result)
{
    QDir dir(basePath + (currentPath.isEmpty() ? QString() : "/" + currentPath));
    // 同时列出文件和子目录，排除 . 和 .. 避免无限递归
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    // 先记录空目录（如果当前目录为空）
    if (entries.isEmpty() && !currentPath.isEmpty()) {
        FileItem item;
        item.relativePath = currentPath + "/";
        item.sizeBytes = 0;
        item.sha256 = QString();
        result.append(item);
    }

    for (const QFileInfo &entry : entries) {
        // 拼接相对路径：根层级直接用文件名，子层级拼接父路径前缀
        QString relPath = currentPath.isEmpty()
            ? entry.fileName()
            : currentPath + "/" + entry.fileName();

        if (entry.isFile()) {
            FileItem item;
            item.relativePath = relPath;
            item.sizeBytes = entry.size();
            item.sha256 = computeSha256(entry.absoluteFilePath());
            result.append(item);
        } else if (entry.isDir()) {
            traverseDir(basePath, relPath, result);  // 递归进入子目录
        }
    }
}

} // namespace gy
