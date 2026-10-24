/**
* @file    test_database_worker.cpp
* @version 7.26.1
* @date    2026-10-25
* @author  GY
* @brief   DatabaseWorker 语义测试
*
* 用不可用的数据库入口隔离验证任务线程语义：任务按提交顺序串行执行、
* beginShutdown 后拒绝新任务且不崩溃、drained 在全部已受理任务完成后到达。
*/

#include <QtTest/QtTest>
#include <QList>
#include <QSignalSpy>
#include <QThread>

#include "database_worker.h"

#include "core/storage/database_core.h"

class TestDatabaseWorker : public QObject {
    Q_OBJECT

private slots:
    void testTasksExecuteInFifoOrder();
    void testSubmitRejectedAfterBeginShutdown();
    void testDrainedAfterAllAcceptedTasks();
    void testTasksRunOffCallerThread();
    void testWorkerDestroyedOnOwnThreadBeforeQuit();
};

// 任务按提交顺序在单一队列上串行执行
void TestDatabaseWorker::testTasksExecuteInFifoOrder()
{
    gy::storage::DatabaseCore database;  // 未初始化：任务照常执行，不影响顺序语义
    DatabaseWorker worker{&database};

    QList<int> executionOrder;
    QSignalSpy finishedSpy(&worker, &DatabaseWorker::taskFinished);

    for (int i = 0; i < 10; ++i) {
        worker.submitTask([&executionOrder, i](gy::storage::DatabaseCore &, QString *) {
            executionOrder.append(i);
            return true;
        });
    }

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 10, 5000);
    QCOMPARE(executionOrder, QList<int>({0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
}

// beginShutdown 之后提交的任务被静默拒绝，不执行也不崩溃
void TestDatabaseWorker::testSubmitRejectedAfterBeginShutdown()
{
    gy::storage::DatabaseCore database;
    DatabaseWorker worker{&database};

    worker.beginShutdown();

    int executed = 0;
    QSignalSpy finishedSpy(&worker, &DatabaseWorker::taskFinished);
    worker.submitTask([&executed](gy::storage::DatabaseCore &, QString *) {
        ++executed;
        return true;
    });
    worker.submitTask([&executed](gy::storage::DatabaseCore &, QString *) {
        ++executed;
        return true;
    });

    // 泵一轮事件循环确认没有任何任务被调度执行
    QTest::qWait(100);
    QCOMPARE(executed, 0);
    QCOMPARE(finishedSpy.count(), 0);
}

// drained 在所有已受理任务完成后到达，与 LocalDataBroker 的排空调用方式一致
void TestDatabaseWorker::testDrainedAfterAllAcceptedTasks()
{
    gy::storage::DatabaseCore database;
    DatabaseWorker worker{&database};

    QList<int> executionOrder;
    worker.submitTask([&executionOrder](gy::storage::DatabaseCore &, QString *) {
        executionOrder.append(1);
        QTest::qWait(50);  // 拉长任务耗时，验证 drained 不会提前到达
        executionOrder.append(2);
        return true;
    });

    QSignalSpy drainedSpy(&worker, &DatabaseWorker::drained);
    // 与 LocalDataBroker::beginShutdown 相同的排队调用方式
    QMetaObject::invokeMethod(&worker, &DatabaseWorker::beginShutdown, Qt::QueuedConnection);

    QTRY_COMPARE_WITH_TIMEOUT(drainedSpy.count(), 1, 5000);
    QCOMPARE(executionOrder, QList<int>({1, 2}));
}

// 任务在 Worker 自有线程执行，不占用调用方线程（std::thread 队列的线程归属契约）
void TestDatabaseWorker::testTasksRunOffCallerThread()
{
    gy::storage::DatabaseCore database;
    DatabaseWorker worker{&database};

    const Qt::HANDLE callerThread = QThread::currentThreadId();
    Qt::HANDLE workerThread = nullptr;
    QSignalSpy finishedSpy(&worker, &DatabaseWorker::taskFinished);

    worker.submitTask([&workerThread](gy::storage::DatabaseCore &, QString *) {
        workerThread = QThread::currentThreadId();
        return true;
    });

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QVERIFY(workerThread != nullptr);
    QVERIFY(workerThread != callerThread);  // 必须在独立任务线程上执行
}

// 托管在存储专用线程上的 Worker 按 closeStorage 同构序列收尾：
// quit 前经阻塞队列调用回到归属线程同步删除，析构必然执行
void TestDatabaseWorker::testWorkerDestroyedOnOwnThreadBeforeQuit()
{
    gy::storage::DatabaseCore database;
    QThread storageThread;
    DatabaseWorker *worker = new DatabaseWorker{&database};
    worker->moveToThread(&storageThread);
    storageThread.start();

    QSignalSpy finishedSpy(worker, &DatabaseWorker::taskFinished);
    worker->submitTask([](gy::storage::DatabaseCore &, QString *) { return true; });
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);

    // 与 LocalDataBroker::closeStorage 相同的收尾序列
    QSignalSpy destroyedSpy(worker, &QObject::destroyed);
    QMetaObject::invokeMethod(worker, [worker] { delete worker; },
                              Qt::BlockingQueuedConnection);
    storageThread.quit();
    storageThread.wait();
    QCOMPARE(destroyedSpy.count(), 1);
}

QTEST_MAIN(TestDatabaseWorker)
#include "test_database_worker.moc"
