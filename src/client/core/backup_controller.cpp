/**
* @file    backup_controller.cpp
* @version 7.23.0
* @date 2026-10-07
* @author  GridYard Team
* @brief   用户数据备份与迁移控制器实现
*/

#include "backup_controller.h"

#include "config_manager.h"
#include "local_data_broker.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFileDevice>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QSet>

namespace {
constexpr int kBackupFormatVersion = 1;  // 备份包格式版本：只升不降，导入向后兼容
const QString kBackupFormatId = QStringLiteral("gridyard-backup");

// 备份文件默认保存目录：用户文档目录，取不到时回退主目录
QString defaultBackupDirectory()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}

// 备份 JSON 的时间字段统一为 UTC ISO 文本
QDateTime timeFromJson(const QJsonValue &value)
{
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}

QString timeToJson(const QDateTime &time)
{
    return time.toUTC().toString(Qt::ISODateWithMs);
}
}

// 构造函数
BackupController::BackupController(LocalDataBroker *dataBroker, ConfigManager *config,
                                   QObject *parent)
    : QObject{parent}
    , _dataBroker{dataBroker}
    , _config{config}
{
}

// 获取控制器是否正在执行导出或导入任务
bool BackupController::busy() const
{
    return _busy;
}

// 设置忙碌状态并通知界面
void BackupController::setBusy(bool busy)
{
    if (_busy == busy) {
        return;
    }
    _busy = busy;
    emit busyChanged();
}

// 身份层决策：导入身份与本机相同保留；本机目录尚无任何数据（全新安装）
// 则写入配置待重启生效；本机已有不同身份的记录时拒绝身份层、其余层正常导入
BackupController::IdentityAction BackupController::decideIdentityAction(
        const QString &localDeviceId, bool localLibraryHasData, const QString &importedDeviceId)
{
    if (importedDeviceId.isEmpty() || localDeviceId == importedDeviceId) {
        return IdentityAction::Keep;
    }
    return localLibraryHasData ? IdentityAction::Reject : IdentityAction::Replace;
}

// 收集导出数据与各层条目数：设备目录必带，聊天与传输一并收集供计数展示
void BackupController::prepareExport()
{
    if (_busy) {
        return;
    }
    setBusy(true);
    _dataBroker->loadBackupData(this, true, true,
                                [this](const QList<PeerRecord> &devices,
                                       const QList<MessageRecord> &messages,
                                       const QList<TransferRecord> &transfers, bool succeeded) {
        setBusy(false);
        if (!succeeded) {
            emit exportFinished(false, QString(), QStringLiteral("本地历史库不可用，无法导出"));
            return;
        }
        _exportDevices = devices;
        _exportMessages = messages;
        _exportTransfers = transfers;

        const QString defaultPath = defaultBackupDirectory() + QStringLiteral("/gridyard-backup-")
                                    + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"))
                                    + QStringLiteral(".json");
        QVariantMap summary;
        summary.insert(QStringLiteral("devices"), devices.size());
        summary.insert(QStringLiteral("messages"), messages.size());
        summary.insert(QStringLiteral("transfers"), transfers.size());
        summary.insert(QStringLiteral("defaultPath"), defaultPath);
        emit exportPrepared(summary);
    });
}

// 将最近一次导出数据按勾选层写入目标路径（明文 JSON，覆盖同名文件）
void BackupController::writeExportFile(const QString &path, bool includeIdentity,
                                       bool includeChat, bool includeTransfers)
{
    if (path.trimmed().isEmpty()) {
        emit exportFinished(false, path, QStringLiteral("请选择保存位置"));
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), kBackupFormatId);
    root.insert(QStringLiteral("version"), kBackupFormatVersion);
    root.insert(QStringLiteral("exportedAt"), timeToJson(QDateTime::currentDateTimeUtc()));

    if (includeIdentity && _config) {
        QJsonObject identity;
        identity.insert(QStringLiteral("deviceId"), _config->deviceId());
        identity.insert(QStringLiteral("deviceName"), _config->deviceName());
        root.insert(QStringLiteral("identity"), identity);
    }

    QJsonArray devices;
    for (const PeerRecord &record : _exportDevices) {
        devices.append(deviceToJson(record));
    }
    root.insert(QStringLiteral("devices"), devices);

    if (includeChat) {
        QJsonArray chat;
        for (const MessageRecord &record : _exportMessages) {
            chat.append(messageToJson(record));
        }
        root.insert(QStringLiteral("chat"), chat);
    }

    if (includeTransfers) {
        QJsonArray transfers;
        for (const TransferRecord &record : _exportTransfers) {
            transfers.append(transferToJson(record));
        }
        root.insert(QStringLiteral("transfers"), transfers);
    }

    // 默认路径的父目录可能尚不存在（如全新系统的文档目录），写出前先补建
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        emit exportFinished(false, path, QStringLiteral("无法创建保存目录：%1").arg(info.absolutePath()));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit exportFinished(false, path, QStringLiteral("无法写入文件：%1").arg(file.errorString()));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    emit exportFinished(true, path, QString());
}

