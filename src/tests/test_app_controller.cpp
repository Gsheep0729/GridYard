/**
* @file    test_app_controller.cpp
* @version 8.2.1
* @date 2026-11-01
* @author  GY
* @brief   组合根启动路径测试
*
* 测试用例：P2P 监听端口被占时启动失败标记随属性发布，端口空闲时不标记；
* 协调服务器错误令牌等交互错误随属性发布、注册确认到达后清空；
* 托盘退出决策与关窗退出分支同判（活动传输拦截不随记忆豁免）。
*/

#include <QtTest/QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

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

class TestAppController : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testListenFailureMarkedWhenPortOccupied();
    void testListenSuccessMarkedWhenPortFree();
    void testRendezvousErrorSurfacesAndClearsOnAck();
    void testTrayQuitDecisionMatchesWindowCloseGuard();

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

QTEST_MAIN(TestAppController)
#include "test_app_controller.moc"
