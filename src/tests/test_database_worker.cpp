/**
* @file    test_database_worker.cpp
* @version 7.18.0
* @date 2026-10-05
* @author  GY
* @brief   DatabaseWorker 语义测试
*
* 用不可用的数据库入口隔离验证任务线程语义：任务按提交顺序串行执行、
* beginShutdown 后拒绝新任务且不崩溃、drained 在全部已受理任务完成后到达。
*
* Change Log:
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.14.2] GY   2026-10-03
* * 初始版本：补齐 DatabaseWorker 的直接测试（Phase1-E）
*/

#include <QtTest/QtTest>
#include <QList>
#include <QSignalSpy>

#include "database_worker.h"
#include "sqlite_database_broker.h"

class TestDatabaseWorker : public QObject {
    Q_OBJECT

private slots:
    void testTasksExecuteInFifoOrder();
    void testSubmitRejectedAfterBeginShutdown();
    void testDrainedAfterAllAcceptedTasks();
};

// 任务按提交顺序在单一队列上串行执行
void TestDatabaseWorker::testTasksExecuteInFifoOrder()
{
    SqliteDatabaseBroker broker;  // 未初始化：任务执行但返回失败，不影响顺序语义
    DatabaseWorker worker{&broker};

    QList<int> executionOrder;
    QSignalSpy finishedSpy(&worker, &DatabaseWorker::taskFinished);

    for (int i = 0; i < 10; ++i) {
        worker.submitTask([&executionOrder, i](SqliteDatabaseBroker &, QString *) {
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
    SqliteDatabaseBroker broker;
    DatabaseWorker worker{&broker};

    worker.beginShutdown();

    int executed = 0;
    QSignalSpy finishedSpy(&worker, &DatabaseWorker::taskFinished);
    worker.submitTask([&executed](SqliteDatabaseBroker &, QString *) {
        ++executed;
        return true;
    });
    worker.submitTask([&executed](SqliteDatabaseBroker &, QString *) {
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
    SqliteDatabaseBroker broker;
    DatabaseWorker worker{&broker};

    QList<int> executionOrder;
    worker.submitTask([&executionOrder](SqliteDatabaseBroker &, QString *) {
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

QTEST_MAIN(TestDatabaseWorker)
#include "test_database_worker.moc"