// 解析备份文件并统计各层条目与冲突数，结果经 importAnalyzed 发布
void BackupController::analyzeImportFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit importAnalyzed({{QStringLiteral("error"), QStringLiteral("无法读取文件：%1")
                                                           .arg(file.errorString())}});
        return;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonObject root;
    QString error;
    if (!parseBackupJson(raw, &root, &error)) {
        emit importAnalyzed({{QStringLiteral("error"), error}});
        return;
    }

    // 逐条转换并丢弃坏行，坏行计数并入预览提示
    _importIdentity = root.value(QStringLiteral("identity")).toObject();
    QList<PeerRecord> devices;
    QList<MessageRecord> messages;
    QList<TransferRecord> transfers;
    int invalid = 0;
    const QJsonArray deviceArray = root.value(QStringLiteral("devices")).toArray();
    for (const QJsonValue &value : deviceArray) {
        const PeerRecord record = deviceFromJson(value.toObject());
        if (record.deviceId.isEmpty()) {
            ++invalid;
        } else {
            devices.append(record);
        }
    }
    const QJsonArray chatArray = root.value(QStringLiteral("chat")).toArray();
    for (const QJsonValue &value : chatArray) {
        MessageRecord record;
        if (!messageFromJson(value.toObject(), &record)) {
            ++invalid;
        } else {
            messages.append(record);
        }
    }
    const QJsonArray transferArray = root.value(QStringLiteral("transfers")).toArray();
    for (const QJsonValue &value : transferArray) {
        TransferRecord record;
        if (!transferFromJson(value.toObject(), &record)) {
            ++invalid;
        } else {
            transfers.append(record);
        }
    }
    _importDevices = deviceArray;
    _importMessages = chatArray;
    _importTransfers = transferArray;

    setBusy(true);
    _dataBroker->analyzeBackupRecords(
        this, devices, messages, transfers,
        [this, invalid](const QVariantMap &stats, bool succeeded) {
            setBusy(false);
            QVariantMap summary{stats};
            summary.insert(QStringLiteral("invalid"), invalid);
            summary.insert(QStringLiteral("devices"), _importDevices.size());
            summary.insert(QStringLiteral("messages"), _importMessages.size());
            summary.insert(QStringLiteral("transfers"), _importTransfers.size());
            summary.insert(QStringLiteral("hasIdentity"), !_importIdentity.isEmpty()
                                                           && !_importIdentity.value(
                                                                  QStringLiteral("deviceId"))
                                                                   .toString().isEmpty());
            if (succeeded) {
                const bool libraryHasData =
                        stats.value(QStringLiteral("libraryDevices")).toInt() > 0
                        || stats.value(QStringLiteral("libraryMessages")).toInt() > 0
                        || stats.value(QStringLiteral("libraryTransfers")).toInt() > 0;
                summary.insert(QStringLiteral("libraryHasData"), libraryHasData);
                const IdentityAction action = decideIdentityAction(
                    _config ? _config->deviceId() : QString(), libraryHasData,
                    _importIdentity.value(QStringLiteral("deviceId")).toString());
                _lastIdentityAction =
                        action == IdentityAction::Keep ? QStringLiteral("keep")
                        : (action == IdentityAction::Replace ? QStringLiteral("replace")
                                                             : QStringLiteral("reject"));
                summary.insert(QStringLiteral("identityAction"), _lastIdentityAction);
            } else {
                summary.insert(QStringLiteral("error"), QStringLiteral("本地历史库不可用"));
            }
            emit importAnalyzed(summary);
        });
}

