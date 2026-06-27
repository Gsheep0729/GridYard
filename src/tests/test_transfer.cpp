/**
* @file    test_transfer.cpp
* @date    2026-06-05
* @author  GY
* @brief   文件传输功能测试
*
* 测试 GridYard 文件传输的各个功能模块。
*
* Change Log:
* [v1.0] GY   2026-06-05
* * 初始版本：基础传输功能测试
*/

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonDocument>

#include "data_types.h"
#include "frame_codec.h"
#include "protocol.h"
#include "dir_serializer.h"

using gy::FileItem;
using gy::DirSerializer;

class TestTransfer : public QObject {
    Q_OBJECT

private slots:
    // 测试 DirSerializer 目录序列化
    void testDirSerializer();
    // 测试 FileEntry 数据结构
    void testFileEntry();
};

void TestTransfer::testDirSerializer()
{
    // 创建临时目录和文件
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 创建测试文件
    QFile file1(dir.path() + "/test1.txt");
    QVERIFY(file1.open(QIODevice::WriteOnly));
    file1.write("Hello World");
    file1.close();

    QFile file2(dir.path() + "/test2.pdf");
    QVERIFY(file2.open(QIODevice::WriteOnly));
    file2.write("PDF content");
    file2.close();

    // 测试序列化
    QList<FileItem> entries = DirSerializer::serialize(dir.path());
    QCOMPARE(entries.size(), 2);

    // 验证文件信息
    bool foundTxt = false;
    bool foundPdf = false;
    for (const auto &entry : entries) {
        if (entry.relativePath == "test1.txt") {
            QCOMPARE(entry.sizeBytes, 11);
            foundTxt = true;
        }
        if (entry.relativePath == "test2.pdf") {
            QCOMPARE(entry.sizeBytes, 11);
            foundPdf = true;
        }
    }
    QVERIFY(foundTxt);
    QVERIFY(foundPdf);
}

void TestTransfer::testFileEntry()
{
    // 测试 FileItem 数据结构
    FileItem entry;
    entry.relativePath = "test/file.pdf";
    entry.sizeBytes = 1024;
    entry.sha256 = "abc123";

    QCOMPARE(entry.relativePath, "test/file.pdf");
    QCOMPARE(entry.sizeBytes, 1024);
    QCOMPARE(entry.sha256, "abc123");
}

QTEST_MAIN(TestTransfer)
#include "test_transfer.moc"
