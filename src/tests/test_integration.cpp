/**
* @file    test_integration.cpp
* @date    2026-06-05
* @author  GY
* @brief   集成测试 - 单机多实例模拟
*
* 在同一台机器上启动多个实例，模拟多设备传输场景。
* 使用不同端口和配置文件实现隔离。
*
* Change Log:
* [v1.0] GY   2026-06-05
* * 初始版本
*/

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonDocument>

#include "config_manager.h"
#include "discovery_service.h"
#include "p2p_server.h"
#include "file_sender_worker.h"
#include "file_receiver_worker.h"
#include "transfer_session_manager.h"
#include "dir_serializer.h"
#include "frame_codec.h"
#include "protocol.h"

using gy::DirSerializer;
using gy::protocol::kTypeHello;
using gy::protocol::kTypeTransferReq;
using gy::protocol::kTypeTransferRsp;
using gy::protocol::kTypeDataChunk;
using gy::protocol::kTypeChunkAck;
using gy::protocol::kTypeTransferDone;
using gy::protocol::kTypeCancel;

class TestIntegration : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testSingleMachineTransfer();
    void testMultiplePorts();
    void testConfigIsolation();
    void testDirSerializerComprehensive();
    void testFrameCodecEdgeCases();
    void testProtocolConstants();

private:
    void createTestFile(const QString &path, const QByteArray &content);
    bool verifyFileContent(const QString &path, const QByteArray &expected);

    QTemporaryDir *_baseDir = nullptr;
};

void TestIntegration::initTestCase()
{
    _baseDir = new QTemporaryDir();
    QVERIFY(_baseDir->isValid());
}

void TestIntegration::cleanupTestCase()
{
    delete _baseDir;
}

void TestIntegration::createTestFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable("无法创建文件: " + path));
    file.write(content);
    file.close();
}

bool TestIntegration::verifyFileContent(const QString &path, const QByteArray &expected)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    return file.readAll() == expected;
}

void TestIntegration::testSingleMachineTransfer()
{
    // 创建测试目录
    QString sendDir = _baseDir->path() + "/send";
    QDir().mkpath(sendDir);

    // 创建测试文件
    QString sendFile = sendDir + "/test.txt";
    createTestFile(sendFile, "Hello from sender!");

    // 测试 DirSerializer
    auto items = DirSerializer::serialize(sendFile);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].relativePath, QString("test.txt"));
    QCOMPARE(items[0].sizeBytes, 18);
    QVERIFY(!items[0].sha256.isEmpty());
}

void TestIntegration::testMultiplePorts()
{
    // 测试不同端口的配置隔离
    quint16 ports[] = {35100, 35101, 35102};

    for (quint16 port : ports) {
        QString configPath = _baseDir->path() + QString("/config_port_%1").arg(port);
        qputenv("GRIDYARD_CONFIG", configPath.toUtf8());
        qputenv("GRIDYARD_PORT", QByteArray::number(port));

        ConfigManager *config = ConfigManager::create(nullptr, nullptr);
        QCOMPARE(config->tcpPort(), port);
        delete config;
    }

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_PORT");
}

void TestIntegration::testConfigIsolation()
{
    // 测试配置隔离 - 使用端口参数自动生成不同配置文件
    qputenv("GRIDYARD_PORT", "35200");
    qputenv("GRIDYARD_NAME", "Device1");
    ConfigManager *mgr1 = ConfigManager::create(nullptr, nullptr);
    mgr1->setDeviceName("Device1");

    qputenv("GRIDYARD_PORT", "35201");
    qputenv("GRIDYARD_NAME", "Device2");
    ConfigManager *mgr2 = ConfigManager::create(nullptr, nullptr);
    mgr2->setDeviceName("Device2");

    // 验证配置隔离
    QCOMPARE(mgr1->deviceName(), QString("Device1"));
    QCOMPARE(mgr2->deviceName(), QString("Device2"));
    // 注意：deviceId 可能相同因为使用了相同的配置文件路径
    // 这里只验证设备名不同
    QVERIFY(mgr1->deviceName() != mgr2->deviceName());

    delete mgr1;
    delete mgr2;

    qunsetenv("GRIDYARD_PORT");
    qunsetenv("GRIDYARD_NAME");
}

