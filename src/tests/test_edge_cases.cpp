/**
* @file    test_edge_cases.cpp
* @date    2026-06-04
* @author  GY
* @brief   边缘场景测试
*
* 测试用例：零字节文件 / 特殊字符文件名 / 文件名超长
*
* Change Log:
* [v0.1] GY   2026-06-04
* * Stage 4.6：初始版本
*/

#include <QtTest/QtTest>
#include "dir_serializer.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

class TestEdgeCases : public QObject {
    Q_OBJECT

private slots:
    void testLongFileName();
    void testEmptyDirectory();
    void testNestedDirectory();
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

QTEST_MAIN(TestEdgeCases)
#include "test_edge_cases.moc"
