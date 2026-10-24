/**
* @file    dir_serializer.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   目录序列化工具实现
*
* 实现递归遍历目录、计算文件 SHA-256 哈希值、生成 FileItem 列表。
* 遍历与哈希的内部实现为纯 C++（std::filesystem + picosha2 流式），
* 遍历语义与旧 QDir 版一致：隐藏条目不外发、名称大小写不敏感排序、
* 符号链接一律跳过、空子目录以"路径/"条目入清单。
*/

#include "dir_serializer.h"

#include "core/utils/dir_hasher.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace gy {

namespace {

// 名称比较：先按小写折叠的字典序，并列时按原始字节序决出，
// 与旧 QDir 默认遍历（大小写不敏感 Name 排序）保持一致
bool nameLess(const std::string &a, const std::string &b)
{
    const size_t common = std::min(a.size(), b.size());
    for (size_t i = 0; i < common; ++i) {
        const int ca = std::tolower(static_cast<unsigned char>(a[i]));
        const int cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) {
            return ca < cb;
        }
    }
    if (a.size() != b.size()) {
        return a.size() < b.size();
    }
    return a < b;
}

// 列出目录可见子条目名称：隐藏条目（点开头）排除，条目按名称排序；
// 目录不可读时返回空（与旧 QDir 行为一致，由上层按空目录处理）
std::vector<std::string> visibleEntries(const std::filesystem::path &dirPath)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dirPath, ec)) {
        const std::string name = entry.path().filename().string();
        if (!name.empty() && name.front() == '.') {
            continue;  // 隐藏文件与隐藏目录均不外发
        }
        names.push_back(name);
    }
    if (ec) {
        return {};
    }
    std::sort(names.begin(), names.end(), nameLess);
    return names;
}

// 读取文件大小；查询失败按 0 处理（与旧 QFileInfo::size 失败语义一致）
qint64 fileSizeOrZero(const std::filesystem::path &path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<qint64>(size);
}

}  // namespace

// 遍历路径（文件或目录），返回 FileItem 列表
QList<FileItem> DirSerializer::serialize(const QString &path)
{
    QList<FileItem> result;
    const std::filesystem::path target{path.toStdString()};
    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) {
        return result;
    }

    if (std::filesystem::is_regular_file(target, ec)) {
        // 单文件场景：直接生成一个 FileItem 条目（顶层路径跟随符号链接，与旧实现一致）
        FileItem item;
        item.relativePath = QString::fromStdString(target.filename().string());
        item.sizeBytes = fileSizeOrZero(target);
        item.sha256 = computeSha256(path);
        result.append(item);
    } else if (std::filesystem::is_directory(target, ec)) {
        traverseDir(path, QString(), result);  // 目录场景：递归遍历所有子条目
    }

    return result;
}

// 遍历路径（文件或目录），返回 FileItem 列表（不计算 SHA-256，仅用于 UI 线程快速统计）
QList<FileItem> DirSerializer::serializeNoHash(const QString &path)
{
    QList<FileItem> result;
    const std::filesystem::path target{path.toStdString()};
    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) {
        return result;
    }

    if (std::filesystem::is_regular_file(target, ec)) {
        FileItem item;
        item.relativePath = QString::fromStdString(target.filename().string());
        item.sizeBytes = fileSizeOrZero(target);
        item.sha256 = QString();
        result.append(item);
    } else if (std::filesystem::is_directory(target, ec)) {
        traverseDirNoHash(path, QString(), result);
    }

    return result;
}

// 计算单个文件的 SHA-256 哈希值（纯 C++ 流式分块实现，失败返回空串）
QString DirSerializer::computeSha256(const QString &filePath)
{
    return QString::fromStdString(gy::core::sha256HexOfFile(filePath.toStdString()));
}

// 递归遍历目录，收集文件信息
void DirSerializer::traverseDir(const QString &basePath,
                                const QString &currentPath,
                                QList<FileItem> &result)
{
    std::filesystem::path dir{basePath.toStdString()};
    if (!currentPath.isEmpty()) {
        dir /= currentPath.toStdString();
    }
    const std::vector<std::string> names = visibleEntries(dir);

    // 先记录空目录（如果当前目录为空）
    if (names.empty() && !currentPath.isEmpty()) {
        FileItem item;
        item.relativePath = currentPath + "/";
        item.sizeBytes = 0;
        item.sha256 = QString();
        result.append(item);
        return;
    }

    for (const std::string &name : names) {
        const std::filesystem::path child = dir / name;

        // 符号链接一律跳过：目录链接不递归（环形链接如 a/loop -> a 会让序列化无限递归挂死 worker 线程），
        // 文件链接不入清单（避免把目录外的文件外发给对端）；静默跳过不报错，与主流压缩工具行为一致
        std::error_code linkEc;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(child, linkEc))) {
            continue;
        }

        // 拼接相对路径：根层级直接用文件名，子层级拼接父路径前缀
        const QString relPath = currentPath.isEmpty()
            ? QString::fromStdString(name)
            : currentPath + "/" + QString::fromStdString(name);

        std::error_code typeEc;
        if (std::filesystem::is_regular_file(child, typeEc)) {
            FileItem item;
            item.relativePath = relPath;
            item.sizeBytes = fileSizeOrZero(child);
            item.sha256 = computeSha256(QString::fromStdString(child.string()));
            result.append(item);
        } else if (std::filesystem::is_directory(child, typeEc)) {
            traverseDir(basePath, relPath, result);  // 递归进入子目录
        }
    }
}

// 递归遍历目录，收集文件信息（不计算 SHA-256）
void DirSerializer::traverseDirNoHash(const QString &basePath,
                                       const QString &currentPath,
                                       QList<FileItem> &result)
{
    std::filesystem::path dir{basePath.toStdString()};
    if (!currentPath.isEmpty()) {
        dir /= currentPath.toStdString();
    }
    const std::vector<std::string> names = visibleEntries(dir);

    if (names.empty() && !currentPath.isEmpty()) {
        FileItem item;
        item.relativePath = currentPath + "/";
        item.sizeBytes = 0;
        item.sha256 = QString();
        result.append(item);
        return;
    }

    for (const std::string &name : names) {
        const std::filesystem::path child = dir / name;

        // 符号链接一律跳过，与 traverseDir 同一安全动机：环形链接不递归、目录外文件不外发
        std::error_code linkEc;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(child, linkEc))) {
            continue;
        }

        const QString relPath = currentPath.isEmpty()
            ? QString::fromStdString(name)
            : currentPath + "/" + QString::fromStdString(name);

        std::error_code typeEc;
        if (std::filesystem::is_regular_file(child, typeEc)) {
            FileItem item;
            item.relativePath = relPath;
            item.sizeBytes = fileSizeOrZero(child);
            item.sha256 = QString();
            result.append(item);
        } else if (std::filesystem::is_directory(child, typeEc)) {
            traverseDirNoHash(basePath, relPath, result);
        }
    }
}

} // namespace gy
