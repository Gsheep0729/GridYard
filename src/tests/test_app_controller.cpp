/**
* @file    test_app_controller.cpp
* @version 8.2.1
* @date 2026-11-01
* @author  GY
* @brief   组合根启动路径测试
*
* 测试用例：P2P 监听端口被占时启动失败标记随属性发布，端口空闲时不标记；
* 协调服务器错误令牌等交互错误随属性发布、注册确认到达后清空；
* 托盘退出决策与关窗退出分支同判（活动传输拦截不随记忆豁免）；
* 托盘打开接收目录入口打开当前配置的接收路径，目录缺失时静默不外呼；
* 唤回置顶平台判定：仅 Wayland 不支持临时置顶须降级，其余平台保持置顶。
*/

#include <QtTest/QtTest>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include "app_controller.h"
#include "application_paths.h"
#include "config_manager.h"
#include "file_sender_worker.h"
#include "ip_address.h"
#include "local_data_broker.h"
#include "p2p_server.h"
#include "posix_tcp_server.h"
#include "rendezvous_client.h"
#include "transfer_session_manager.h"

#include "test_utils/wait.h"

// file scheme 打开请求拦截器：QDesktopServices::setUrlHandler 把"打开目录"的
// 外呼改道到这里，既能断言打开的目标路径，也避免测试真的拉起文件管理器
class FileUrlSpy : public QObject {
    Q_OBJECT

public slots:
    void handle(const QUrl &url)
    {
        _called = true;
        _lastUrl = url;
    }

public:
    bool called() const { return _called; }
    QUrl lastUrl() const { return _lastUrl; }
    void reset() { _called = false; _lastUrl = QUrl(); }

private:
    bool _called = false;
    QUrl _lastUrl;
};

class TestAppController : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testListenFailureMarkedWhenPortOccupied();
    void testListenSuccessMarkedWhenPortFree();
    void testRendezvousErrorSurfacesAndClearsOnAck();
    void testTrayQuitDecisionMatchesWindowCloseGuard();
    void testTrayOpenReceiveFolderOpensConfiguredPath();
    void testStaysOnTopSupportedPerPlatform();

private:
    QTemporaryDir *_tempDir = nullptr;

    // 组合根销毁前先关停存储线程：构造期排队的数据库任务回调锚定着
    // 尚未销毁的界面对象，跳过停机契约直接析构会与之竞争
    static void drainStorageBeforeDestruction(AppController &controller)
    {
        controller._dataBroker->closeStorage();
    }
};

void TestAppController::init()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    // 配置与数据目录覆盖到临时路径，UDP/TCP 端口取默认值（ctest 串行无争用）
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/config.ini").toUtf8());
    qputenv("GRIDYARD_NAME", "AppCtrlTest");
    qunsetenv("GRIDYARD_PORT");
    qunsetenv("GRIDYARD_INSTANCE");
    ApplicationPaths::coverForTest(_tempDir->path());

    // 接收路径预置到临时目录：组合根启动时会清理接收目录的陈旧 .part 残留，
    // 不预置就会扫到真实用户主目录下的默认接收目录
    QFile configFile(_tempDir->path() + "/config.ini");
    QVERIFY(configFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    configFile.write(QString("[device]\nreceivePath=%1/recv\n")
                         .arg(_tempDir->path()).toUtf8());
    configFile.close();
}

