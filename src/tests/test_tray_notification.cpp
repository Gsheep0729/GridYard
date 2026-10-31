/**
 * @file    test_tray_notification.cpp
 * @version 8.2.1
 * @date 2026-11-01
 * @author  GridYard Team
 * @brief   托盘通知送达与点击路由回归测试（C-17）
 *
 * 以桩上下文属性（AppController/ConfigManager）从源码树加载 TrayIcon.qml，
 * 验证通知入口 notify* 记录最近一次通知的类别与目标设备、托盘不可用时
 * 统一跳过 showMessage、点击路由 routeMessageClicked 仅对聊天/传输完成
 * 两类上抛动作信号，其余类别与无记录点击保持无动作兜底。
 */

#include <QtTest/QtTest>
#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QUrl>
#include <QVariant>

namespace {

// 桩：发现视图模型（TrayIcon 在线设备快照只读 peers，测试恒为空列表）
class StubPeerDiscoveryViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList peers READ peers CONSTANT)
public:
    explicit StubPeerDiscoveryViewModel(QObject *parent = nullptr)
        : QObject(parent) {}
    QVariantList peers() const { return {}; }
};

// 桩：传输控制器（tooltip 绑定读活动会话数）
class StubTransferController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int activeSessionCount READ activeSessionCount CONSTANT)
public:
    explicit StubTransferController(QObject *parent = nullptr)
        : QObject(parent) {}
    int activeSessionCount() const { return 0; }
};

// 桩：应用控制器（TrayIcon 引用发现视图模型、传输控制器与实例标题后缀）
class StubAppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *peerDiscoveryViewModel READ peerDiscoveryViewModel CONSTANT)
    Q_PROPERTY(QObject *transferController READ transferController CONSTANT)
    Q_PROPERTY(QString instanceTitleSuffix READ instanceTitleSuffix CONSTANT)
public:
    explicit StubAppController(QObject *parent = nullptr)
        : QObject(parent),
          _peers(new StubPeerDiscoveryViewModel(this)),
          _transfer(new StubTransferController(this)) {}
    QObject *peerDiscoveryViewModel() const { return _peers; }
    QObject *transferController() const { return _transfer; }
    QString instanceTitleSuffix() const { return QString(); }
private:
    StubPeerDiscoveryViewModel *_peers;
    StubTransferController *_transfer;
};

// 桩：配置管理器（勿扰开关被 tooltip 绑定与托盘菜单读写）
class StubConfigManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool doNotDisturb READ doNotDisturb
               WRITE setDoNotDisturb NOTIFY doNotDisturbChanged)
public:
    explicit StubConfigManager(QObject *parent = nullptr)
        : QObject(parent) {}
    bool doNotDisturb() const { return _doNotDisturb; }
    void setDoNotDisturb(bool on) {
        if (_doNotDisturb != on) {
            _doNotDisturb = on;
            emit doNotDisturbChanged();
        }
    }
signals:
    void doNotDisturbChanged();
private:
    bool _doNotDisturb = false;
};

} // namespace

class TrayNotificationTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void chatNotificationClickRoutesDeviceId();
    void transferCompletedNotificationClickHasOwnKind();
    void infoNotificationClickHasNoAction();
    void replacedKindIsNotRerouted();
    void clickWithoutPriorNotifyDoesNothing();
    void notifyRecordsKindEvenWhenTrayUnavailable();

private:
    QObject *loadTray();
    bool invokeNotify0(QObject *tray, const char *method);
    bool invokeNotify2(QObject *tray, const char *method,
                       const QString &a1, const QString &a2);
    bool invokeNotify3(QObject *tray, const char *method,
                       const QString &a1, const QString &a2, const QString &a3);

    QQmlEngine _engine;
    StubAppController _appController;
    StubConfigManager _configManager;
    QList<QObject *> _loaded;
};

void TrayNotificationTest::init()
{
    _engine.rootContext()->setContextProperty("AppController", &_appController);
    _engine.rootContext()->setContextProperty("ConfigManager", &_configManager);
}

