/**
 * @file    test_keyboard_access.cpp
 * @version 8.2.1
 * @date 2026-11-01
 * @author  GridYard Team
 * @brief   键盘可达性回归测试（O-23）
 *
 * 以桩上下文属性与桩 QML 模块（cqnu.gridyard.client 哑导出）从源码树加载
 * DeviceCard.qml 与 Sidebar.qml，在离屏窗口上驱动真实按键：
 * - 设备卡 Tab 可聚焦（修前 ItemDelegate 默认 NoFocus，Tab 永远到不了），
 *   聚焦后 Return/Enter 与左键点击走同一 cardClicked 路径，Space 由
 *   AbstractButton 原生处理，聚焦态 visualFocus 可驱动焦点环；
 * - 侧栏本机头像与菜单按钮（自绘 Rectangle+TapHandler，默认不在焦点链）
 *   Tab 可被遍历到，Return/Enter/Space 打开对应弹窗。
 */

#include <QtTest/QtTest>
#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>

namespace {

// 桩：发现视图模型（侧栏列表绑定与添加设备回调用，恒空列表、恒不忙）
class StubPeerDiscoveryViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList peers READ peers CONSTANT)
    Q_PROPERTY(QVariantList searchResults READ searchResults CONSTANT)
    Q_PROPERTY(bool searchBusy READ searchBusy CONSTANT)
    Q_PROPERTY(QString selectedDeviceId READ selectedDeviceId CONSTANT)
    Q_PROPERTY(int recentTotalCount READ recentTotalCount CONSTANT)
    Q_PROPERTY(bool recentExpanded READ recentExpanded CONSTANT)
    Q_PROPERTY(int recentVisibleLimit READ recentVisibleLimit CONSTANT)
public:
    explicit StubPeerDiscoveryViewModel(QObject *parent = nullptr)
        : QObject(parent) {}
    QVariantList peers() const { return {}; }
    QVariantList searchResults() const { return {}; }
    bool searchBusy() const { return false; }
    QString selectedDeviceId() const { return QString(); }
    int recentTotalCount() const { return 0; }
    bool recentExpanded() const { return false; }
    int recentVisibleLimit() const { return 10; }
    Q_INVOKABLE void searchPeers(const QString &) {}
    Q_INVOKABLE void refresh() {}
    Q_INVOKABLE void setDeviceFavorite(const QString &, bool) {}
};

// 桩：可达性控制器（添加设备弹窗创建期绑定 isProbing 与 Connections target）
class StubReachabilityController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isProbing READ isProbing CONSTANT)
public:
    explicit StubReachabilityController(QObject *parent = nullptr)
        : QObject(parent) {}
    bool isProbing() const { return false; }
};

// 桩：应用控制器（侧栏信息弹窗读应用名版本，列表与添加设备读上面两个桩）
class StubAppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *peerDiscoveryViewModel READ peerDiscoveryViewModel CONSTANT)
    Q_PROPERTY(QObject *reachabilityController READ reachabilityController CONSTANT)
    Q_PROPERTY(QString applicationName READ applicationName CONSTANT)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
public:
    explicit StubAppController(QObject *parent = nullptr)
        : QObject(parent),
          _peers(new StubPeerDiscoveryViewModel(this)),
          _reachability(new StubReachabilityController(this)) {}
    QObject *peerDiscoveryViewModel() const { return _peers; }
    QObject *reachabilityController() const { return _reachability; }
    QString applicationName() const { return QStringLiteral("GridYard"); }
    QString applicationVersion() const { return QStringLiteral("8.2.1"); }
private:
    StubPeerDiscoveryViewModel *_peers;
    StubReachabilityController *_reachability;
};

// 桩：配置管理器（侧栏与添加设备弹窗创建期读设备名/IP/端口/设备 ID）
class StubConfigManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
    Q_PROPERTY(QString localIp READ localIp CONSTANT)
    Q_PROPERTY(int tcpPort READ tcpPort CONSTANT)
    Q_PROPERTY(QString deviceId READ deviceId CONSTANT)
