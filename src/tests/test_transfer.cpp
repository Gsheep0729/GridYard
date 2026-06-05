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
    // 测试 FrameCodec 编解码
    void testFrameCodecEncodeDecode();
    // 测试 DirSerializer 目录序列化
    void testDirSerializer();
    // 测试协议 Type 码
    void testProtocolTypes();
    // 测试 FileEntry 数据结构
    void testFileEntry();
};

void TestTransfer::testFrameCodecEncodeDecode()
{
    // 测试单帧编解码
    QByteArray payload = "Hello, GridYard!";
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeHello, payload);

    // 验证帧格式：4字节类型 + 4字节长度 + payload
    QCOMPARE(frame.size(), 4 + 4 + payload.size());

    // 解码
    FrameCodec codec;
    QSignalSpy spy(&codec, &FrameCodec::frameReady);
    codec.feed(frame);

    QCOMPARE(spy.size(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args[0].toUInt(), gy::protocol::kTypeHello);
    QCOMPARE(args[1].toByteArray(), payload);
}

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

void TestTransfer::testProtocolTypes()
{
    // 验证协议 Type 码定义
    QCOMPARE(gy::protocol::kTypeHello, quint32(0x0001));
    QCOMPARE(gy::protocol::kTypeTransferReq, quint32(0x0101));
    QCOMPARE(gy::protocol::kTypeTransferRsp, quint32(0x0102));
    QCOMPARE(gy::protocol::kTypeDataChunk, quint32(0x0201));
    QCOMPARE(gy::protocol::kTypeChunkAck, quint32(0x0301));
    QCOMPARE(gy::protocol::kTypeTransferDone, quint32(0x0302));
    QCOMPARE(gy::protocol::kTypeCancel, quint32(0x0401));
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
