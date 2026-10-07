/**
* @file    test_backup.cpp
* @version 7.23.0
* @date 2026-10-07
* @author  GY
* @brief   用户数据备份与迁移测试
*
* 测试用例：三层 JSON 备份包导出往返（含 Unicode 备注）、导入幂等、
* 冲突字段级合并规则、身份层替换/拒绝分支、导入事务失败整体回滚。
*/

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "backup_controller.h"
#include "config_manager.h"
#include "db_seed.h"
#include "local_data_broker.h"
#include "sqlite_device_repository.h"
#include "sqlite_message_repository.h"
#include "sqlite_transfer_history_repository.h"

class TestBackup : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testExportImportRoundTripWithUnicode();
    void testImportIsIdempotent();
    void testConflictMergeRules();
    void testIdentityRejectedWhenLibraryHasData();
    void testTransactionRollbackOnForeignKey();

private:
    // 在临时目录打开一份本地历史库
    std::unique_ptr<LocalDataBroker> openBroker(const QString &fileName);
    // 直写种子数据到库文件（绕开代管者异步接口，精确控制管理列与记录内容）
    void seedDatabase(const QString &fileName, const QList<PeerRecord> &devices,
                      const QList<MessageRecord> &messages = {},
                      const QList<TransferRecord> &transfers = {});
    // 把 GRIDYARD_CONFIG 切到全新文件（新实例将生成独立身份）
    void useFreshConfig(const QString &name);
    // 构造一份设备记录
    PeerRecord makeDevice(const QString &deviceId, const QString &name,
                          const QDateTime &seen);
    // 构造一条入站聊天消息
    MessageRecord makeMessage(const QString &peerId, const QString &messageId,
                              const QDateTime &sentAt);
    // 构造一条已完成传输记录
    TransferRecord makeTransfer(const QString &peerId, const QString &recordId,
                                const QDateTime &startedAt);
    // 等待导出数据就绪，成功时把摘要写入 out（QTRY 宏不可用于非 void 函数）
    bool waitForExport(BackupController &controller, QVariantMap *out);

    QTemporaryDir *_tempDir = nullptr;
    ConfigManager *_config = nullptr;
};

void TestBackup::initTestCase()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    useFreshConfig("config-a.ini");
    qputenv("GRIDYARD_NAME", "BackupTest");
    _config = new ConfigManager{};
}

