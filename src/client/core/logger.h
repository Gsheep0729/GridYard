/**
* @file    logger.h
* @version 6.6.2
* @date    2026-06-25
* @author  GridYard Team
* @brief   运行日志工具（拦截 Qt 日志输出到文件）
*
* 单例模式，使用 qInstallMessageHandler 拦截所有 qDebug/qWarning/
* qCritical/qInfo 输出，同时写入控制台和日志文件。
* 日志文件按日期自动命名（gridyard_yyyyMMdd.log），
* 存放在应用数据目录 logs/ 文件夹下。线程安全。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.0.0] GY   2026-06-25
* * 日志默认目录迁移到应用数据目录
* [v4.16.1] GY   2026-06-21
* * 删除未使用的 logFilePath() 访问器
* [v4.7.1] GY   2026-06-06
* * 初始版本：文件输出 + 控制台输出 + 线程安全
*/

#pragma once

#include <QMutex>
#include <QObject>
#include <QString>
#include <QFile>
#include <QTextStream>

class Logger : public QObject {
private:
    Q_OBJECT

public:
    static Logger *instance();

    // 初始化日志系统，logDir 为空则使用应用数据目录
    void init(const QString &logDir = QString());

private:
    explicit Logger(QObject *parent = nullptr);
    // 析构函数
    virtual ~Logger() override;

    // Qt 消息处理回调
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &ctx,
                               const QString &msg);

    // 写入日志（线程安全）
    void writeLog(QtMsgType type, const QString &formatted);

    // 打开当天的日志文件
    void openLogFile();

    // 格式化日志级别
    static QString levelString(QtMsgType type);

    static Logger *_instance;  // 进程内唯一日志实例

    QFile _logFile;            // 当前日期对应的日志文件
    QTextStream _stream;       // 日志文件的文本写入流
    QString _logDir;           // 日志文件所在目录
    mutable QMutex _mutex;     // 保护文件切换和并发写入
};
