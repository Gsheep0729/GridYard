/**
 * @file    test_screen_reader_semantics.cpp
 * @version 8.2.1
 * @date 2026-11-01
 * @author  GridYard Team
 * @brief   读屏语义回归测试（O-24）
 *
 * 以桩上下文属性与桩 QML 模块（cqnu.gridyard.client 哑导出 + FileTypeIcon 桩）
 * 从源码树加载四个高频弹窗（接收确认/关闭确认/删除确认/添加设备）与传输任务卡，
 * 经公开 API QAccessible::queryAccessibleInterface 检查读屏语义：
 * - 每个弹窗内恰有一个 Dialog 角色承载，名字对齐弹窗标题（Qt 6.11.1 实测
 *   QQC2 Dialog 本体不是 Item，Accessible 附挂不上，语义挂在内容根 Item 上）；
 * - 弹窗内 Button/PageTab/CheckBox 角色（QQC2 内建 role）必须有非空且与
 *   可见文字一致的名字（实测内建只给 role 不给 name）；
 * - EditableText 角色（输入框）必须有非空名字（SpinBox 内部输入框除外，
 *   由 SpinBox 的值接口承载）；
 * - 关键文本（问句、警示、原因、错误行）以 StaticText 名字可达；
 * - 任务卡状态徽标为 Indicator 角色且名字随状态绑定更新（内部文本行置
 *   ignored 防重复播报）。
 */

#include <QtTest/QtTest>
#include <QAccessible>
#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>

namespace {

// 桩：可达性控制器（添加设备弹窗创建期绑定 isProbing，打开时读邀请码）
class StubReachabilityController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isProbing READ isProbing CONSTANT)
public:
    explicit StubReachabilityController(QObject *parent = nullptr)
        : QObject(parent) {}
    bool isProbing() const { return false; }
    Q_INVOKABLE QString generateInvite() const
    {
        return QStringLiteral("gridyard://invite?stub");
    }
};

// 桩：传输控制器（接收确认弹窗 Connections target，仅要求对象存在）
class StubTransferController : public QObject {
    Q_OBJECT
public:
    explicit StubTransferController(QObject *parent = nullptr)
        : QObject(parent) {}
};

// 桩：应用控制器（弹窗创建期读 reachabilityController/transferController）
class StubAppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *reachabilityController READ reachabilityController CONSTANT)
    Q_PROPERTY(QObject *transferController READ transferController CONSTANT)
public:
    explicit StubAppController(QObject *parent = nullptr)
        : QObject(parent),
          _reachability(new StubReachabilityController(this)),
          _transfer(new StubTransferController(this)) {}
    QObject *reachabilityController() const { return _reachability; }
    QObject *transferController() const { return _transfer; }
private:
    StubReachabilityController *_reachability;
    StubTransferController *_transfer;
};

// 桩：配置管理器（添加设备弹窗创建期读设备 ID/端口）
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

class ScreenReaderSemanticsTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void acceptDialogAccessibleSemantics();
    void closeConfirmDialogAccessibleSemantics();
    void deleteDeviceDialogAccessibleSemantics();
    void addDeviceDialogAccessibleSemantics();
    void transferTaskCardStatusBadgeAccessible();

private:
    QObject *loadComponent(const QString &relativePath,
                           const QVariantMap &initialProperties = {});
    bool openDialogInWindow(QObject *dialog);
    QString checkDialogSemantics(QObject *dialog, const QString &expectedDialogName);
    void collectItems(QObject *obj, QSet<QObject *> &seen, QList<QQuickItem *> &out) const;
    static bool hasAncestorRole(QQuickItem *item, QAccessible::Role role);

    QQmlEngine _engine;
    StubAppController _appController;
    StubConfigManager _configManager;
    QTemporaryDir _stubModuleDir;
    QList<QObject *> _loaded;
    QQuickWindow *_window = nullptr;
};