void TestBackup::cleanupTestCase()
{
    delete _config;
    delete _tempDir;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

// 导出往返（含 Unicode 备注）与全新库导入：设备关系、聊天、传输逐字段一致，
// 全新库 + 不同身份触发身份替换分支
void TestBackup::testExportImportRoundTripWithUnicode()
{
    // 源库写入含 Unicode 备注的设备、聊天与传输记录
    const QDateTime base = QDateTime::fromString("2026-10-06T08:00:00.000Z", Qt::ISODateWithMs);
    PeerRecord device = makeDevice("backup-dev-1", "备份设备甲", base);
    device.alias = QStringLiteral("老王的电脑✦");
    device.favorite = true;
    PeerRecord device2 = makeDevice("backup-dev-2", "备份设备乙", base.addSecs(60));
    MessageRecord message = makeMessage("backup-dev-1", "backup-msg-1", base.addSecs(120));
    message.senderName = QStringLiteral("张三");
    message.content = QStringLiteral("你好，世界 🌏");
    const TransferRecord transfer = makeTransfer("backup-dev-2", "backup-rec-1", base.addSecs(180));
    seedDatabase("backup-source.sqlite", {device, device2}, {message}, {transfer});

    auto broker = openBroker("backup-source.sqlite");
    QVERIFY(broker);

    BackupController exporter(broker.get(), _config, this);
    QVariantMap summary;
    QVERIFY(waitForExport(exporter, &summary));
    QCOMPARE(summary.value("devices").toInt(), 2);
    QCOMPARE(summary.value("messages").toInt(), 1);
    QCOMPARE(summary.value("transfers").toInt(), 1);

    // 写出备份文件并校验是合法 JSON
    const QString backupPath = _tempDir->path() + "/roundtrip.json";
    QSignalSpy exportSpy(&exporter, &BackupController::exportFinished);
    exporter.writeExportFile(backupPath, true, true, true);
    QCOMPARE(exportSpy.count(), 1);
    QVERIFY(exportSpy.first().first().toBool());
    QVERIFY(QFile::exists(backupPath));

    // 全新库 + 独立配置文件（自动生成的新身份）：导入前应给出 replace 决策
    auto freshBroker = openBroker("backup-fresh.sqlite");
    QVERIFY(freshBroker);
    useFreshConfig("config-b.ini");
    ConfigManager freshConfig;
    QVERIFY2(freshConfig.deviceId() != _config->deviceId(), "新配置应生成不同身份");
    BackupController importer(freshBroker.get(), &freshConfig, this);

    QSignalSpy analyzedSpy(&importer, &BackupController::importAnalyzed);
    importer.analyzeImportFile(backupPath);
    QTRY_COMPARE(analyzedSpy.count(), 1);
    const QVariantMap analyzed = analyzedSpy.first().first().toMap();
    QCOMPARE(analyzed.value("error"), QVariant());
    QCOMPARE(analyzed.value("devices").toInt(), 2);
    QCOMPARE(analyzed.value("deviceConflicts").toInt(), 0);
    QCOMPARE(analyzed.value("messageConflicts").toInt(), 0);
    QCOMPARE(analyzed.value("identityAction").toString(), QStringLiteral("replace"));

    QSignalSpy importSpy(&importer, &BackupController::importFinished);
    importer.performImport();
    QTRY_COMPARE(importSpy.count(), 1);
    QVERIFY(importSpy.first().first().toBool());
    QCOMPARE(importSpy.first().at(1).toMap().value("identityAction").toString(),
             QStringLiteral("replace"));

    // 身份已写入配置文件（运行期不切换，新实例读配置可见）
    ConfigManager restoredConfig;
    QCOMPARE(restoredConfig.deviceId(), _config->deviceId());

    // 导入库全量回读：记录逐字段一致（含 Unicode 备注与消息内容）
    const auto loaded = QSharedPointer<QVariantMap>::create();
    auto verifyLoaded = [this, loaded, &base](const QList<PeerRecord> &devices,
                                              const QList<MessageRecord> &messages,
                                              const QList<TransferRecord> &transfers,
                                              bool ok) {
        QVERIFY(ok);
        QCOMPARE(devices.size(), 2);
        for (const PeerRecord &record : devices) {
            if (record.deviceId == QStringLiteral("backup-dev-1")) {
                QCOMPARE(record.alias, QStringLiteral("老王的电脑✦"));
                QCOMPARE(record.favorite, true);
            }
        }
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().content, QStringLiteral("你好，世界 🌏"));
        QCOMPARE(messages.first().senderName, QStringLiteral("张三"));
        QCOMPARE(messages.first().sentAt, base.addSecs(120));
        QCOMPARE(transfers.size(), 1);
        QCOMPARE(transfers.first().recordId, QStringLiteral("backup-rec-1"));
        *loaded = QVariantMap{{QStringLiteral("done"), true}};
    };
    freshBroker->loadBackupData(this, true, true, verifyLoaded);
    QTRY_COMPARE(loaded->value("done").toBool(), true);
}