void TrayNotificationTest::cleanup()
{
    for (QObject *obj : _loaded) {
        if (obj) {
            obj->deleteLater();
        }
    }
    _loaded.clear();
    _engine.clearComponentCache();
}

// 每个用例从源码树新加载一份 TrayIcon.qml，返回根对象（SystemTrayIcon），
// 失败（状态非 Ready 或创建为空）时返回空指针，由用例自行断言
QObject *TrayNotificationTest::loadTray()
{
    const QString path = QDir(GRIDYARD_SOURCE_DIR).filePath(
                QStringLiteral("client/ui/TrayIcon.qml"));
    QQmlComponent component(&_engine, QUrl::fromLocalFile(path));
    if (component.status() != QQmlComponent::Ready) {
        qWarning() << "TrayIcon.qml 加载失败：" << component.errorString();
        return nullptr;
    }
    QObject *tray = component.create();
    _loaded.append(tray);
    return tray;
}

// 调用 QML 函数：带类型注解的 QML 方法元签名以 QString 形参注册，
// 按实参个数分派（QMetaObject::invokeMethod 不做实参省略匹配）
bool TrayNotificationTest::invokeNotify0(QObject *tray, const char *method)
{
    return QMetaObject::invokeMethod(tray, method);
}

bool TrayNotificationTest::invokeNotify2(QObject *tray, const char *method,
                                         const QString &a1, const QString &a2)
{
    return QMetaObject::invokeMethod(tray, method,
                                     Q_ARG(QString, a1),
                                     Q_ARG(QString, a2));
}

bool TrayNotificationTest::invokeNotify3(QObject *tray, const char *method,
                                         const QString &a1, const QString &a2,
                                         const QString &a3)
{
    return QMetaObject::invokeMethod(tray, method,
                                     Q_ARG(QString, a1),
                                     Q_ARG(QString, a2),
                                     Q_ARG(QString, a3));
}

// 聊天气泡点击路由：上抛 chatMessageClicked 且载荷为记录的设备 ID
void TrayNotificationTest::chatNotificationClickRoutesDeviceId()
{
    QObject *tray = loadTray();
    QVERIFY(tray);
    QSignalSpy chatSpy(tray, SIGNAL(chatMessageClicked(QString)));
    QSignalSpy completeSpy(tray, SIGNAL(transferCompletedClicked()));
    QVERIFY(chatSpy.isValid());
    QVERIFY(completeSpy.isValid());

    QVERIFY(invokeNotify3(tray, "notifyChatMessage",
                         QStringLiteral("device-a"),
                         QStringLiteral("张三"),
                         QStringLiteral("在吗")));
    QVERIFY(invokeNotify0(tray, "routeMessageClicked"));

    QCOMPARE(chatSpy.count(), 1);
    QCOMPARE(chatSpy.takeFirst().at(0).toString(), QStringLiteral("device-a"));
    QCOMPARE(completeSpy.count(), 0);
}

// 传输完成气泡点击路由：上抛 transferCompletedClicked，不误报聊天动作
void TrayNotificationTest::transferCompletedNotificationClickHasOwnKind()
{
    QObject *tray = loadTray();
    QVERIFY(tray);
    QSignalSpy chatSpy(tray, SIGNAL(chatMessageClicked(QString)));
    QSignalSpy completeSpy(tray, SIGNAL(transferCompletedClicked()));
    QVERIFY(chatSpy.isValid());
    QVERIFY(completeSpy.isValid());

    QVERIFY(invokeNotify2(tray, "notifyTransferCompleted",
                         QStringLiteral("传输完成"),
                         QStringLiteral("已完成一项文件传输")));
    QVERIFY(invokeNotify0(tray, "routeMessageClicked"));

    QCOMPARE(completeSpy.count(), 1);
    QCOMPARE(chatSpy.count(), 0);
}