void ScreenReaderSemanticsTest::init()
{
    _engine.rootContext()->setContextProperty("AppController", &_appController);
    _engine.rootContext()->setContextProperty("ConfigManager", &_configManager);

    // 桩 QML 模块：cqnu.gridyard.client 含哑类型与 FileTypeIcon 桩，让四个
    // 弹窗与任务卡的 import 可解析而不遮蔽上下文属性（AppController 走桩上下文）
    const QString moduleDir = _stubModuleDir.filePath(
                QStringLiteral("cqnu/gridyard/client"));
    QDir().mkpath(moduleDir);
    QFile qmldir(moduleDir + QStringLiteral("/qmldir"));
    if (qmldir.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qmldir.write("module cqnu.gridyard.client\n"
                     "Dummy 1.0 Dummy.qml\n"
                     "FileTypeIcon 1.0 FileTypeIcon.qml\n");
    }
    QFile dummy(moduleDir + QStringLiteral("/Dummy.qml"));
    if (dummy.open(QIODevice::WriteOnly | QIODevice::Text)) {
        dummy.write("import QtQuick\nItem { }\n");
    }
    QFile fileTypeIcon(moduleDir + QStringLiteral("/FileTypeIcon.qml"));
    if (fileTypeIcon.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fileTypeIcon.write("import QtQuick\n"
                           "Item { property string fileName: \"\";"
                           " property bool isDirectory: false }\n");
    }
    _engine.addImportPath(_stubModuleDir.path());
}

void ScreenReaderSemanticsTest::cleanup()
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

// 从源码树加载 QML 组件（任务卡为普通 Item；弹窗的读屏校验另经
// openDialogInWindow 挂窗口打开），失败（状态非 Ready 或创建为空）时
// 返回空指针，由用例自行断言
QObject *ScreenReaderSemanticsTest::loadComponent(const QString &relativePath,
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
    if (obj) {
        _loaded.append(obj);
    }
    return obj;
}

// 递归收集 obj 子树内全部 QQuickItem（弹窗的 contentItem/footer/header 都以
// 弹窗内部件为 QObject 父对象，children 递归可达）
void ScreenReaderSemanticsTest::collectItems(QObject *obj, QSet<QObject *> &seen,
                                             QList<QQuickItem *> &out) const
{
    for (QObject *child : obj->children()) {
        if (seen.contains(child)) {
            continue;
        }
        seen.insert(child);
        if (auto *item = qobject_cast<QQuickItem *>(child)) {
            out.append(item);
        }
        collectItems(child, seen, out);
    }
}

// 把弹窗挂到离屏窗口并打开：读屏语义面向打开后的弹窗内容（关闭态内容全部
// 隐形，可访问面查询虽在但不适合做可见性过滤的通用校验）
bool ScreenReaderSemanticsTest::openDialogInWindow(QObject *dialog)
{
    if (!_window) {
        _window = new QQuickWindow();
        _window->show();
        if (!QTest::qWaitForWindowExposed(_window)) {
            qWarning() << "离屏窗口未能在时限内暴露";
            return false;
        }
    }
    dialog->setProperty("parent", QVariant::fromValue(_window->contentItem()));
    const bool opened = QMetaObject::invokeMethod(dialog, "open");
    if (!opened) {
        return false;
    }
    return QTest::qWaitFor([&] {
        return dialog->property("opened").toBool();
    }, 2000);
}

// item 的任一祖先具 role 角色则真（SpinBox 内部输入框豁免用）
bool ScreenReaderSemanticsTest::hasAncestorRole(QQuickItem *item, QAccessible::Role role)
{
    for (QQuickItem *p = item->parentItem(); p; p = p->parentItem()) {
        if (auto *iface = QAccessible::queryAccessibleInterface(p)) {
            if (iface->role() == role) {
                return true;
            }
        }
    }
    return false;
}

// 可见文字是否含"词字符"（字母/数字/中日韩）：纯符号文字（如关闭钮的"✕"）
// 的读屏名应当是动作词而非符号本身，只要求非空不要求与文字一致
static bool textHasWordChar(const QString &text)
{
    for (const QChar &ch : text) {
        if (ch.isLetterOrNumber() || ch.script() == QChar::Script_Han) {
            return true;
        }
    }
    return false;
}