// 同一备份文件导入两次结果一致：重复条目全部跳过，库状态不再变化
void TestBackup::testImportIsIdempotent()
{
    auto broker = openBroker("backup-idempotent.sqlite");
    QVERIFY(broker);

    const QDateTime base = QDateTime::fromString("2026-10-06T09:00:00.000Z", Qt::ISODateWithMs);
    seedDatabase("backup-idempotent.sqlite",
                 {makeDevice("idem-dev", "幂等设备", base)},
                 {makeMessage("idem-dev", "idem-msg", base.addSecs(30))},
                 {makeTransfer("idem-dev", "idem-rec", base.addSecs(60))});
    BackupController exporter(broker.get(), _config, this);
    QVariantMap exportSummary;
    QVERIFY(waitForExport(exporter, &exportSummary));
    const QString backupPath = _tempDir->path() + "/idempotent.json";
    exporter.writeExportFile(backupPath, false, true, true);

    useFreshConfig("config-d.ini");
    auto target = openBroker("backup-idempotent-target.sqlite");
    QVERIFY(target);
    ConfigManager targetConfig;
    BackupController importer(target.get(), &targetConfig, this);
    importer.analyzeImportFile(backupPath);
    QTRY_VERIFY(!importer.busy());

    QSignalSpy firstImport(&importer, &BackupController::importFinished);
    importer.performImport();
    QTRY_COMPARE(firstImport.count(), 1);
    const QVariantMap firstStats = firstImport.first().at(1).toMap();
    QCOMPARE(firstStats.value("messagesAdded").toInt(), 1);
    QCOMPARE(firstStats.value("transfersAdded").toInt(), 1);

    // 二次导入：全部命中已有条目，新增为零
    importer.analyzeImportFile(backupPath);
    QTRY_VERIFY(!importer.busy());
    QSignalSpy secondImport(&importer, &BackupController::importFinished);
    importer.performImport();
    QTRY_COMPARE(secondImport.count(), 1);
    const QVariantMap secondStats = secondImport.first().at(1).toMap();
    QCOMPARE(secondStats.value("messagesAdded").toInt(), 0);
    QCOMPARE(secondStats.value("transfersAdded").toInt(), 0);

    // 两次导入后记录数与一次导入后一致
    const auto counts = QSharedPointer<QVariantMap>::create();
    target->loadBackupData(this, true, true,
                           [counts](const QList<PeerRecord> &devices,
                                    const QList<MessageRecord> &messages,
                                    const QList<TransferRecord> &transfers, bool ok) {
                               QVERIFY(ok);
                               counts->insert(QStringLiteral("devices"), devices.size());
                               counts->insert(QStringLiteral("messages"), messages.size());
                               counts->insert(QStringLiteral("transfers"), transfers.size());
                           });
    QTRY_COMPARE(counts->value("devices").toInt(), 1);
    QCOMPARE(counts->value("messages").toInt(), 1);
    QCOMPARE(counts->value("transfers").toInt(), 1);
}

// 冲突字段级合并：备注取非空一方、置顶/隐藏/收藏取或、last_seen 取新
void TestBackup::testConflictMergeRules()
{
    const QDateTime base = QDateTime::fromString("2026-10-06T10:00:00.000Z", Qt::ISODateWithMs);
    PeerRecord exported = makeDevice("conflict-dev", "冲突设备", base);
    exported.alias = QStringLiteral("备份备注");
    exported.favorite = true;
    exported.hidden = true;
    seedDatabase("backup-conflict-source.sqlite", {exported});

    auto source = openBroker("backup-conflict-source.sqlite");
    QVERIFY(source);
    BackupController exporter(source.get(), _config, this);
    QVERIFY(waitForExport(exporter, nullptr));
    const QString backupPath = _tempDir->path() + "/conflict.json";
    exporter.writeExportFile(backupPath, false, true, true);

    // 本机已有同 ID 设备：备注非空、置顶为真、last_seen 更新
    PeerRecord local = makeDevice("conflict-dev", "本机设备", base.addSecs(600));
    local.alias = QStringLiteral("本机备注");
    local.pinned = true;
    seedDatabase("backup-conflict-target.sqlite", {local});
    auto target = openBroker("backup-conflict-target.sqlite");
    QVERIFY(target);

    useFreshConfig("config-e.ini");
    ConfigManager targetConfig;
    BackupController importer(target.get(), &targetConfig, this);
    QSignalSpy analyzedSpy(&importer, &BackupController::importAnalyzed);
    importer.analyzeImportFile(backupPath);
    QTRY_COMPARE(analyzedSpy.count(), 1);
    QCOMPARE(analyzedSpy.first().first().toMap().value("deviceConflicts").toInt(), 1);

    QSignalSpy importSpy(&importer, &BackupController::importFinished);
    importer.performImport();
    QTRY_COMPARE(importSpy.count(), 1);
    QVERIFY(importSpy.first().first().toBool());

    const auto merged = QSharedPointer<PeerRecord>::create();
    target->loadRecentPeers(this, 10,
                            [merged](const QList<PeerRecord> &records, bool ok) {
                                QVERIFY(ok);
                                QCOMPARE(records.size(), 1);
                                *merged = records.first();
                            });
    QTRY_COMPARE(merged->deviceId, QStringLiteral("conflict-dev"));
    QCOMPARE(merged->alias, QStringLiteral("本机备注"));      // 非空一方保留
    QCOMPARE(merged->pinned, true);                          // 取或
    QCOMPARE(merged->favorite, true);                        // 取或
    QCOMPARE(merged->hidden, true);                          // 取或
    QCOMPARE(merged->deviceName, QStringLiteral("本机设备")); // 已有名称不被覆盖
    QCOMPARE(merged->lastSeenAt, base.addSecs(600));         // last_seen 取新
}

