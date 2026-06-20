/**
* @file    logger.h
* @version 4.7.0
* @date    2026-06-13
* @author  GridYard Team
* @brief   运行日志工具（拦截 Qt 日志输出到文件）
*
* 单例模式，使用 qInstallMessageHandler 拦截所有 qDebug/qWarning/
* qCritical/qInfo 输出，同时写入控制台和日志文件。
* 日志文件按日期自动命名（gridyard_yyyyMMdd.log），
* 存放在项目根目录 logs/ 文件夹下。线程安全。
*
* Change Log:
* [v4.7.0] GY   2026-06-05
* * 初始版本：文件输出 + 控制台输出 + 线程安全
*/

#pragma once

#include <QMutex>
#include <QObject>
#include <QString>
#include <QFile>
#include <QTextStream>

class Logger : public QObject {
    Q_OBJECT

public:
    static Logger *instance();

    // 初始化日志系统，logDir 为空则使用默认路径
    void init(const QString &logDir = QString());

    // 获取当前日志文件路径
    QString logFilePath() const;

private:
    explicit Logger(QObject *parent = nullptr);
    ~Logger() override;

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

    static Logger *_instance;

    QFile _logFile;
    QTextStream _stream;
    QString _logDir;
    mutable QMutex _mutex;
};
