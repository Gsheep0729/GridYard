/**
 * @file    app_controller.h
 * @date    2026-05-24
 * @author  GY
 * @brief   应用全局控制器（QML 单例形式暴露）
 *
 * 演示规范 §七.3 推荐的"全局服务"暴露方式：用 QML_SINGLETON 而
 * 非 setContextProperty。AppController 是 QML 引擎按需 create
 * 出来的，所有权归引擎，无需手动 delete。
 * 该类本身只承担"应用元信息 + 关闭意图"等极简职责，避免成为
 * 上帝对象。真实项目中具体业务由它注入的下层模块完成。
 *
 * Change Log:
 * [v1.0] GY   2026-05-24
 * * Initial creation
 */

#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString applicationName    READ applicationName    CONSTANT)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)

public:
    // QML_SINGLETON 必需的工厂；引擎调用，外界不应直接 new
    static AppController *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    QString applicationName()    const;
    QString applicationVersion() const;

    Q_INVOKABLE void quit();

signals:
    void appReady();

private:
    explicit AppController(QObject *parent = nullptr);
    AppController(const AppController &)            = delete;
    AppController &operator=(const AppController &) = delete;
};