// 弹窗级通用校验：返回失败原因（空串=通过）。
// ① 恰有一个 Dialog 角色承载且名字为弹窗标题（QQC2 自带的 QQuickPopupItem
//    也有内建 Dialog 角色但无名，不计入——语义由我们的内容根承载）；
// ② Button/PageTab/CheckBox 角色（QQC2 内建）名字与可见文字 text 一致且非空；
// ③ EditableText 角色名字非空（SpinBox 内部输入框除外）；
// ④ 以上均只对声明 visible 的项生效（读屏跳过不可见项）。
QString ScreenReaderSemanticsTest::checkDialogSemantics(QObject *dialog,
                                                        const QString &expectedDialogName)
{
    QSet<QObject *> seen{dialog};
    QList<QQuickItem *> items;
    collectItems(dialog, seen, items);
    if (items.isEmpty()) {
        return QStringLiteral("弹窗子树未收集到任何 Item（contentItem 未挂到弹窗对象树）");
    }

    QString errors;
    int dialogRoleCount = 0;
    for (QQuickItem *item : items) {
        auto *iface = QAccessible::queryAccessibleInterface(item);
        if (!iface) {
            continue;
        }
        const QAccessible::Role role = iface->role();
        const bool declaredVisible = item->property("visible").isValid()
                                     && item->property("visible").toBool();
        if (!declaredVisible) {
            continue;
        }
        if (role == QAccessible::Dialog) {
            if (QByteArray(item->metaObject()->className())
                    .startsWith("QQuickPopupItem")) {
                continue;  // QQC2 内建弹窗壳：自带无名 Dialog 角色，不计入
            }
            ++dialogRoleCount;
            if (iface->text(QAccessible::Name) != expectedDialogName) {
                errors += QStringLiteral("Dialog 角色名字「%1」≠「%2」；")
                              .arg(iface->text(QAccessible::Name), expectedDialogName);
            }
        } else if (role == QAccessible::Button || role == QAccessible::PageTab
                   || role == QAccessible::CheckBox) {
            const QString text = item->property("text").toString();
            const QString name = iface->text(QAccessible::Name);
            const bool mismatched = name.isEmpty()
                                    || (!text.isEmpty() && textHasWordChar(text)
                                        && name != text);
            if (mismatched) {
                errors += QStringLiteral("%1（role=%2）读屏名「%3」≠ 文字「%4」；")
                              .arg(item->objectName().isEmpty()
                                       ? QString::fromUtf8(item->metaObject()->className())
                                       : item->objectName())
                              .arg(int(role))
                              .arg(name, text);
            }
        } else if (role == QAccessible::EditableText) {
            if (hasAncestorRole(item, QAccessible::SpinBox)) {
                continue;  // SpinBox 内部输入框由 SpinBox 值接口承载
            }
            if (iface->text(QAccessible::Name).isEmpty()) {
                errors += QStringLiteral("%1 输入框读屏名为空；")
                              .arg(item->objectName().isEmpty()
                                       ? QString::fromUtf8(item->metaObject()->className())
                                       : item->objectName());
            }
        }
    }
    if (dialogRoleCount != 1) {
        errors += QStringLiteral("Dialog 角色承载数 %1 ≠ 1；").arg(dialogRoleCount);
    }
    return errors;
}

// 接收确认弹窗：Dialog 角色对齐标题、描述含发送方，文件信息卡成组可达，
// 问句可达，全部按钮有读屏名
void ScreenReaderSemanticsTest::acceptDialogAccessibleSemantics()
{
    const QVariantMap props{
        { "sessionId", QStringLiteral("sess-1") },
        { "senderName", QStringLiteral("张三") },
        { "fileName", QStringLiteral("季度报告.pdf") },
        { "fileSize", 2048.0 },
        { "totalFiles", 1 },
        { "totalBytes", 2048.0 },
        { "isDirectory", false },
        { "fileList", QVariantList{} },
    };
    QObject *dialog = loadComponent(QStringLiteral("client/ui/AcceptDialog.qml"), props);
    QVERIFY(dialog);
    QVERIFY(openDialogInWindow(dialog));

    const QString semanticErrors = checkDialogSemantics(dialog, QStringLiteral("接收文件"));
    QVERIFY2(semanticErrors.isEmpty(), qPrintable(semanticErrors));

    // 描述含发送方（读屏开场能听到"来自谁"）
    QQuickItem *content = dialog->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    auto *contentIface = QAccessible::queryAccessibleInterface(content);
    QVERIFY(contentIface);
    QVERIFY2(contentIface->text(QAccessible::Description)
                 .contains(QStringLiteral("张三")),
             qPrintable(contentIface->text(QAccessible::Description)));

    // 文件信息卡：成组角色且名字带上文件名（单文件场景的大小随文案词表走）
    QQuickItem *infoCard = dialog->findChild<QQuickItem *>("acceptFileInfoCard");
    QVERIFY(infoCard);
    auto *cardIface = QAccessible::queryAccessibleInterface(infoCard);
    QVERIFY(cardIface);
    QCOMPARE(cardIface->role(), QAccessible::Grouping);
    QVERIFY2(cardIface->text(QAccessible::Name)
                 .contains(QStringLiteral("季度报告.pdf")),
             qPrintable(cardIface->text(QAccessible::Name)));

    // 问句行可达
    QQuickItem *question = dialog->findChild<QQuickItem *>("acceptQuestionText");
    QVERIFY(question);
    auto *questionIface = QAccessible::queryAccessibleInterface(question);
    QVERIFY(questionIface);
    QCOMPARE(questionIface->role(), QAccessible::StaticText);
    QVERIFY(questionIface->text(QAccessible::Name).contains(QStringLiteral("是否接受")));
}