void TestAppController::cleanup()
{
    ApplicationPaths::clearTestCover();
    delete _tempDir;
    _tempDir = nullptr;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

// 监听端口被占时组合根应把启动失败标记成可呈现的属性，而不是静默运行
void TestAppController::testListenFailureMarkedWhenPortOccupied()
{
    // 先占住一个随机端口，组合根按该端口启动 P2P 监听必然失败
    gy::net::PosixTcpServer occupier;
    QVERIFY(occupier.listen(gy::net::IpAddress::localHost(), 0));
    qputenv("GRIDYARD_PORT", QByteArray::number(occupier.serverPort()));

    AppController controller;
    QVERIFY2(controller.networkListenFailed(), "端口被占时应标记网络监听失败");
    drainStorageBeforeDestruction(controller);
}

// 端口空闲时不得误报监听失败
void TestAppController::testListenSuccessMarkedWhenPortFree()
{
    AppController controller;
    QVERIFY2(!controller.networkListenFailed(), "端口空闲时不应标记网络监听失败");
    drainStorageBeforeDestruction(controller);
}

// 协调服务器错误令牌拒绝应随组合根属性发布，注册确认到达后清空
void TestAppController::testRendezvousErrorSurfacesAndClearsOnAck()
{
    AppController controller;
    QVERIFY2(controller.rendezvousError().isEmpty(), "初始不应有协调错误");

    emit controller._rendezvousClient->errorOccurred(QStringLiteral("无效的访问口令"));
    QCOMPARE(controller.rendezvousError(), QStringLiteral("无效的访问口令"));

    emit controller._rendezvousClient->registerAckReceived(30);
    QVERIFY2(controller.rendezvousError().isEmpty(), "注册确认到达后应清空错误");
    drainStorageBeforeDestruction(controller);
}

// 托盘退出决策与关窗退出分支同判（O-01 回归锚）：挂着的传输会话在托盘退出时
// 必须与关窗路径一样拦截为警示确认（A6 防护不随记忆豁免），无活动传输时直接
// 退出；记住隐藏/每次询问只约束关窗动作，不得改变托盘退出的显式退出意图
void TestAppController::testTrayQuitDecisionMatchesWindowCloseGuard()
{
    AppController controller;
    QVERIFY2(!controller.networkListenFailed(), "端口空闲时组合根应正常监听");

    // 真实等待确认的接收会话把活动计数抬到 1（waiting_confirm 计入活动会话）
    controller._config->setReceivePath(_tempDir->path() + "/tray-quit-recv");
    const QString sourcePath = _tempDir->path() + "/tray-quit-source.txt";
    QFile source{sourcePath};
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(QByteArray("tray-quit-payload")), qint64(17));
    source.close();

    auto *sender = new FileSenderWorker;  // 堆对象：随线程 finished 链在归属线程 deleteLater
    QThread senderThread;
    sender->moveToThread(&senderThread);
    QObject::connect(&senderThread, &QThread::finished, sender, &QObject::deleteLater);
    QObject::connect(&senderThread, &QThread::started, sender,
                     [sender, &controller, &sourcePath]() {
        sender->startTransfer(QStringLiteral("127.0.0.1"),
                              controller._p2pServer->serverPort(), sourcePath,
                              QStringLiteral("tray-sender"), QStringLiteral("TraySender"));
    });
    QSignalSpy requestSpy(controller._transfer, &TransferSessionManager::receiveRequestReceived);
    senderThread.start();

    QVERIFY2(gy::test::waitSignal(requestSpy, 5000), "接收请求应在 5 秒内到达");
    const QString sessionId = requestSpy.first().at(0).toString();
    QVERIFY2(gy::test::waitFor([&controller]() {
        return controller._transfer->activeSessionCount() == 1;
    }, 5000), "等待确认的接收会话应计入活动会话");

    // 活动传输挂起：即使记住"完全退出"，托盘退出也必须拦截为警示确认，
    // 且与关窗决策在 Exit 记忆 + 活动传输下的结果同判
    controller._config->setCloseWindowAction(CloseWindowAction::Exit);
    QCOMPARE(controller.resolveTrayQuitAction(), QStringLiteral("confirm"));
    QCOMPARE(controller.resolveWindowCloseAction(), QStringLiteral("confirm"));

    // 会话收敛到终态后：托盘退出直接退出
    controller._transfer->rejectReceiveSession(sessionId);
    QVERIFY2(gy::test::waitFor([&controller]() {
        return controller._transfer->activeSessionCount() == 0;
    }, 5000), "拒绝后活动会话应归零");
    QCOMPARE(controller.resolveTrayQuitAction(), QStringLiteral("exit"));

    // 无活动传输：托盘退出不随 hide/ask 记忆漂移（托盘"退出"是显式退出意图）
    controller._config->setCloseWindowAction(CloseWindowAction::Hide);
    QCOMPARE(controller.resolveTrayQuitAction(), QStringLiteral("exit"));
    // 关窗决策仍尊重记忆：hide 只约束关窗动作，两路径语义各自成立
    QCOMPARE(controller.resolveWindowCloseAction(), QStringLiteral("hide"));
    controller._config->setCloseWindowAction(CloseWindowAction::Ask);
    QCOMPARE(controller.resolveTrayQuitAction(), QStringLiteral("exit"));
    QCOMPARE(controller.resolveWindowCloseAction(), QStringLiteral("ask"));

    senderThread.quit();
    QVERIFY(senderThread.wait(3000));
    drainStorageBeforeDestruction(controller);
}