// 普通提示（后台运行/传输请求/本地失败）无点击语义：路由不动作
void TrayNotificationTest::infoNotificationClickHasNoAction()
{
    QObject *tray = loadTray();
    QVERIFY(tray);
    QSignalSpy chatSpy(tray, SIGNAL(chatMessageClicked(QString)));
    QSignalSpy completeSpy(tray, SIGNAL(transferCompletedClicked()));
    QVERIFY(chatSpy.isValid());
    QVERIFY(completeSpy.isValid());

    QVERIFY(invokeNotify2(tray, "notifyInfo",
                         QStringLiteral("GridYard"),
                         QStringLiteral("应用仍在后台运行")));
    QVERIFY(invokeNotify0(tray, "routeMessageClicked"));

    QCOMPARE(chatSpy.count(), 0);
    QCOMPARE(completeSpy.count(), 0);
}

// 类别被后续通知覆盖后点击只按最近一次路由：先聊天后普通提示不再上抛聊天动作
void TrayNotificationTest::replacedKindIsNotRerouted()
{
    QObject *tray = loadTray();
    QVERIFY(tray);
    QSignalSpy chatSpy(tray, SIGNAL(chatMessageClicked(QString)));
    QSignalSpy completeSpy(tray, SIGNAL(transferCompletedClicked()));
    QVERIFY(chatSpy.isValid());
    QVERIFY(completeSpy.isValid());

    QVERIFY(invokeNotify3(tray, "notifyChatMessage",
                         QStringLiteral("device-a"),
                         QStringLiteral("张三"),
                         QStringLiteral("在吗")));
    QVERIFY(invokeNotify2(tray, "notifyInfo",
                         QStringLiteral("GridYard"),
                         QStringLiteral("应用仍在后台运行")));
    QVERIFY(invokeNotify0(tray, "routeMessageClicked"));

    QCOMPARE(chatSpy.count(), 0);
    QCOMPARE(completeSpy.count(), 0);
}

// 无任何通知记录时点击（如后端重放）不动作，保持无操作兜底
void TrayNotificationTest::clickWithoutPriorNotifyDoesNothing()
{
    QObject *tray = loadTray();
    QVERIFY(tray);
    QSignalSpy chatSpy(tray, SIGNAL(chatMessageClicked(QString)));
    QSignalSpy completeSpy(tray, SIGNAL(transferCompletedClicked()));
    QVERIFY(chatSpy.isValid());
    QVERIFY(completeSpy.isValid());

    QVERIFY(invokeNotify0(tray, "routeMessageClicked"));

    QCOMPARE(chatSpy.count(), 0);
    QCOMPARE(completeSpy.count(), 0);
}

// 托盘不可用（离屏平台 available 为 false）时 notify* 不崩溃且类别仍被记录，
// 供后端缺席场景下的点击路由语义保持确定
void TrayNotificationTest::notifyRecordsKindEvenWhenTrayUnavailable()
{
    QObject *tray = loadTray();
    QVERIFY(tray);
    // 离屏平台无托盘后端：available 应为 false，showMessage 不应被触达
    if (tray->property("available").toBool()) {
        QSKIP("当前平台托盘可用，跳过不可用分支断言");
    }

    QVERIFY(invokeNotify3(tray, "notifyChatMessage",
                         QStringLiteral("device-b"),
                         QStringLiteral("李四"),
                         QStringLiteral("收到")));
    QCOMPARE(tray->property("_lastMessageKind").toString(),
             QStringLiteral("chat"));
    QCOMPARE(tray->property("_lastChatDeviceId").toString(),
             QStringLiteral("device-b"));

    QVERIFY(invokeNotify2(tray, "notifyInfo",
                         QStringLiteral("GridYard"),
                         QStringLiteral("应用仍在后台运行")));
    QCOMPARE(tray->property("_lastMessageKind").toString(), QString());
}

int main(int argc, char *argv[])
{
    // 默认压到离屏平台：托盘不可用分支可复现，也避免测试在桌面会话里
    // 真弹系统气泡；显式设置过 QT_QPA_PLATFORM 时以外部取值为准
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);
    TrayNotificationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_tray_notification.moc"
