#include "mainwindow.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTextStream>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Win11 图片/视频预览客户端");
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