// 执行导入：设备/聊天/传输单事务落库，身份层按预览时的决策分支处理
void BackupController::performImport()
{
    if (_busy) {
        return;
    }

    // 重新从缓存的 JSON 数组转换记录，解析失败行维持分析时的丢弃口径
    QList<PeerRecord> devices;
    for (const QJsonValue &value : _importDevices) {
        const PeerRecord record = deviceFromJson(value.toObject());
        if (!record.deviceId.isEmpty()) {
            devices.append(record);
        }
    }
    QList<MessageRecord> messages;
    for (const QJsonValue &value : _importMessages) {
        MessageRecord record;
        if (messageFromJson(value.toObject(), &record)) {
            messages.append(record);
        }
    }
    QList<TransferRecord> transfers;
    for (const QJsonValue &value : _importTransfers) {
        TransferRecord record;
        if (transferFromJson(value.toObject(), &record)) {
            transfers.append(record);
        }
    }
    if (devices.isEmpty() && messages.isEmpty() && transfers.isEmpty()) {
        emit importFinished(false, {}, QStringLiteral("备份文件中没有可导入的数据"));
        return;
    }

    // 聊天与传输经由设备目录外键关联：为文件中引用而本机尚不存在的对端
    // 补建最小离线条目（peer_devices 行），保证任意层组合都能通过外键约束
    QSet<QString> knownIds;
    for (const PeerRecord &record : devices) {
        knownIds.insert(record.deviceId);
    }
    QList<PeerRecord> stubs;
    const auto ensureStub = [&](const QString &deviceId, const QString &fallbackName) {
        if (!deviceId.isEmpty() && !knownIds.contains(deviceId)) {
            knownIds.insert(deviceId);
            PeerRecord stub;
            stub.deviceId = deviceId;
            stub.deviceName = fallbackName;
            stub.lastSeenAt = QDateTime::currentDateTimeUtc();
            stub.firstSeenAt = stub.lastSeenAt;
            stubs.append(stub);
        }
    };
    for (const MessageRecord &record : messages) {
        ensureStub(record.peerDeviceId, record.senderName);
    }
    for (const TransferRecord &record : transfers) {
        ensureStub(record.peerDeviceId, record.peerName);
    }
    devices.append(stubs);

    // 身份层处理沿用预览决策：replace 写入配置待重启生效，reject 跳过
    const QString identityAction = _lastIdentityAction;
    if (identityAction == QStringLiteral("replace") && _config) {
        _config->restoreDeviceIdentity(
            _importIdentity.value(QStringLiteral("deviceId")).toString(),
            _importIdentity.value(QStringLiteral("deviceName")).toString());
    }

    setBusy(true);
    _dataBroker->importBackupRecords(
        this, devices, messages, transfers,
        [this, identityAction](const QVariantMap &stats, bool succeeded) {
            setBusy(false);
            QVariantMap summary{stats};
            summary.insert(QStringLiteral("identityAction"), identityAction);
            emit importFinished(succeeded, summary,
                                succeeded ? QString()
                                          : QStringLiteral("导入失败，已整体回滚，本机数据未改动"));
        });
}

// 设备记录转备份 JSON 条目
QJsonObject BackupController::deviceToJson(const PeerRecord &record)
{
    return QJsonObject{
        {QStringLiteral("deviceId"), record.deviceId},
        {QStringLiteral("deviceName"), record.deviceName},
        {QStringLiteral("alias"), record.alias},
        {QStringLiteral("pinned"), record.pinned},
        {QStringLiteral("hidden"), record.hidden},
        {QStringLiteral("favorite"), record.favorite},
        {QStringLiteral("lastSeenAt"), timeToJson(record.lastSeenAt)},
    };
}

// 备份 JSON 条目转设备记录：deviceId 为空的条目视为无效
PeerRecord BackupController::deviceFromJson(const QJsonObject &obj)
{
    PeerRecord record;
    record.deviceId = obj.value(QStringLiteral("deviceId")).toString().trimmed();
    if (record.deviceId.isEmpty()) {
        return {};
    }
    record.deviceName = obj.value(QStringLiteral("deviceName")).toString();
    record.alias = obj.value(QStringLiteral("alias")).toString();
    record.pinned = obj.value(QStringLiteral("pinned")).toBool();
    record.hidden = obj.value(QStringLiteral("hidden")).toBool();
    record.favorite = obj.value(QStringLiteral("favorite")).toBool();
    record.lastSeenAt = timeFromJson(obj.value(QStringLiteral("lastSeenAt")));
    if (!record.lastSeenAt.isValid()) {
        record.lastSeenAt = QDateTime::currentDateTimeUtc();
    }
    record.firstSeenAt = record.lastSeenAt;
    return record;
}

// 聊天记录转备份 JSON 条目
QJsonObject BackupController::messageToJson(const MessageRecord &record)
{
    return QJsonObject{
        {QStringLiteral("messageId"), record.messageId},
        {QStringLiteral("peerDeviceId"), record.peerDeviceId},
        {QStringLiteral("direction"), static_cast<int>(record.direction)},
        {QStringLiteral("senderDeviceId"), record.senderDeviceId},
        {QStringLiteral("senderName"), record.senderName},
        {QStringLiteral("content"), record.content},
        {QStringLiteral("sentAt"), timeToJson(record.sentAt)},
        {QStringLiteral("localStatus"), record.localStatus},
        {QStringLiteral("createdAt"), timeToJson(record.createdAt)},
    };
}