public:
    explicit StubConfigManager(QObject *parent = nullptr)
        : QObject(parent) {}
    QString deviceName() const { return QStringLiteral("测试机"); }
    QString localIp() const { return QStringLiteral("192.168.1.2"); }
    int tcpPort() const { return 35100; }
    QString deviceId() const { return QStringLiteral("self"); }
};

} // namespace

class KeyboardAccessTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void deviceCardTabFocusAndVisualFocus();
    void deviceCardReturnActivatesCardClicked();
    void deviceCardEnterActivatesCardClicked();
    void deviceCardSpaceActivatesCardClicked();
    void sidebarAvatarAndMenuReachableByTab();
    void sidebarAvatarReturnOpensInfoPopup();
    void sidebarMenuReturnOpensMenuPopup();
    void sidebarMenuSpaceOpensMenuPopup();

private:
    QObject *loadComponent(const QString &relativePath,
                           const QVariantMap &initialProperties = {});
    bool pumpTabsUntilFocused(QQuickItem *target, int maxPresses);

    QQmlEngine _engine;
    StubAppController _appController;
    StubConfigManager _configManager;
    QTemporaryDir _stubModuleDir;
    QList<QObject *> _loaded;
    QQuickWindow *_window = nullptr;
};

void KeyboardAccessTest::init()
{
    _engine.rootContext()->setContextProperty("AppController", &_appController);
    _engine.rootContext()->setContextProperty("ConfigManager", &_configManager);

    // 桩 QML 模块：cqnu.gridyard.client 仅含哑类型，让 DeviceCard/Sidebar 的
    // import 可解析而不遮蔽上下文属性（AppController 等走桩上下文）
    const QString moduleDir = _stubModuleDir.filePath(
                QStringLiteral("cqnu/gridyard/client"));
    QDir().mkpath(moduleDir);
    QFile qmldir(moduleDir + QStringLiteral("/qmldir"));
    if (qmldir.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qmldir.write("module cqnu.gridyard.client\nDummy 1.0 Dummy.qml\n");
    }
    QFile dummy(moduleDir + QStringLiteral("/Dummy.qml"));
    if (dummy.open(QIODevice::WriteOnly | QIODevice::Text)) {
        dummy.write("import QtQuick\nItem { }\n");
    }
    _engine.addImportPath(_stubModuleDir.path());
}

void KeyboardAccessTest::cleanup()
{
    for (QObject *obj : _loaded) {
        if (obj) {
            obj->deleteLater();
        }
    }
    _loaded.clear();
    if (_window) {
        _window->deleteLater();
        _window = nullptr;
    }
    _engine.clearComponentCache();
}

// 从源码树加载 QML 组件并放进离屏窗口（供按键投递与焦点链驱动），
// 失败（状态非 Ready 或创建为空）时返回空指针，由用例自行断言
QObject *KeyboardAccessTest::loadComponent(const QString &relativePath,
                                           const QVariantMap &initialProperties)
{
    const QString path = QDir(GRIDYARD_SOURCE_DIR).filePath(relativePath);
    QQmlComponent component(&_engine, QUrl::fromLocalFile(path));
    if (component.status() != QQmlComponent::Ready) {
        qWarning() << relativePath << "加载失败：" << component.errorString();
        return nullptr;
    }
    QObject *obj = initialProperties.isEmpty()
                       ? component.create()
                       : component.createWithInitialProperties(initialProperties);
    if (!obj) {
        return nullptr;
    }
    _loaded.append(obj);
    _window = new QQuickWindow();
    QQuickItem *rootItem = qobject_cast<QQuickItem *>(obj);
    if (rootItem) {
        rootItem->setParentItem(_window->contentItem());
    }
    _window->show();
    if (!QTest::qWaitForWindowExposed(_window)) {
        qWarning() << "离屏窗口未能在时限内暴露";
        return nullptr;
    }
    return obj;
}

// 反复按 Tab 直到 target 获得活动焦点（焦点链顺序断言无关化），按满 maxPresses
// 仍不中返回 false
bool KeyboardAccessTest::pumpTabsUntilFocused(QQuickItem *target, int maxPresses)
{
    for (int i = 0; i < maxPresses; ++i) {
        QTest::keyClick(_window, Qt::Key_Tab);
        if (target->hasActiveFocus()) {
            return true;
        }
    }
    return false;
}

