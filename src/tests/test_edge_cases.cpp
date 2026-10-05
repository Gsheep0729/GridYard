/**
* @file    test_edge_cases.cpp
* @version 7.19.0
* @date 2026-10-05
* @author  GY
* @brief   边缘场景测试
*
* 测试用例：零字节文件 / 特殊字符文件名 / 文件名超长 / 符号链接跳过
*/

#include <QtTest/QtTest>
#include "dir_serializer.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <chrono>
#include <future>

namespace {

// 在工作线程启动序列化并返回堆上的 future：超时失败路径故意不回收，
// std::future 的析构会阻塞等待任务结束，环形链接回归时反而会把测试进程挂死
std::future<QList<gy::FileItem>> *startAsyncSerialize(QList<gy::FileItem> (*fn)(const QString &),
                                                      const QString &path)
{
    return new std::future<QList<gy::FileItem>>(
        std::async(std::launch::async, [fn, path]() { return fn(path); }));
}

// 构造含符号链接的测试目录：普通子目录、普通文件、环形目录链接、目录外与目录内文件链接
bool buildSymlinkTree(const QString &root, const QString &outsideFile)
{
    QDir dir(root);
    if (!dir.mkpath("real_dir")) {
        return false;
    }

    QFile plain(root + "/plain.txt");
    if (!plain.open(QIODevice::WriteOnly)) {
        return false;
    }
    plain.write("plain");
    plain.close();

    QFile nested(root + "/real_dir/nested.txt");
    if (!nested.open(QIODevice::WriteOnly)) {
        return false;
    }
    nested.write("nested");
    nested.close();

    // 环形链接指回序列化根目录自身，另加目录外与目录内文件链接，三类链接条目都应被跳过
    return QFile::link(root, root + "/loop_dir")
        && QFile::link(outsideFile, root + "/outer_file_link")
        && QFile::link(root + "/plain.txt", root + "/inner_file_link");
}

} // namespace

class TestEdgeCases : public QObject {
    Q_OBJECT

private slots:
    void testLongFileName();
    void testEmptyDirectory();
    void testNestedDirectory();
    void testSymlinkLoopSerializationCompletes();
    void testSerializeNoHashSkipsSymlinks();
};

void TestEdgeCases::testLongFileName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 创建长文件名（255 字节是 Linux 文件名限制）
    QString longName = QString("a").repeated(200) + ".txt";
    QString filePath = dir.path() + "/" + longName;
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("test content");
    file.close();

    // 测试序列化
    QList<gy::FileItem> items = gy::DirSerializer::serialize(filePath);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].relativePath, longName);
    QVERIFY(items[0].sizeBytes > 0);
}

void TestEdgeCases::testEmptyDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 创建空子目录
    QDir subDir(dir.path() + "/empty_subdir");
    QVERIFY(subDir.mkpath("."));

    // 测试序列化
    QList<gy::FileItem> items = gy::DirSerializer::serialize(dir.path());

    // 验证空目录被记录
    bool found = false;
    for (const auto &item : items) {
        if (item.relativePath == "empty_subdir/") {
            found = true;
            QCOMPARE(item.sizeBytes, 0);
            break;
        }
    }
    QVERIFY2(found, "未找到空目录");
}

void TestEdgeCases::testNestedDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 创建嵌套目录结构
    QDir root(dir.path());
    QVERIFY(root.mkpath("level1/level2/level3"));

    // 在各层创建文件
    QFile f1(dir.path() + "/root.txt");
    QVERIFY(f1.open(QIODevice::WriteOnly));
    f1.write("root");
    f1.close();

    QFile f2(dir.path() + "/level1/file1.txt");
    QVERIFY(f2.open(QIODevice::WriteOnly));
    f2.write("level1");
    f2.close();

    QFile f3(dir.path() + "/level1/level2/file2.txt");
    QVERIFY(f3.open(QIODevice::WriteOnly));
    f3.write("level2");
    f3.close();

    QFile f4(dir.path() + "/level1/level2/level3/file3.txt");
    QVERIFY(f4.open(QIODevice::WriteOnly));
    f4.write("level3");
    f4.close();

    // 测试序列化
    QList<gy::FileItem> items = gy::DirSerializer::serialize(dir.path());

    // 验证所有文件都被正确识别
    QCOMPARE(items.size(), 4);  // 4 个文件

    QStringList relativePaths;
    for (const auto &item : items) {
        relativePaths.append(item.relativePath);
    }

    QVERIFY(relativePaths.contains("root.txt"));
    QVERIFY(relativePaths.contains("level1/file1.txt"));
    QVERIFY(relativePaths.contains("level1/level2/file2.txt"));
    QVERIFY(relativePaths.contains("level1/level2/level3/file3.txt"));
}

void TestEdgeCases::testSymlinkLoopSerializationCompletes()
{
    QTemporaryDir outsideDir;
    QVERIFY(outsideDir.isValid());
    QFile outside(outsideDir.path() + "/outside.txt");
    QVERIFY(outside.open(QIODevice::WriteOnly));
    outside.write("outside secret");
    outside.close();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(buildSymlinkTree(dir.path(), outsideDir.path() + "/outside.txt"));

    // 序列化放到工作线程，QTRY 轮询限时等待：环形链接回归时超时失败，而不是把测试进程挂死
    auto *future = startAsyncSerialize(&gy::DirSerializer::serialize, dir.path());
    QTRY_VERIFY(future->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    QList<gy::FileItem> items = future->get();
    delete future;

    // 普通内容完整（路径与字节数逐一核对），任何链接条目都不应出现在结果里
    QCOMPARE(items.size(), 2);
    for (const auto &item : items) {
        if (item.relativePath == "plain.txt") {
            QCOMPARE(item.sizeBytes, qint64(5));
        } else if (item.relativePath == "real_dir/nested.txt") {
            QCOMPARE(item.sizeBytes, qint64(6));
        } else {
            QVERIFY2(false, qPrintable("结果中出现了非预期条目: " + item.relativePath));
        }
        QVERIFY(!item.sha256.isEmpty());
    }
}

void TestEdgeCases::testSerializeNoHashSkipsSymlinks()
{
    QTemporaryDir outsideDir;
    QVERIFY(outsideDir.isValid());
    QFile outside(outsideDir.path() + "/outside.txt");
    QVERIFY(outside.open(QIODevice::WriteOnly));
    outside.write("outside secret");
    outside.close();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(buildSymlinkTree(dir.path(), outsideDir.path() + "/outside.txt"));

    auto *future = startAsyncSerialize(&gy::DirSerializer::serializeNoHash, dir.path());
    QTRY_VERIFY(future->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    QList<gy::FileItem> items = future->get();
    delete future;

    QCOMPARE(items.size(), 2);
    for (const auto &item : items) {
        if (item.relativePath == "plain.txt") {
            QCOMPARE(item.sizeBytes, qint64(5));
        } else if (item.relativePath == "real_dir/nested.txt") {
            QCOMPARE(item.sizeBytes, qint64(6));
        } else {
            QVERIFY2(false, qPrintable("结果中出现了非预期条目: " + item.relativePath));
        }
        QVERIFY(item.sha256.isEmpty());
    }
}

QTEST_MAIN(TestEdgeCases)
#include "test_edge_cases.moc"
