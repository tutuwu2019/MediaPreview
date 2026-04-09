#include "mainwindow.h"

#include <QAudioOutput>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QFileInfo>
#include <QDir>
#include <QListWidget>
#include <QMediaPlayer>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>


/**
 *  生成文件列表中的图标，视频文件和LIVP文件会有一个统一的播放标识，其他文件则显示扩展名。
 */
QIcon MainWindow::buildListIcon(const QString &path) const
{
    QPixmap canvas(kThumbWidth, kThumbHeight);
    canvas.fill(QColor(44, 48, 56));

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QString ext = QFileInfo(path).suffix().toLower();
    const bool isVideo = isVideoFile(path);
    const bool isLivp = (ext == "livp");
    const bool showPlayBadge = isVideo || (isLivp && m_livpPreferVideo);

    if (showPlayBadge) {
        const QPoint center(canvas.width() / 2, canvas.height() / 2);       // 在缩略图中心绘制一个带有播放标识的圆形
        const int r = 18;
        painter.setPen(Qt::NoPen);          // 画笔无边框
        painter.setBrush(QColor(255, 255, 255, 220));   // 半透明白色
        painter.drawEllipse(center, r, r);      // 绘制圆形背景
        
        // 绘制播放标识（三角形），相对于圆心稍微偏右一些以保持视觉平衡
        QPolygon triangle;
        triangle << QPoint(center.x() - 5, center.y() - 8)
                 << QPoint(center.x() - 5, center.y() + 8)
                 << QPoint(center.x() + 9, center.y());
        painter.setBrush(QColor(44, 48, 56));
        painter.drawPolygon(triangle);

        // 如果是LIVP文件，在圆形下方再绘制一个小标签
        painter.setPen(QColor(240, 240, 240));
        painter.drawText(QRect(0, 0, canvas.width(), 20), Qt::AlignCenter,
                         isLivp ? "LIVP" : "VIDEO");
    } else {
        painter.setPen(QColor(240, 240, 240));
        painter.drawText(canvas.rect(), Qt::AlignCenter, ext.toUpper());
    }

    return QIcon(canvas);
}

QIcon MainWindow::buildAlbumIcon(const QString &path)
{
    if (m_albumIconCache.contains(path)) {
        return m_albumIconCache.value(path);
    }

    QIcon diskCached;
    if (loadAlbumIconFromDiskCache(path, diskCached)) {
        m_albumIconCache.insert(path, diskCached);
        return diskCached;
    }

    QPixmap thumb;
    bool showPlayOverlay = false;

    const QString ext = QFileInfo(path).suffix().toLower();

    if (isImageFile(path)) {
        QString playablePath = path;
        if (!canDecodeImage(playablePath)) {
            const QString converted = transcodeHeicToDisplayImage(playablePath);
            if (!converted.isEmpty()) {
                playablePath = converted;
            }
        }

        if (canDecodeImage(playablePath)) {
            QPixmap src(playablePath);
            if (!src.isNull()) {
                thumb = src;
            }
        } else {
            logLine("album icon image decode unsupported: " + path);
        }
    } else if (ext == "livp") {
        showPlayOverlay = m_livpPreferVideo;
        QString candidateImage;
        QString candidateVideo;
        if (extractLivpCandidates(path, candidateImage, candidateVideo)) {
            if (!candidateImage.isEmpty()) {
                QString playableImage = candidateImage;
                if (!canDecodeImage(playableImage)) {
                    const QString converted = transcodeHeicToDisplayImage(playableImage);
                    if (!converted.isEmpty()) {
                        playableImage = converted;
                    }
                }
                QPixmap src(playableImage);
                if (!src.isNull()) {
                    thumb = src;
                    logLine("album icon use livp static image: " + playableImage);
                }
            }
            if (thumb.isNull()) {
                logLine("album icon livp fallback placeholder: " + path);
            }
        }
    } else if (isVideoFile(path)) {
        showPlayOverlay = true;
        const QPixmap firstFrame = extractVideoFirstFrame(path);
        if (!firstFrame.isNull()) {
            thumb = firstFrame;
        } else {
            logLine("album icon video fallback placeholder: " + path);
        }
    }

    QIcon icon;
    if (!thumb.isNull()) {
        QPixmap canvas(kThumbWidth, kThumbHeight);
        canvas.fill(QColor(28, 30, 34));
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPixmap scaled = thumb.scaled(
            QSize(kThumbWidth, kThumbHeight),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
        const int x = (kThumbWidth - scaled.width()) / 2;
        const int y = (kThumbHeight - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);

        if (showPlayOverlay) {
            const QPoint center(canvas.width() / 2, canvas.height() / 2);
            const int r = 16;

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 220));
            painter.drawEllipse(center, r, r);

            QPolygon triangle;
            triangle << QPoint(center.x() - 4, center.y() - 7)
                     << QPoint(center.x() - 4, center.y() + 7)
                     << QPoint(center.x() + 8, center.y());
            painter.setBrush(QColor(44, 48, 56));
            painter.drawPolygon(triangle);
        }

        icon = QIcon(canvas);
    } else {
        icon = buildListIcon(path);
    }

    m_albumIconCache.insert(path, icon);
    if (!thumb.isNull()) {
        saveAlbumIconToDiskCache(path, icon);
    }
    return icon;
}

