#include "mainwindow.h"

#include <QCloseEvent>
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

MainWindow::~MainWindow()
{
    if (m_cleanupThread && m_cleanupThread->isRunning()) {
        m_cleanupThread->wait(5000);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 取消所有挂起的异步任务，避免任务在析构后访问已销毁对象
    cancelImageLoad();
    cancelAllAlbumThumbWatchers();

    // 禁止 QTemporaryDir 析构时自行删除（由清理线程接管）
    m_tempDir.setAutoRemove(false);
    const QString tempPath = m_tempDir.path();

    // 创建并启动清理线程，退出时在后台删除临时目录
    m_cleanupThread = new QThread(this);
    connect(m_cleanupThread, &QThread::started, [tempPath, thread = m_cleanupThread]() {
        QDir(tempPath).removeRecursively();
        thread->quit();
    });
    m_cleanupThread->start();
    logLine("closeEvent: cleanup thread started, temp=" + tempPath);

    event->accept();
    QMainWindow::closeEvent(event);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    const QString buildStamp = QString("%1 %2").arg(__DATE__, __TIME__);
    setWindowTitle("Win11 图片/视频预览客户端 [" + buildStamp + "]");
    resize(1200, 760);
    setAcceptDrops(true);

    setupUi();
    setupLogging();
    setupConnections();
}

void MainWindow::setupLogging()
{
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (logDir.isEmpty()) {
        logDir = QDir::tempPath() + QDir::separator() + "MediaPreviewClient";
    }

    QDir().mkpath(logDir);
    m_logFilePath = logDir + QDir::separator() + "preview.log";

    QFile file(m_logFilePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << "\n========== Session Start: "
            << QDateTime::currentDateTime().toString(Qt::ISODate)
            << " ==========" << Qt::endl;
    }

    statusBar()->showMessage("日志文件: " + m_logFilePath, 6000);
    logLine("Application started.");
    logLine("Binary path: " + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    logLine("Build stamp: " + QString(__DATE__) + " " + QString(__TIME__));
}

void MainWindow::logLine(const QString &line) const
{
    const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    const QString msg = "[" + stamp + "] " + line;

    qInfo().noquote() << msg;

    if (m_logFilePath.isEmpty()) {
        return;
    }

    QFile file(m_logFilePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << msg << Qt::endl;
    }
}
