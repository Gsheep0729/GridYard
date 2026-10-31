/**
* @file    test_single_instance_guard.cpp
* @version 8.2.1
* @date 2026-10-31
* @author  GY
* @brief   单实例守卫测试
*
* 测试用例：首实例获取锁、同目录第二实例获取失败、唤回请求送达
* 既有实例、锁释放后可再次获取。
*/

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "application_paths.h"
#include "single_instance_guard.h"

class TestSingleInstanceGuard : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testFirstInstanceAcquiresAndSecondFails();
    void testActivationRequestDeliveredToFirstInstance();
    void testLockReleasedAllowsReacquire();

private:
    QTemporaryDir *_tempDir = nullptr;
};

void TestSingleInstanceGuard::init()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    // 实例目录覆盖到临时路径，锁文件与激活文件不落生产目录
    ApplicationPaths::coverForTest(_tempDir->path());
}

void TestSingleInstanceGuard::cleanup()
{
    ApplicationPaths::clearTestCover();
    delete _tempDir;
    _tempDir = nullptr;
}

// 首实例获取锁，同实例目录下的第二个守卫获取失败
void TestSingleInstanceGuard::testFirstInstanceAcquiresAndSecondFails()
{
    SingleInstanceGuard first;
    QVERIFY(first.tryAcquire());

    SingleInstanceGuard second;
    QVERIFY2(!second.tryAcquire(), "同实例目录下第二个守卫不得获取锁");
}

// 第二实例的唤回请求应送达既有实例并触发激活通知
void TestSingleInstanceGuard::testActivationRequestDeliveredToFirstInstance()
{
    SingleInstanceGuard first;
    QVERIFY(first.tryAcquire());

    QSignalSpy activatedSpy(&first, &SingleInstanceGuard::activationRequested);
    SingleInstanceGuard second;
    QVERIFY(!second.tryAcquire());
    QVERIFY2(second.notifyExistingInstance(), "唤回请求应成功写入激活文件");

    QTRY_COMPARE_WITH_TIMEOUT(activatedSpy.count(), 1, 3000);
}

// 既有实例释放锁后，同目录的新守卫可以再次获取
void TestSingleInstanceGuard::testLockReleasedAllowsReacquire()
{
    {
        SingleInstanceGuard first;
        QVERIFY(first.tryAcquire());
    }

    SingleInstanceGuard next;
    QVERIFY2(next.tryAcquire(), "锁释放后同实例目录应可再次获取");
}

QTEST_MAIN(TestSingleInstanceGuard)
#include "test_single_instance_guard.moc"