// 关闭确认弹窗：Dialog 角色对齐标题，问句与传输中警示随注入状态更新，
// 复选框与三按钮有读屏名
void ScreenReaderSemanticsTest::closeConfirmDialogAccessibleSemantics()
{
    QObject *dialog = loadComponent(QStringLiteral("client/ui/CloseConfirmDialog.qml"));
    QVERIFY(dialog);

    // 常规询问（托盘可用 + 无进行中传输）：问句读"隐藏到后台还是退出"
    dialog->setProperty("trayAvailable", true);
    QVERIFY(openDialogInWindow(dialog));
    const QString semanticErrors = checkDialogSemantics(dialog, QStringLiteral("关闭 GridYard"));
    QVERIFY2(semanticErrors.isEmpty(), qPrintable(semanticErrors));

    QQuickItem *question = dialog->findChild<QQuickItem *>("closeConfirmQuestionText");
    QVERIFY(question);
    auto *questionIface = QAccessible::queryAccessibleInterface(question);
    QVERIFY(questionIface);
    QCOMPARE(questionIface->role(), QAccessible::StaticText);
    QVERIFY2(questionIface->text(QAccessible::Name)
                 .contains(QStringLiteral("还是直接退出程序")),
             qPrintable(questionIface->text(QAccessible::Name)));

    // 有进行中传输时警示行随计数更新
    dialog->setProperty("activeTransferCount", 2);
    QQuickItem *warning = dialog->findChild<QQuickItem *>("closeConfirmActiveTransfersText");
    QVERIFY(warning);
    auto *warningIface = QAccessible::queryAccessibleInterface(warning);
    QVERIFY(warningIface);
    QVERIFY2(warningIface->text(QAccessible::Name)
                 .contains(QStringLiteral("2 个传输任务正在进行")),
             qPrintable(warningIface->text(QAccessible::Name)));
}

// 删除设备确认弹窗：Dialog 角色对齐标题，删除范围与重现说明两行可达，
// 取消/删除按钮有读屏名
void ScreenReaderSemanticsTest::deleteDeviceDialogAccessibleSemantics()
{
    QObject *dialog = loadComponent(QStringLiteral("client/ui/DeleteDeviceDialog.qml"));
    QVERIFY(dialog);
    dialog->setProperty("deviceName", QStringLiteral("设备甲"));
    QVERIFY(openDialogInWindow(dialog));

    const QString semanticErrors = checkDialogSemantics(dialog, QStringLiteral("删除该设备"));
    QVERIFY2(semanticErrors.isEmpty(), qPrintable(semanticErrors));

    QQuickItem *scope = dialog->findChild<QQuickItem *>("deleteDeviceScopeText");
    QVERIFY(scope);
    auto *scopeIface = QAccessible::queryAccessibleInterface(scope);
    QVERIFY(scopeIface);
    QCOMPARE(scopeIface->role(), QAccessible::StaticText);
    QVERIFY2(scopeIface->text(QAccessible::Name).contains(QStringLiteral("设备甲")),
             qPrintable(scopeIface->text(QAccessible::Name)));

    QQuickItem *rescan = dialog->findChild<QQuickItem *>("deleteDeviceRescanText");
    QVERIFY(rescan);
    auto *rescanIface = QAccessible::queryAccessibleInterface(rescan);
    QVERIFY(rescanIface);
    QVERIFY2(rescanIface->text(QAccessible::Name)
                 .contains(QStringLiteral("全新设备身份")),
             qPrintable(rescanIface->text(QAccessible::Name)));
}

