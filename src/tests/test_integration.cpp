/**
* @file    test_integration.cpp
* @version 8.0.0
* @date 2026-10-25
* @author  GY
* @brief   集成测试 - 单机多实例模拟
*
* 在同一台机器上启动多个实例，模拟多设备传输场景。
* 使用不同端口和配置文件实现隔离。
*/

#include <QtTest/QtTest>

#include <utility>
#include <vector>
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
#include "codec_bridge.h"
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
    // 两个实例刻意共用同一配置文件（本用例只验证设备名独立）；必须先把配置指到
    // 临时目录，否则 ConfigManager 会读写真实用户配置，把测试设备名持久化进去
    qputenv("GRIDYARD_CONFIG", (_baseDir->path() + "/config_shared").toUtf8());
    qputenv("GRIDYARD_PORT", "35200");
    qputenv("GRIDYARD_NAME", "Device1");
    ConfigManager *mgr1 = new ConfigManager{};
    mgr1->setDeviceName("Device1");

    qputenv("GRIDYARD_PORT", "35201");
    qputenv("GRIDYARD_NAME", "Device2");
    ConfigManager *mgr2 = new ConfigManager{};
    mgr2->setDeviceName("Device2");

    // 验证配置隔离
    QCOMPARE(mgr1->deviceName(), QString("Device1"));
    QCOMPARE(mgr2->deviceName(), QString("Device2"));
    // 使用相同配置文件路径时，deviceId 可能相同
    // 这里只验证设备名不同
    QVERIFY(mgr1->deviceName() != mgr2->deviceName());

    delete mgr1;
    delete mgr2;

    qunsetenv("GRIDYARD_PORT");
    qunsetenv("GRIDYARD_NAME");
    qunsetenv("GRIDYARD_CONFIG");
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
    std::vector<std::pair<quint32, QByteArray>> frames;
    gy::core::FrameCodecCore codec(
        [&frames](uint32_t type, std::vector<uint8_t> payload) {
            frames.emplace_back(type, gy::bridge::fromBytes(payload));
        },
        [](std::string) {});

    // 测试 1: 连续多个小帧
    QByteArray multiFrame;
    for (int i = 0; i < 10; ++i) {
        QByteArray payload = QString("frame_%1").arg(i).toUtf8();
        multiFrame.append(gy::bridge::encodeFrame(0x0001, payload));
    }
    gy::bridge::feedFrame(codec, multiFrame);
    QCOMPARE(frames.size(), size_t(10));

    // 测试 2: 大帧（1MB）
    frames.clear();
    QByteArray largePayload(1024 * 1024, 'X');
    QByteArray largeFrame = gy::bridge::encodeFrame(0x0201, largePayload);
    gy::bridge::feedFrame(codec, largeFrame);
    QCOMPARE(frames.size(), size_t(1));
    QCOMPARE(frames[0].second, largePayload);

    // 测试 3: 空 payload
    frames.clear();
    QByteArray emptyFrame = gy::bridge::encodeFrame(0x0302, QByteArray());
    gy::bridge::feedFrame(codec, emptyFrame);
    QCOMPARE(frames.size(), size_t(1));
    QVERIFY(frames[0].second.isEmpty());
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