// 本机已有不同身份的使用记录时拒绝身份层，其余层正常导入
void TestBackup::testIdentityRejectedWhenLibraryHasData()
{
    const QDateTime base = QDateTime::fromString("2026-10-06T11:00:00.000Z", Qt::ISODateWithMs);
    seedDatabase("backup-reject-source.sqlite",
                 {makeDevice("reject-dev", "拒绝设备", base)});
    auto source = openBroker("backup-reject-source.sqlite");
    QVERIFY(source);

    BackupController exporter(source.get(), _config, this);
    QVERIFY(waitForExport(exporter, nullptr));
    const QString backupPath = _tempDir->path() + "/reject.json";
    exporter.writeExportFile(backupPath, true, false, false);

    // 目标库已有数据（另一台设备行），身份不同
    seedDatabase("backup-reject-target.sqlite",
                 {makeDevice("local-dev", "本地已有设备", base)});
    useFreshConfig("config-c.ini");
    auto target = openBroker("backup-reject-target.sqlite");
    QVERIFY(target);
    ConfigManager targetConfig;
    const QString localIdentity = targetConfig.deviceId();
    QVERIFY2(localIdentity != _config->deviceId(), "两个配置应持不同身份");

    BackupController importer(target.get(), &targetConfig, this);
    QSignalSpy analyzedSpy(&importer, &BackupController::importAnalyzed);
    importer.analyzeImportFile(backupPath);
    QTRY_COMPARE(analyzedSpy.count(), 1);
    QCOMPARE(analyzedSpy.first().first().toMap().value("identityAction").toString(),
             QStringLiteral("reject"));

    QSignalSpy importSpy(&importer, &BackupController::importFinished);
    importer.performImport();
    QTRY_COMPARE(importSpy.count(), 1);
    QVERIFY(importSpy.first().first().toBool());

    // 备份设备已导入，本机身份未被替换
    const auto devices = QSharedPointer<QStringList>::create();
    target->loadRecentPeers(this, 10, [devices](const QList<PeerRecord> &records, bool ok) {
        QVERIFY(ok);
        for (const PeerRecord &record : records) {
            *devices << record.deviceId;
        }
    });
    QTRY_VERIFY(devices->contains(QStringLiteral("reject-dev")));
    ConfigManager unchangedConfig;
    QCOMPARE(unchangedConfig.deviceId(), localIdentity);
}

// 导入事务失败整体回滚：外键无法满足时设备行也不得残留
void TestBackup::testTransactionRollbackOnForeignKey()
{
    auto target = openBroker("backup-rollback.sqlite");
    QVERIFY(target);

    // 直接走数据层导入：消息引用了设备清单之外的对端（本接口不建 stub，
    // 会话行外键无法满足），整个事务应失败并回滚
    const QDateTime base = QDateTime::fromString("2026-10-06T12:00:00.000Z", Qt::ISODateWithMs);
    QList<PeerRecord> devices{makeDevice("rollback-dev", "回滚设备", base)};
    QList<MessageRecord> messages{makeMessage("ghost-dev", "rollback-msg", base.addSecs(30))};

    QSignalSpy finishedSpy(target.get(), &LocalDataBroker::operationFailed);
    const auto called = QSharedPointer<bool>::create(false);
    const auto result = QSharedPointer<QPair<bool, QVariantMap>>::create();
    target->importBackupRecords(this, devices, messages, {},
                                [called, result](const QVariantMap &stats, bool succeeded) {
                                    *called = true;
                                    *result = {succeeded, stats};
                                });
    QTRY_VERIFY(*called);
    QVERIFY(!result->first);
    QTRY_VERIFY(finishedSpy.count() >= 1);

    // 设备行未残留（事务回滚），库仍可用
    const auto devicesAfter = QSharedPointer<QStringList>::create();
    target->loadRecentPeers(this, 10, [devicesAfter](const QList<PeerRecord> &records, bool ok) {
        QVERIFY(ok);
        for (const PeerRecord &record : records) {
            *devicesAfter << record.deviceId;
        }
    });
    QTRY_COMPARE(devicesAfter->contains(QStringLiteral("rollback-dev")), false);
}