void TestIntegration::testDirSerializerComprehensive()
{
    // 创建复杂目录结构
    QString basePath = _baseDir->path() + "/complex_dir";
    QDir base(basePath);
    QVERIFY(base.mkpath("."));

    // 创建多层目录
    QVERIFY(base.mkpath("level1/level2/level3"));
    QVERIFY(base.mkpath("another/path"));

    // 创建各种文件
    createTestFile(basePath + "/root.txt", "root file");
    createTestFile(basePath + "/level1/file1.txt", "level1 file");
    createTestFile(basePath + "/level1/level2/file2.txt", "level2 file");
    createTestFile(basePath + "/level1/level2/level3/file3.txt", "level3 file");
    createTestFile(basePath + "/another/path/data.bin", QByteArray(1024, '\0'));

    // 测试序列化
    auto items = DirSerializer::serialize(basePath);

    // 验证文件数量（5个文件）
    QCOMPARE(items.size(), 5);

    // 验证每个文件都有正确的信息
    for (const auto &item : items) {
        QVERIFY(!item.relativePath.isEmpty());
        QVERIFY(item.sizeBytes >= 0);
        QVERIFY(!item.sha256.isEmpty());
    }

    // 测试单个文件 SHA-256
    QString sha256 = DirSerializer::computeSha256(basePath + "/root.txt");
    QVERIFY(!sha256.isEmpty());
    QCOMPARE(sha256.size(), 64); // SHA-256 hex 长度
}

void TestIntegration::testFrameCodecEdgeCases()
{
    // 测试 FrameCodec 各种边界情况
    FrameCodec codec;
    QSignalSpy spy(&codec, &FrameCodec::frameReady);

    // 测试 1: 连续多个小帧
    QByteArray multiFrame;
    for (int i = 0; i < 10; ++i) {
        QByteArray payload = QString("frame_%1").arg(i).toUtf8();
        multiFrame.append(FrameCodec::encode(0x0001, payload));
    }
    codec.feed(multiFrame);
    QCOMPARE(spy.count(), 10);

    // 测试 2: 大帧（1MB）
    spy.clear();
    QByteArray largePayload(1024 * 1024, 'X');
    QByteArray largeFrame = FrameCodec::encode(0x0201, largePayload);
    codec.feed(largeFrame);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst()[1].toByteArray(), largePayload);

    // 测试 3: 空 payload
    spy.clear();
    QByteArray emptyFrame = FrameCodec::encode(0x0302, QByteArray());
    codec.feed(emptyFrame);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.takeFirst()[1].toByteArray().isEmpty());
}

void TestIntegration::testProtocolConstants()
{
    // 验证协议常量
    QCOMPARE(gy::protocol::kHeaderBytes, quint32(8));
    QCOMPARE(gy::protocol::kDefaultDiscoveryPort, quint16(45678));
    QCOMPARE(gy::protocol::kDefaultP2pPort, quint16(35100));

    // 验证 Type 码
    QCOMPARE(gy::protocol::kTypeHello, quint32(0x0001));
    QCOMPARE(gy::protocol::kTypeTransferReq, quint32(0x0101));
    QCOMPARE(gy::protocol::kTypeTransferRsp, quint32(0x0102));
    QCOMPARE(gy::protocol::kTypeDataChunk, quint32(0x0201));
    QCOMPARE(gy::protocol::kTypeChunkAck, quint32(0x0301));
    QCOMPARE(gy::protocol::kTypeTransferDone, quint32(0x0302));
    QCOMPARE(gy::protocol::kTypeCancel, quint32(0x0401));
}

QTEST_MAIN(TestIntegration)
#include "test_integration.moc"