// 设备卡：Tab 可聚焦且聚焦态 visualFocus 成立（焦点环视觉的驱动条件），
// 修前 ItemDelegate 默认 activeFocusOnTab=false（NoFocus），Tab 永远到不了
void KeyboardAccessTest::deviceCardTabFocusAndVisualFocus()
{
    const QVariantMap props{
        { "deviceId", QStringLiteral("dev-1") },
        { "deviceName", QStringLiteral("设备A") },
        { "ipAddress", QStringLiteral("192.168.1.5") },
        { "isOnline", true },
        { "isSelected", false },
        { "lastSeenAt", QString() },
        { "favorite", false },
        { "alias", QString() },
    };
    auto *card = qobject_cast<QQuickItem *>(
                loadComponent(QStringLiteral("client/ui/DeviceCard.qml"), props));
    QVERIFY(card);
    QCOMPARE(card->property("activeFocusOnTab").toBool(), true);

    QVERIFY(pumpTabsUntilFocused(card, 10));
    QCOMPARE(card->hasActiveFocus(), true);
    // 键盘落点必须可见：焦点环以 visualFocus 为驱动条件
    QCOMPARE(card->property("visualFocus").toBool(), true);
}

// 设备卡 Return：与左键点击同路 cardClicked，载荷为完整设备信息
void KeyboardAccessTest::deviceCardReturnActivatesCardClicked()
{
    const QVariantMap props{
        { "deviceId", QStringLiteral("dev-1") },
        { "deviceName", QStringLiteral("设备A") },
        { "ipAddress", QStringLiteral("192.168.1.5") },
        { "isOnline", true },
        { "isSelected", false },
        { "lastSeenAt", QString() },
        { "favorite", false },
        { "alias", QString() },
    };
    auto *card = qobject_cast<QQuickItem *>(
                loadComponent(QStringLiteral("client/ui/DeviceCard.qml"), props));
    QVERIFY(card);
    QVERIFY(pumpTabsUntilFocused(card, 10));

    QSignalSpy spy(card, SIGNAL(cardClicked(QString, QString, QString, bool)));
    QVERIFY(spy.isValid());
    QTest::keyClick(_window, Qt::Key_Return);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("dev-1"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("设备A"));
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("192.168.1.5"));
    QCOMPARE(spy.at(0).at(3).toBool(), true);
}

// 设备卡 Enter（小键盘）：与 Return 同路激活
void KeyboardAccessTest::deviceCardEnterActivatesCardClicked()
{
    const QVariantMap props{
        { "deviceId", QStringLiteral("dev-2") },
        { "deviceName", QStringLiteral("设备B") },
        { "ipAddress", QStringLiteral("192.168.1.6") },
        { "isOnline", false },
        { "isSelected", false },
        { "lastSeenAt", QString() },
        { "favorite", false },
        { "alias", QString() },
    };
    auto *card = qobject_cast<QQuickItem *>(
                loadComponent(QStringLiteral("client/ui/DeviceCard.qml"), props));
    QVERIFY(card);
    QVERIFY(pumpTabsUntilFocused(card, 10));

    QSignalSpy spy(card, SIGNAL(cardClicked(QString, QString, QString, bool)));
    QVERIFY(spy.isValid());
    QTest::keyClick(_window, Qt::Key_Enter);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("dev-2"));
    QCOMPARE(spy.at(0).at(3).toBool(), false);
}

// 设备卡 Space：AbstractButton 原生键盘激活路径，修前因不可聚焦同样不可达
void KeyboardAccessTest::deviceCardSpaceActivatesCardClicked()
{
    const QVariantMap props{
        { "deviceId", QStringLiteral("dev-3") },
        { "deviceName", QStringLiteral("设备C") },
        { "ipAddress", QStringLiteral("192.168.1.7") },
        { "isOnline", true },
        { "isSelected", false },
        { "lastSeenAt", QString() },
        { "favorite", false },
        { "alias", QString() },
    };
    auto *card = qobject_cast<QQuickItem *>(
                loadComponent(QStringLiteral("client/ui/DeviceCard.qml"), props));
    QVERIFY(card);
    QVERIFY(pumpTabsUntilFocused(card, 10));

    QSignalSpy spy(card, SIGNAL(cardClicked(QString, QString, QString, bool)));
    QVERIFY(spy.isValid());
    QTest::keyClick(_window, Qt::Key_Space);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("dev-3"));
}