// 工具方法：在临时目录打开一份本地历史库
std::unique_ptr<LocalDataBroker> TestBackup::openBroker(const QString &fileName)
{
    auto broker = std::make_unique<LocalDataBroker>();
    QString error;
    if (!broker->initialize(_tempDir->path() + "/" + fileName, &error)) {
        qWarning() << "本地历史库初始化失败:" << error;
        return nullptr;
    }
    return broker;
}

// 工具方法：直写种子数据到库文件（独立连接，写入即提交）
void TestBackup::seedDatabase(const QString &fileName, const QList<PeerRecord> &devices,
                              const QList<MessageRecord> &messages,
                              const QList<TransferRecord> &transfers)
{
    auto database = gy::test::openDatabase(_tempDir->path(), fileName);
    QVERIFY(database);
    SqliteDeviceRepository deviceRepository(database.get());
    SqliteMessageRepository messageRepository(database.get());
    SqliteTransferHistoryRepository transferRepository(database.get());
    QString error;
    for (const PeerRecord &record : devices) {
        QVERIFY2(deviceRepository.upsertPeer(record, &error), qPrintable(error));
        // upsert 刻意不写管理列（防心跳清标记），种子数据用独立 UPDATE 补齐
        QSqlQuery management(database->connectionForWorkerThread(&error));
        management.prepare("UPDATE peer_devices SET alias = ?, pinned = ?, hidden = ?, "
                           "favorite = ? WHERE device_id = ?");
        management.addBindValue(record.alias);
        management.addBindValue(record.pinned ? 1 : 0);
        management.addBindValue(record.hidden ? 1 : 0);
        management.addBindValue(record.favorite ? 1 : 0);
        management.addBindValue(record.deviceId);
        QVERIFY2(management.exec(), qPrintable(management.lastError().text()));
    }
    for (const MessageRecord &record : messages) {
        QVERIFY2(messageRepository.saveMessage(record, &error), qPrintable(error));
    }
    for (const TransferRecord &record : transfers) {
        QVERIFY2(transferRepository.upsertFinishedTransfer(record, &error), qPrintable(error));
    }
}

// 工具方法：把 GRIDYARD_CONFIG 切到全新文件（新实例将生成独立身份）
void TestBackup::useFreshConfig(const QString &name)
{
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/" + name).toUtf8());
}

// 工具方法：构造一份设备记录
PeerRecord TestBackup::makeDevice(const QString &deviceId, const QString &name,
                                  const QDateTime &seen)
{
    PeerRecord record;
    record.deviceId = deviceId;
    record.deviceName = name;
    record.lastIpAddress = QStringLiteral("192.168.90.1");
    record.lastTcpPort = 35100;
    record.firstSeenAt = seen;
    record.lastSeenAt = seen;
    return record;
}

// 工具方法：构造一条入站聊天消息
MessageRecord TestBackup::makeMessage(const QString &peerId, const QString &messageId,
                                      const QDateTime &sentAt)
{
    MessageRecord record;
    record.messageId = messageId;
    record.peerDeviceId = peerId;
    record.direction = RecordDirection::Incoming;
    record.senderDeviceId = peerId;
    record.senderName = QStringLiteral("对端设备");
    record.content = QStringLiteral("测试消息");
    record.sentAt = sentAt;
    record.createdAt = sentAt;
    return record;
}

// 工具方法：构造一条已完成传输记录
TransferRecord TestBackup::makeTransfer(const QString &peerId, const QString &recordId,
                                        const QDateTime &startedAt)
{
    TransferRecord record;
    record.recordId = recordId;
    record.sessionId = recordId + QStringLiteral("-session");
    record.peerDeviceId = peerId;
    record.peerName = QStringLiteral("对端设备");
    record.direction = RecordDirection::Outgoing;
    record.displayName = QStringLiteral("演示文件.txt");
    record.fileCount = 1;
    record.totalBytes = 2048;
    record.status = QStringLiteral("完成");
    record.startedAt = startedAt;
    record.finishedAt = startedAt.addSecs(12);
    return record;
}

// 工具方法：等待导出数据就绪，成功时把摘要写入 out
bool TestBackup::waitForExport(BackupController &controller, QVariantMap *out)
{
    QSignalSpy spy(&controller, &BackupController::exportPrepared);
    controller.prepareExport();
    if (!spy.wait(5000) || out == nullptr) {
        return !spy.isEmpty();
    }
    *out = spy.first().first().toMap();
    return true;
}

QTEST_MAIN(TestBackup)
#include "test_backup.moc"
