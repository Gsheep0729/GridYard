/**
* @file    logger.cpp
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   运行日志工具实现
*
* 使用 qInstallMessageHandler 拦截 Qt 日志输出，同时写入控制台和文件。
* 日志文件按日期自动命名，支持跨天自动切换。线程安全（QMutex）。
*/

#include "logger.h"
#include "application_paths.h"

#include <QDateTime>
#include <QDir>
#include <cstdio>

// 静态实例指针
Logger *Logger::_instance = nullptr;

// 构造函数
Logger::Logger(QObject *parent)
    : QObject(parent) {
}

// 析构函数：关闭日志文件
Logger::~Logger() {
    QMutexLocker locker(&_mutex);
    if (_logFile.isOpen()) {
        _stream.flush();
        _logFile.close();
    }
}

// 获取单例实例
Logger *Logger::instance() {
    if (!_instance) {
        _instance = new Logger;
    }
    return _instance;
}

// 初始化日志系统，安装消息处理器，logDir 为空时使用应用数据目录
void Logger::init(const QString &logDir) {
    QMutexLocker locker(&_mutex);  // 加锁保护文件切换

    if (logDir.isEmpty()) {
        _logDir = ApplicationPaths::logDir();  // 默认使用应用数据目录下的 logs 子目录
    } else {
        _logDir = logDir;
    }

    // 规范化路径，避免不同路径表示方式导致的文件比较问题
    QDir dir(_logDir);
    _logDir = dir.absolutePath();

    if (!dir.exists()) {
        bool ok = dir.mkpath(".");  // 创建日志目录，首次启动可能不存在
        fprintf(stderr, "Logger: 创建日志目录 %s (%s)\n",
                _logDir.toLocal8Bit().constData(),
                ok ? "成功" : "失败");
    }

    openLogFile();  // 打开当天的日志文件

    qInstallMessageHandler(messageHandler);  // 拦截所有 Qt 日志输出
}

// 关闭日志系统，释放当前日志文件句柄
void Logger::shutdown() {
    QMutexLocker locker(&_mutex);
    qInstallMessageHandler(nullptr);
    if (_logFile.isOpen()) {
        _stream.flush();
        _stream.setDevice(nullptr);
        _logFile.close();
    }
}

// 打开当天的日志文件（按日期自动命名）
void Logger::openLogFile() {
    if (_logFile.isOpen()) {
        _stream.flush();
        _logFile.close();
    }

    QString dateStr = QDateTime::currentDateTime().toString("yyyyMMdd");
    QString filePath = _logDir + "/gridyard_" + dateStr + ".log";

    _logFile.setFileName(filePath);
    if (!_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "无法打开日志文件: %s\n", filePath.toLocal8Bit().constData());
    }
    _stream.setDevice(&_logFile);
}

// 格式化日志级别字符串
QString Logger::levelString(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO";
    case QtWarningMsg:  return "WARNING";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    default:            return "UNKNOWN";
    }
}

// Qt 消息处理回调（拦截所有 qDebug/qWarning/qCritical 输出，全局唯一入口）
void Logger::messageHandler(QtMsgType type,
                            const QMessageLogContext &ctx,
                            const QString &msg) {
    Logger *logger = Logger::instance();

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString level = levelString(type);

    // 从消息中提取模块名（仅当消息以 [Module] 开头时），便于日志过滤；
    // QML 引擎的告警不带分类前缀、消息体内自带方括号（如 Unable to assign [undefined] to QColor），
    // 不能从中间截取，否则前半段报错会被当成模块名吞掉
    QString module;
    QString content = msg;
    if (msg.startsWith('[')) {
        int start = msg.indexOf('[');
        int end = msg.indexOf(']');
        if (start == 0 && end > start) {
            module = msg.mid(1, end - 1);
            content = msg.mid(end + 1).trimmed();
        }
    }

    // 格式化日志行：有模块名时显示模块标签，无模块名时直接拼接原始消息
    QString formatted;
    if (module.isEmpty()) {
        formatted = QString("[%1] [%2] %3")
                        .arg(timestamp, level, msg);
    } else {
        formatted = QString("[%1] [%2] [%3] %4")
                        .arg(timestamp, level, module, content);
    }

    logger->writeLog(type, formatted);  // 同时输出到控制台和文件
}

// 写入日志（同时输出到控制台和文件，QMutex 保证线程安全）
void Logger::writeLog(QtMsgType type, const QString &formatted) {
    QMutexLocker locker(&_mutex);

    // Warning 及以上级别输出到 stderr，其余输出到 stdout
    FILE *output = (type >= QtWarningMsg) ? stderr : stdout;
    fprintf(output, "%s\n", formatted.toLocal8Bit().constData());
    fflush(output);

    // 跨天时自动切换到新日期的日志文件
    QString currentDate = QDateTime::currentDateTime().toString("yyyyMMdd");
    if (!_logFile.fileName().contains(currentDate)) {
        openLogFile();
    }

    // 文件输出（每次写入后立即 flush，避免崩溃时丢失最后几行日志）
    if (_logFile.isOpen()) {
        _stream << formatted << "\n";
        _stream.flush();
    }
}