// 添加设备弹窗：Dialog 角色对齐标题，三种方式的说明与行内错误可达，
// 页签/按钮/输入区全部有读屏名
void ScreenReaderSemanticsTest::addDeviceDialogAccessibleSemantics()
{
    QObject *dialog = loadComponent(QStringLiteral("client/ui/AddDeviceDialog.qml"));
    QVERIFY(dialog);
    QVERIFY(openDialogInWindow(dialog));

    const QString semanticErrors = checkDialogSemantics(dialog, QStringLiteral("添加设备"));
    QVERIFY2(semanticErrors.isEmpty(), qPrintable(semanticErrors));

    // 三种方式的说明文本可达
    const QStringList hintNames{
        QStringLiteral("addInviteHintText"),
        QStringLiteral("addImportHintText"),
        QStringLiteral("addManualHintText"),
    };
    for (const QString &hintName : hintNames) {
        QQuickItem *hint = dialog->findChild<QQuickItem *>(hintName);
        QVERIFY2(hint, qPrintable(QStringLiteral("缺少 objectName=%1").arg(hintName)));
        auto *hintIface = QAccessible::queryAccessibleInterface(hint);
        QVERIFY(hintIface);
        QCOMPARE(hintIface->role(), QAccessible::StaticText);
        QVERIFY2(!hintIface->text(QAccessible::Name).isEmpty(),
                 qPrintable(QStringLiteral("%1 读屏名为空").arg(hintName)));
    }

    // 行内错误提示随错误状态更新（读屏能听到失败原因）
    dialog->setProperty("_importError", QStringLiteral("邀请码无效，请检查后重试"));
    QQuickItem *importError = dialog->findChild<QQuickItem *>("addImportErrorText");
    QVERIFY(importError);
    auto *importErrorIface = QAccessible::queryAccessibleInterface(importError);
    QVERIFY(importErrorIface);
    QVERIFY2(importErrorIface->text(QAccessible::Name)
                 .contains(QStringLiteral("邀请码无效")),
             qPrintable(importErrorIface->text(QAccessible::Name)));

    dialog->setProperty("_manualError", QStringLiteral("无法连接该地址"));
    QQuickItem *manualError = dialog->findChild<QQuickItem *>("addManualErrorText");
    QVERIFY(manualError);
    auto *manualErrorIface = QAccessible::queryAccessibleInterface(manualError);
    QVERIFY(manualErrorIface);
    QVERIFY2(manualErrorIface->text(QAccessible::Name)
                 .contains(QStringLiteral("无法连接该地址")),
             qPrintable(manualErrorIface->text(QAccessible::Name)));
}

// 任务卡状态徽标：Indicator 角色，名字随状态词表更新；内部同文文本行
// 置 ignored 防重复播报
void ScreenReaderSemanticsTest::transferTaskCardStatusBadgeAccessible()
{
    const QVariantMap props{
        { "sessionId", QStringLiteral("sess-1") },
        { "taskType", QStringLiteral("receive") },
        { "taskName", QStringLiteral("季度报告.pdf") },
        { "status", QStringLiteral("transferring") },
        { "progress", 42 },
        { "bytesTransferred", 1024.0 },
        { "totalBytes", 2048.0 },
        { "isDirectory", false },
        { "fileList", QVariantList{} },
        { "canDeleteLocalFile", false },
    };
    QObject *card = loadComponent(QStringLiteral("client/ui/TransferTaskCard.qml"), props);
    QVERIFY(card);

    QQuickItem *badge = card->findChild<QQuickItem *>("statusBadge");
    QVERIFY2(badge, "任务卡缺少 objectName=statusBadge 的状态徽标");
    auto *badgeIface = QAccessible::queryAccessibleInterface(badge);
    QVERIFY2(badgeIface, "状态徽标无可访问接口（未附挂 Accessible）");
    QCOMPARE(badgeIface->role(), QAccessible::Indicator);

    // 名字随状态绑定更新（词表与 FormatUtils.transferStatusText 一致）
    QCOMPARE(badgeIface->text(QAccessible::Name), QStringLiteral("状态：传输中"));
    QVERIFY(card->setProperty("status", QStringLiteral("completed")));
    QCOMPARE(badgeIface->text(QAccessible::Name), QStringLiteral("状态：已完成"));
    QVERIFY(card->setProperty("status", QStringLiteral("failed")));
    QCOMPARE(badgeIface->text(QAccessible::Name), QStringLiteral("状态：失败"));
    QVERIFY(card->setProperty("status", QStringLiteral("cancelled")));
    QCOMPARE(badgeIface->text(QAccessible::Name), QStringLiteral("状态：已取消"));
}

int main(int argc, char *argv[])
{
    // 离屏平台：读屏语义经 QAccessible 公开 API 查询，不依赖桌面会话
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);
    ScreenReaderSemanticsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_screen_reader_semantics.moc"