// 备份 JSON 条目转聊天记录：messageId/peerDeviceId/sentAt 齐备才有效
bool BackupController::messageFromJson(const QJsonObject &obj, MessageRecord *out)
{
    MessageRecord record;
    record.messageId = obj.value(QStringLiteral("messageId")).toString().trimmed();
    record.peerDeviceId = obj.value(QStringLiteral("peerDeviceId")).toString().trimmed();
    record.sentAt = timeFromJson(obj.value(QStringLiteral("sentAt")));
    if (record.messageId.isEmpty() || record.peerDeviceId.isEmpty()
            || !record.sentAt.isValid()) {
        return false;
    }
    const int direction = obj.value(QStringLiteral("direction")).toInt();
    if (direction != 0 && direction != 1) {
        return false;
    }
    record.direction = static_cast<RecordDirection>(direction);
    record.senderDeviceId = obj.value(QStringLiteral("senderDeviceId")).toString();
    record.senderName = obj.value(QStringLiteral("senderName")).toString();
    record.content = obj.value(QStringLiteral("content")).toString();
    record.localStatus = obj.value(QStringLiteral("localStatus")).toInt();
    record.createdAt = timeFromJson(obj.value(QStringLiteral("createdAt")));
    if (!record.createdAt.isValid()) {
        record.createdAt = record.sentAt;
    }
    *out = record;
    return true;
}

// 传输历史转备份 JSON 条目
QJsonObject BackupController::transferToJson(const TransferRecord &record)
{
    return QJsonObject{
        {QStringLiteral("recordId"), record.recordId},
        {QStringLiteral("sessionId"), record.sessionId},
        {QStringLiteral("peerDeviceId"), record.peerDeviceId},
        {QStringLiteral("peerName"), record.peerName},
        {QStringLiteral("direction"), static_cast<int>(record.direction)},
        {QStringLiteral("displayName"), record.displayName},
        {QStringLiteral("isDirectory"), record.isDirectory},
        {QStringLiteral("fileCount"), record.fileCount},
        {QStringLiteral("totalBytes"), static_cast<qint64>(record.totalBytes)},
        {QStringLiteral("status"), record.status},
        {QStringLiteral("startedAt"), timeToJson(record.startedAt)},
        {QStringLiteral("finishedAt"),
         record.finishedAt.isValid() ? timeToJson(record.finishedAt) : QString()},
        {QStringLiteral("errorCode"), record.errorCode},
        {QStringLiteral("errorMessage"), record.errorMessage},
    };
}

// 备份 JSON 条目转传输历史：recordId/sessionId/startedAt 齐备才有效
bool BackupController::transferFromJson(const QJsonObject &obj, TransferRecord *out)
{
    TransferRecord record;
    record.recordId = obj.value(QStringLiteral("recordId")).toString().trimmed();
    record.sessionId = obj.value(QStringLiteral("sessionId")).toString().trimmed();
    record.startedAt = timeFromJson(obj.value(QStringLiteral("startedAt")));
    if (record.recordId.isEmpty() || record.sessionId.isEmpty()
            || !record.startedAt.isValid()) {
        return false;
    }
    const int direction = obj.value(QStringLiteral("direction")).toInt();
    if (direction != 0 && direction != 1) {
        return false;
    }
    record.direction = static_cast<RecordDirection>(direction);
    record.peerDeviceId = obj.value(QStringLiteral("peerDeviceId")).toString().trimmed();
    record.peerName = obj.value(QStringLiteral("peerName")).toString();
    record.displayName = obj.value(QStringLiteral("displayName")).toString();
    record.isDirectory = obj.value(QStringLiteral("isDirectory")).toBool();
    record.fileCount = obj.value(QStringLiteral("fileCount")).toInt();
    record.totalBytes = static_cast<qint64>(obj.value(QStringLiteral("totalBytes")).toDouble());
    record.status = obj.value(QStringLiteral("status")).toString();
    record.finishedAt = timeFromJson(obj.value(QStringLiteral("finishedAt")));
    record.errorCode = obj.value(QStringLiteral("errorCode")).toInt();
    record.errorMessage = obj.value(QStringLiteral("errorMessage")).toString();
    *out = record;
    return true;
}

// 校验备份包格式并解析根对象：format 标识必须匹配，版本号不得高于当前支持
bool BackupController::parseBackupJson(const QByteArray &raw, QJsonObject *root, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("不是有效的备份文件（JSON 解析失败）");
        return false;
    }
    const QJsonObject obj = document.object();
    if (obj.value(QStringLiteral("format")).toString() != kBackupFormatId) {
        *error = QStringLiteral("不是 GridYard 备份文件");
        return false;
    }
    const int version = obj.value(QStringLiteral("version")).toInt();
    if (version < 1 || version > kBackupFormatVersion) {
        *error = QStringLiteral("备份文件版本过新，请先升级应用");
        return false;
    }
    *root = obj;
    return true;
}
