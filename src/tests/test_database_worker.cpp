/**
* @file    test_database_worker.cpp
* @version 8.1.0
* @date    2026-10-28
* @author  GY
* @brief   DatabaseWorker 语义测试
*
* 用不可用的数据库入口隔离验证任务线程语义：任务按提交顺序串行执行、
* beginShutdown 后拒绝新任务且不崩溃、drained 在全部已受理任务完成后到达。
*/

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QList>
#include <QSignalSpy>
#include <QThread>

#include "database_worker.h"

#include "core/storage/database_core.h"

namespace {

// 汇总当前进程全部线程的 CPU 时间（USER_HZ 计），用于侦测线程空转
qint64 processCpuJiffies()
{
    qint64 total = 0;
    QDir taskDir(QStringLiteral("/proc/self/task"));
    const QStringList taskIds = taskDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &taskId : taskIds) {
        QFile stat(taskDir.absoluteFilePath(taskId) + QStringLiteral("/stat"));
        if (!stat.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray content = stat.readAll();
        const int commEnd = content.lastIndexOf(')');
        if (commEnd < 0) {
            continue;
        }
        // comm 之后第 1 个字段是 state（总第 3 字段），utime/stime 是总第 14/15 字段
        const QList<QByteArray> rest = content.mid(commEnd + 2).trimmed().split(' ');
        if (rest.size() > 12) {
            total += rest.at(11).toLongLong() + rest.at(12).toLongLong();
        }
    }
    return total;
}

}  // namespace

class TestDatabaseWorker : public QObject {
    Q_OBJECT

private slots:
    void testTasksExecuteInFifoOrder();
    void testSubmitRejectedAfterBeginShutdown();
    void testDrainedAfterAllAcceptedTasks();
    void testNoSpinAfterDrained();
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

// drained 之后线程必须回到阻塞等待：排空请求一次性生效，不得空转自旋
void TestDatabaseWorker::testNoSpinAfterDrained()
{
    gy::storage::DatabaseCore database;
    DatabaseWorker worker{&database};

    QSignalSpy drainedSpy(&worker, &DatabaseWorker::drained);
    worker.beginShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(drainedSpy.count(), 1, 5000);

    // 旧实现排空后请求标志恒真，等待谓词常真，线程在空队列上持续自旋；
    // 修复后线程阻塞在条件变量，200ms 窗口内 CPU 增量应接近零
    // （USER_HZ=100 时空转档约 20 jiffies，阈值 10 jiffies 留足余量）
    const qint64 before = processCpuJiffies();
    QTest::qWait(200);
    const qint64 delta = processCpuJiffies() - before;
    QVERIFY2(delta < 10,
             qPrintable(QStringLiteral("排空后线程仍在空转：CPU 增量 %1 jiffies").arg(delta)));
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