void MainWindow::ensureAlbumIcon(int row)
{
    if (!m_albumListWidget || row < 0 || row >= m_files.size()) {
        return;
    }

    QListWidgetItem *item = m_albumListWidget->item(row);
    if (!item) {
        return;
    }

    const QString &path = m_files.at(row);
    item->setIcon(buildAlbumIcon(path));
}

void MainWindow::ensureAlbumIconsNear(int centerRow, int radius)
{
    if (!m_albumListWidget || m_files.isEmpty() || centerRow < 0) {
        return;
    }

    rebuildAlbumQueueAround(centerRow, radius);
    queueVisibleAlbumIcons();
}

void MainWindow::rebuildAlbumQueueAround(int centerRow, int radius)
{
    if (!m_albumListWidget || m_files.isEmpty() || centerRow < 0 || centerRow >= m_files.size()) {
        return;
    }

    m_albumIconQueue.clear();
    m_albumIconQueuedSet.clear();
    m_albumIconPumpScheduled = false;

    const auto enqueueIfNeeded = [this](int row) {
        if (row < 0 || row >= m_files.size()) {
            return;
        }
        const QString &path = m_files.at(row);
        if (m_albumIconCache.contains(path) || m_albumIconQueuedSet.contains(row)) {
            return;
        }
        m_albumIconQueue.append(row);
        m_albumIconQueuedSet.insert(row);
    };

    enqueueIfNeeded(centerRow);
    const int maxOffset = qMax(0, radius);
    for (int offset = 1; offset <= maxOffset; ++offset) {
        enqueueIfNeeded(centerRow - offset);
        enqueueIfNeeded(centerRow + offset);
    }

    const QPair<int, int> range = albumVisibleRange(2);
    if (range.first >= 0 && range.second >= range.first) {
        for (int row = range.first; row <= range.second; ++row) {
            enqueueIfNeeded(row);
        }
    }

    if (!m_albumIconQueue.isEmpty()) {
        m_albumIconPumpScheduled = true;
        QTimer::singleShot(0, this, [this]() { pumpAlbumIconQueue(); });
    }
}

void MainWindow::queueAlbumIcon(int row, bool highPriority)
{
    if (!m_albumListWidget || row < 0 || row >= m_files.size()) {
        return;
    }
    if (m_albumIconCache.contains(m_files.at(row))) {
        return;
    }
    if (m_albumIconQueuedSet.contains(row)) {
        return;
    }

    if (highPriority) {
        m_albumIconQueue.prepend(row);
    } else {
        m_albumIconQueue.append(row);
    }
    m_albumIconQueuedSet.insert(row);

    if (!m_albumIconPumpScheduled) {
        m_albumIconPumpScheduled = true;
        QTimer::singleShot(0, this, [this]() { pumpAlbumIconQueue(); });
    }
}

void MainWindow::queueVisibleAlbumIcons(int overscan)
{
    const QPair<int, int> range = albumVisibleRange(overscan);
    if (range.first < 0 || range.second < range.first) {
        return;
    }

    for (int row = range.first; row <= range.second; ++row) {
        queueAlbumIcon(row, false);
    }
}

void MainWindow::pumpAlbumIconQueue()
{
    m_albumIconPumpScheduled = false;
    if (!m_albumListWidget || m_albumIconQueue.isEmpty()) {
        return;
    }

    int row = -1;
    while (!m_albumIconQueue.isEmpty()) {
        row = m_albumIconQueue.front();
        m_albumIconQueue.pop_front();
        m_albumIconQueuedSet.remove(row);
        if (row >= 0 && row < m_files.size()) {
            break;
        }
        row = -1;
    }

    if (row >= 0) {
        ensureAlbumIcon(row);
    }

    if (!m_albumIconQueue.isEmpty()) {
        m_albumIconPumpScheduled = true;
        QTimer::singleShot(0, this, [this]() { pumpAlbumIconQueue(); });
    }
}