// 侧栏：本机头像与菜单按钮 Tab 可被遍历到（顺序无关化），且 activeFocusOnTab 成立
void KeyboardAccessTest::sidebarAvatarAndMenuReachableByTab()
{
    QObject *sidebar = loadComponent(QStringLiteral("client/ui/Sidebar.qml"));
    QVERIFY(sidebar);
    auto *avatar = sidebar->findChild<QQuickItem *>("avatarButton");
    auto *menu = sidebar->findChild<QQuickItem *>("menuButton");
    QVERIFY(avatar);
    QVERIFY(menu);
    QCOMPARE(avatar->property("activeFocusOnTab").toBool(), true);
    QCOMPARE(menu->property("activeFocusOnTab").toBool(), true);

    QVERIFY(pumpTabsUntilFocused(avatar, 15));
    QCOMPARE(avatar->hasActiveFocus(), true);
    QVERIFY(pumpTabsUntilFocused(menu, 15));
    QCOMPARE(menu->hasActiveFocus(), true);
}

// 侧栏本机头像：Return 打开本机信息弹窗
void KeyboardAccessTest::sidebarAvatarReturnOpensInfoPopup()
{
    QObject *sidebar = loadComponent(QStringLiteral("client/ui/Sidebar.qml"));
    QVERIFY(sidebar);
    auto *avatar = sidebar->findChild<QQuickItem *>("avatarButton");
    QVERIFY(avatar);
    QObject *infoPopup = sidebar->findChild<QObject *>("deviceInfoPopup");
    QVERIFY(infoPopup);
    QCOMPARE(infoPopup->property("opened").toBool(), false);

    avatar->forceActiveFocus();
    QCOMPARE(avatar->hasActiveFocus(), true);
    QTest::keyClick(_window, Qt::Key_Return);
    QVERIFY(QTest::qWaitFor([&] {
        return infoPopup->property("opened").toBool();
    }, 2000));
    QCOMPARE(infoPopup->property("opened").toBool(), true);
}

// 侧栏菜单按钮：Return 打开菜单弹窗
void KeyboardAccessTest::sidebarMenuReturnOpensMenuPopup()
{
    QObject *sidebar = loadComponent(QStringLiteral("client/ui/Sidebar.qml"));
    QVERIFY(sidebar);
    auto *menu = sidebar->findChild<QQuickItem *>("menuButton");
    QVERIFY(menu);
    QObject *menuPopup = sidebar->findChild<QObject *>("menuPopup");
    QVERIFY(menuPopup);
    QCOMPARE(menuPopup->property("opened").toBool(), false);

    menu->forceActiveFocus();
    QCOMPARE(menu->hasActiveFocus(), true);
    QTest::keyClick(_window, Qt::Key_Return);
    QVERIFY(QTest::qWaitFor([&] {
        return menuPopup->property("opened").toBool();
    }, 2000));
    QCOMPARE(menuPopup->property("opened").toBool(), true);
}

// 侧栏菜单按钮：Space 同路激活（与 AbstractButton 原生键盘习惯一致）
void KeyboardAccessTest::sidebarMenuSpaceOpensMenuPopup()
{
    QObject *sidebar = loadComponent(QStringLiteral("client/ui/Sidebar.qml"));
    QVERIFY(sidebar);
    auto *menu = sidebar->findChild<QQuickItem *>("menuButton");
    QVERIFY(menu);
    QObject *menuPopup = sidebar->findChild<QObject *>("menuPopup");
    QVERIFY(menuPopup);

    menu->forceActiveFocus();
    QCOMPARE(menu->hasActiveFocus(), true);
    QTest::keyClick(_window, Qt::Key_Space);
    QVERIFY(QTest::qWaitFor([&] {
        return menuPopup->property("opened").toBool();
    }, 2000));
    QCOMPARE(menuPopup->property("opened").toBool(), true);
}

int main(int argc, char *argv[])
{
    // 离屏平台：不依赖桌面会话，焦点链与按键投递在 QQuickWindow 内完成
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);
    KeyboardAccessTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_keyboard_access.moc"
