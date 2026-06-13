/**
* @file    logger.cpp
* @version 4.10.0
* @date    2026-06-13
* @author  GY
* @brief   Logger 实现
*
* Change Log:
* [v4.7.0] GY   2026-06-05
* * 初始版本：文件输出 + 控制台输出 + 线程安全
*/

#include "logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <cstdio>

Logger *Logger::_instance = nullptr;

Logger::Logger(QObject *parent)
    : QObject(parent) {
}

Logger::~Logger() {
    QMutexLocker locker(&_mutex);
    if (_logFile.isOpen()) {
        _stream.flush();
        _logFile.close();
    }
}

Logger *Logger::instance() {
    if (!_instance) {
        _instance = new Logger;
    }
    return _instance;
}

void Logger::init(const QString &logDir) {
    QMutexLocker locker(&_mutex);

    if (logDir.isEmpty()) {
        // 默认日志目录：仓库根目录下的 logs 文件夹
        // 可执行文件路径：src/build/client/appGridYard
        // 向上 3 级到达仓库根目录 (src/)
        _logDir = QCoreApplication::applicationDirPath() + "/../../../logs";
    } else {
        _logDir = logDir;
    }

    // 规范化路径
    QDir dir(_logDir);
    _logDir = dir.absolutePath();

    if (!dir.exists()) {
        bool ok = dir.mkpath(".");
        fprintf(stderr, "Logger: 创建日志目录 %s (%s)\n",
                _logDir.toLocal8Bit().constData(),
                ok ? "成功" : "失败");
    }

    openLogFile();

    qInstallMessageHandler(messageHandler);
}

QString Logger::logFilePath() const {
    QMutexLocker locker(&_mutex);
    return _logFile.fileName();
}

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

void Logger::messageHandler(QtMsgType type,
                            const QMessageLogContext &ctx,
                            const QString &msg) {
    Logger *logger = Logger::instance();

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString level = levelString(type);

    // 提取模块名（从 [ModuleName] 格式中）
    QString module;
    QString content = msg;
    int start = msg.indexOf('[');
    int end = msg.indexOf(']');
    if (start >= 0 && end > start) {
        module = msg.mid(start + 1, end - start - 1);
        content = msg.mid(end + 1).trimmed();
    }

    // 格式化日志行
    QString formatted;
    if (module.isEmpty()) {
        formatted = QString("[%1] [%2] %3")
                        .arg(timestamp, level, msg);
    } else {
        formatted = QString("[%1] [%2] [%3] %4")
                        .arg(timestamp, level, module, content);
    }

    // 同时输出到控制台和文件
    logger->writeLog(type, formatted);
}

void Logger::writeLog(QtMsgType type, const QString &formatted) {
    QMutexLocker locker(&_mutex);

    // 控制台输出（带颜色）
    FILE *output = (type >= QtWarningMsg) ? stderr : stdout;
    fprintf(output, "%s\n", formatted.toLocal8Bit().constData());
    fflush(output);

    // 检查是否需要切换到新日期的文件
    QString currentDate = QDateTime::currentDateTime().toString("yyyyMMdd");
    if (!_logFile.fileName().contains(currentDate)) {
        openLogFile();
    }

    // 文件输出
    if (_logFile.isOpen()) {
        _stream << formatted << "\n";
        _stream.flush();
    }
}
