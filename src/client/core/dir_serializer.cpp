/**
* @file    dir_serializer.cpp
* @version 4.10.0
* @date    2026-06-13
* @author  GridYard Team
* @brief   DirSerializer 实现
*
* Change Log:
* [v4.1.0] FengChunlin   2026-06-04
* * Stage 4：初始实现
*/

#include "dir_serializer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace gy {

QList<FileItem> DirSerializer::serialize(const QString &path)
{
    QList<FileItem> result;
    QFileInfo info(path);

    if (!info.exists()) {
        return result;
    }

    if (info.isFile()) {
        FileItem item;
        item.relativePath = info.fileName();
        item.sizeBytes = info.size();
        item.sha256 = computeSha256(path);
        result.append(item);
    } else if (info.isDir()) {
        traverseDir(path, QString(), result);
    }

    return result;
}

QString DirSerializer::computeSha256(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return QString();
    }

    return hash.result().toHex();
}

void DirSerializer::traverseDir(const QString &basePath,
                                const QString &currentPath,
                                QList<FileItem> &result)
{
    QDir dir(basePath + (currentPath.isEmpty() ? QString() : "/" + currentPath));
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
            traverseDir(basePath, relPath, result);
        }
    }
}

} // namespace gy
