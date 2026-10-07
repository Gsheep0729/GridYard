/**
* @file    backup_controller.h
* @version 7.23.0
* @date 2026-10-07
* @author  GridYard Team
* @brief   用户数据备份与迁移控制器
*
* 组织三层 JSON 备份包（L-身份 / L-设备关系配置 / L-历史）的导出与导入：
* 数据收集与单事务落库委托 LocalDataBroker，JSON 组装与身份层决策在本控制器，
* 文件读写为明文 JSON（不含敏感文件内容，仅设备身份与本地记录）。
*/

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "history_records.h"

class ConfigManager;
class LocalDataBroker;

class BackupController : public QObject {
private:
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit BackupController(LocalDataBroker *dataBroker, ConfigManager *config,
                              QObject *parent = nullptr);
    virtual ~BackupController() override = default;

    BackupController(const BackupController &) = delete;
    BackupController &operator=(const BackupController &) = delete;

    // 获取控制器是否正在执行导出或导入任务
    bool busy() const;

    // 收集导出数据与各层条目数，完成后经 exportPrepared 携带默认路径发布
    Q_INVOKABLE void prepareExport();
    // 将最近一次导出数据按勾选层写入目标路径（明文 JSON，覆盖同名文件）
    Q_INVOKABLE void writeExportFile(const QString &path, bool includeIdentity,
                                     bool includeChat, bool includeTransfers);
    // 解析备份文件并统计各层条目与冲突数，结果经 importAnalyzed 发布
    Q_INVOKABLE void analyzeImportFile(const QString &path);
    // 执行导入：设备/聊天/传输单事务落库，身份层按决策分支处理
    Q_INVOKABLE void performImport();

signals:
    void busyChanged();
    // 导出数据就绪：各层条目数与默认保存路径
    void exportPrepared(const QVariantMap &summary);
    void exportFinished(bool success, const QString &path, const QString &error);
    // 导入预览：各层条目数、与本机目录的冲突数、身份层动作（keep/replace/reject）
    void importAnalyzed(const QVariantMap &summary);
    // 导入完成：summary 含各层导入条数与身份层结果
    void importFinished(bool success, const QVariantMap &summary, const QString &error);

private:
    // 身份层动作：相同保留；本机目录尚无任何数据（全新安装）则替换；已有数据拒绝
    enum class IdentityAction {
        Keep,     // 导入身份与本机一致，无需处理
        Replace,  // 写入配置，重启后以导入身份启动
        Reject,   // 本机已有不同身份的记录，拒绝身份层、其余层正常导入
    };

    // 身份层决策
    static IdentityAction decideIdentityAction(const QString &localDeviceId,
                                               bool localLibraryHasData,
                                               const QString &importedDeviceId);
    // 校验备份包格式并解析根对象（format 标识与版本号检查）
    static bool parseBackupJson(const QByteArray &raw, QJsonObject *root, QString *error);
    // 领域记录与备份 JSON 的双向转换（纯函数，供导出组装与导入解析复用）
    static QJsonObject deviceToJson(const PeerRecord &record);
    static PeerRecord deviceFromJson(const QJsonObject &obj);
    static QJsonObject messageToJson(const MessageRecord &record);
    static bool messageFromJson(const QJsonObject &obj, MessageRecord *out);
    static QJsonObject transferToJson(const TransferRecord &record);
    static bool transferFromJson(const QJsonObject &obj, TransferRecord *out);

    void setBusy(bool busy);

    LocalDataBroker *_dataBroker = nullptr;  // 本地数据层入口（收集与单事务导入）
    ConfigManager *_config = nullptr;        // 本机身份读取与身份恢复写入
    bool _busy = false;                      // 是否有存储任务在途
    QList<PeerRecord> _exportDevices;        // 最近一次导出收集的设备目录
    QList<MessageRecord> _exportMessages;    // 最近一次导出收集的聊天记录
    QList<TransferRecord> _exportTransfers;  // 最近一次导出收集的传输历史
    QJsonArray _importDevices;               // 最近一次解析出的设备条目
    QJsonArray _importMessages;              // 最近一次解析出的聊天条目
    QJsonArray _importTransfers;             // 最近一次解析出的传输条目
    QJsonObject _importIdentity;             // 最近一次解析出的身份层（可能为空）
    QString _lastIdentityAction;             // 预览得出的身份层动作（keep/replace/reject）
};