// 托盘打开接收目录入口打开配置的接收路径（O-02 回归锚）：托盘菜单项的 C++
// 落点必须打开当前配置的接收目录（完成提示仅存续 6 秒，错过提示后托盘是唯一
// 直达入口）；目录已不存在时保持 openFolder 既有语义静默不外呼，不得误开任意位置
void TestAppController::testTrayOpenReceiveFolderOpensConfiguredPath()
{
    AppController controller;
    QVERIFY2(!controller.networkListenFailed(), "端口空闲时组合根应正常监听");

    const QString recvDir = _tempDir->path() + "/tray-open-recv";
    controller._config->setReceivePath(recvDir);  // setReceivePath 会确保目录存在

    FileUrlSpy spy;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &spy, "handle");

    controller.openReceiveFolder();
    QVERIFY2(spy.called(), "打开接收目录应外呼一次");
    QCOMPARE(spy.lastUrl(), QUrl::fromLocalFile(recvDir));

    // 接收目录随后被删除：静默无外呼（openFolder 对非目录非文件不动作）
    spy.reset();
    QVERIFY(QDir(recvDir).removeRecursively());
    controller.openReceiveFolder();
    QVERIFY2(!spy.called(), "接收目录不存在时不应外呼打开任何位置");

    QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
    drainStorageBeforeDestruction(controller);
}

// 唤回置顶平台判定（C-09 回归锚）：Wayland 合成器不支持应用自定置顶，
// 该平台唤回必须降级为 show+raise+activate+任务栏提醒，其余平台保持
// 临时置顶策略；实例属性须与运行时平台名同源同判，QML 唤回分支依赖它
void TestAppController::testStaysOnTopSupportedPerPlatform()
{
    QVERIFY2(!AppController::staysOnTopSupportedOnPlatform(QStringLiteral("wayland")),
             "Wayland 平台不应支持应用自定置顶，唤回须降级");
    QVERIFY2(AppController::staysOnTopSupportedOnPlatform(QStringLiteral("xcb")),
             "X11（xcb）平台应保持临时置顶策略");
    QVERIFY2(AppController::staysOnTopSupportedOnPlatform(QStringLiteral("offscreen")),
             "offscreen 等测试平台不应被误降级");
    QVERIFY2(AppController::staysOnTopSupportedOnPlatform(QStringLiteral("windows")),
             "Windows 平台应保持临时置顶策略");
    QVERIFY2(AppController::staysOnTopSupportedOnPlatform(QString()),
             "平台名未能确定时按支持处理，避免误降级");

    // 实例属性与实际平台一致：不硬编码期望值，测试进程平台随环境变化
    AppController controller;
    QVERIFY2(!controller.networkListenFailed(), "端口空闲时组合根应正常监听");
    QCOMPARE(controller.staysOnTopSupported(),
             AppController::staysOnTopSupportedOnPlatform(
                 QGuiApplication::platformName()));
    drainStorageBeforeDestruction(controller);
}

QTEST_MAIN(TestAppController)
#include "test_app_controller.moc"