QPair<int, int> MainWindow::albumVisibleRange(int overscan) const
{
    if (!m_albumListWidget || m_files.isEmpty()) {
        return qMakePair(-1, -1);
    }

    QScrollBar *bar = m_albumListWidget->horizontalScrollBar();
    if (!bar) {
        return qMakePair(-1, -1);
    }

    const int spacing = m_albumListWidget->spacing();
    const int itemWidth = m_albumListWidget->iconSize().width() + spacing + 12;
    if (itemWidth <= 0) {
        return qMakePair(-1, -1);
    }

    const int viewportWidth = m_albumListWidget->viewport()->width();
    const int first = qMax(0, bar->value() / itemWidth - qMax(0, overscan));
    const int visibleCols = qMax(1, viewportWidth / itemWidth + 2);
    const int last = qMin(m_files.size() - 1, first + visibleCols + qMax(0, overscan) * 2);
    return qMakePair(first, last);
}

QString MainWindow::albumThumbCacheFilePath(const QString &path)
{
    if (m_albumThumbCacheDir.isEmpty()) {
        QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (baseDir.isEmpty()) {
            baseDir = QDir::tempPath() + QDir::separator() + "MediaPreviewClient";
        }
        m_albumThumbCacheDir = baseDir + QDir::separator() + "album_cache";
        QDir().mkpath(m_albumThumbCacheDir);
    }

    const QFileInfo info(path);
    const QString keyRaw = info.absoluteFilePath()
                           + "|" + QString::number(info.lastModified().toMSecsSinceEpoch())
                           + "|" + QString::number(info.size())
                           + "|" + QString(m_livpPreferVideo ? "1" : "0")
                           + "|thumb_v2";
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(keyRaw.toUtf8(), QCryptographicHash::Sha1).toHex());
    return m_albumThumbCacheDir + QDir::separator() + key + ".png";
}

bool MainWindow::loadAlbumIconFromDiskCache(const QString &path, QIcon &icon)
{
    const QString cacheFile = albumThumbCacheFilePath(path);
    if (!QFileInfo::exists(cacheFile)) {
        return false;
    }

    QPixmap pix(cacheFile);
    if (pix.isNull()) {
        return false;
    }

    icon = QIcon(pix);
    return true;
}

void MainWindow::saveAlbumIconToDiskCache(const QString &path, const QIcon &icon)
{
    if (icon.isNull()) {
        return;
    }

    const QString cacheFile = albumThumbCacheFilePath(path);
    if (QFileInfo::exists(cacheFile)) {
        return;
    }

    const QPixmap pix = icon.pixmap(QSize(kThumbWidth, kThumbHeight));
    if (!pix.isNull()) {
        pix.save(cacheFile, "PNG");
    }
}

QPixmap MainWindow::extractVideoFirstFrame(const QString &videoPath) const
{
    QMediaPlayer player;
    QAudioOutput audio;
    QVideoSink sink;
    QEventLoop loop;
    QTimer timeout;

    QImage frameImage;
    bool loaded = false;

    timeout.setSingleShot(true);
    timeout.setInterval(5000);

    audio.setVolume(0.0f);
    player.setAudioOutput(&audio);

    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop,
                     [&](QMediaPlayer::Error, const QString &) { loop.quit(); });
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &loop,
                     [&](QMediaPlayer::MediaStatus status) {
                         if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
                             loaded = true;
                             player.setPosition(100);
                             player.play();
                         }
                     });
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop,
                     [&](const QVideoFrame &frame) {
                         if (!frame.isValid()) {
                             return;
                         }
                         const QImage img = frame.toImage();
                         if (img.isNull()) {
                             return;
                         }
                         frameImage = img;
                         loop.quit();
                     });

    player.setVideoSink(&sink);
    player.setSource(QUrl::fromLocalFile(videoPath));

    timeout.start();
    loop.exec();
    player.stop();

    if (!loaded) {
        logLine("extractVideoFirstFrame media not loaded: " + videoPath);
    }
    if (frameImage.isNull()) {
        logLine("extractVideoFirstFrame no frame captured: " + videoPath);
        return QPixmap();
    }

    logLine("extractVideoFirstFrame success: " + videoPath);
    return QPixmap::fromImage(frameImage);
}
