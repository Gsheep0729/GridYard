/**
* @file    test_app_controller.cpp
* @version 8.2.1
* @date 2026-10-31
* @author  GY
* @brief   组合根启动路径测试
*
* 测试用例：P2P 监听端口被占时启动失败标记随属性发布，端口空闲时不标记。
*/

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "app_controller.h"
#include "application_paths.h"
#include "ip_address.h"
#include "local_data_broker.h"
#include "posix_tcp_server.h"

class TestAppController : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testListenFailureMarkedWhenPortOccupied();
    void testListenSuccessMarkedWhenPortFree();

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

QTEST_MAIN(TestAppController)
#include "test_app_controller.moc"
