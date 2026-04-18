#include "mainwindow.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFutureWatcher>
#include <QImageReader>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>
#include <QProcess>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>


namespace {

// ──────────────────────────────────────────────────────────────────
// 以下自由函数均为线程安全：仅使用传入参数 + 本地实例，无成员访问。
// ──────────────────────────────────────────────────────────────────

static QStringList thumbImageExtensions()
{
    return {
        "jpg", "jpeg", "jpe", "jfif",
        "png", "bmp", "dib", "rle",
        "gif", "webp",
        "heic", "heif",
        "tif", "tiff",
        "ico", "cur", "icns",
        "svg", "svgz",
        "avif", "jxl", "jp2", "j2k", "jxr",
        "tga", "ppm", "pgm", "pbm", "xbm", "xpm"
    };
}

static QStringList thumbVideoExtensions()
{
    return {
        "mp4", "m4v", "mov", "qt",
        "avi", "mkv", "mk3d", "webm",
        "wmv", "asf",
        "flv", "f4v",
        "mpeg", "mpg", "mpe", "m2v", "mpv",
        "ts", "mts", "m2ts",
        "vob", "3gp", "3g2",
        "ogv", "ogm",
        "rm", "rmvb", "dv", "divx", "xvid"
    };
}

// 提取视频首帧。
// 使用 ffmpeg CLI 而非 QMediaPlayer，避免在线程池线程中创建 WMF/COM 对象。
// QMediaPlayer 底层走 WMF（COM STA），在无 COM 初始化的线程池线程中运行时，
// WMF 会把内部回调 marshal 到主线程处理，导致切回窗口时主线程被积压的
// marshal 请求淹没，产生卡死现象。ffmpeg 以独立进程运行，完全隔离 COM 问题。
static QPixmap extractVideoFirstFrameStatic(const QString &videoPath, const QString &tempDirPath)
{
    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) return QPixmap();

    const QString outPath = tempDirPath + QDir::separator()
        + QFileInfo(videoPath).completeBaseName()
        + "_" + QString::number(qHash(videoPath)) + "_frame.jpg";

    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(ffmpeg, {"-y", "-hide_banner", "-loglevel", "error",
                     "-ss", "0.1", "-i", videoPath,
                     "-vframes", "1", "-q:v", "3", outPath});
    if (!p.waitForStarted(3000) || !p.waitForFinished(10000)) return QPixmap();
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) return QPixmap();
    if (!QFileInfo::exists(outPath)) return QPixmap();

    const QImage img(outPath);
    QFile::remove(outPath);
    return img.isNull() ? QPixmap() : QPixmap::fromImage(img);
}

// HEIC 转码（幂等，多线程并发调用安全——最坏情况是两次写同一个输出文件，结果相同）
static QString transcodeHeicStaticIfNeeded(const QString &path, const QString &tempDirPath)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext != "heic" && ext != "heif") return path;

    const QString outPath = tempDirPath + QDir::separator()
                            + QFileInfo(path).completeBaseName()
                            + "_" + QString::number(qHash(path)) + ".png";
    if (QFileInfo::exists(outPath)) return outPath;

    auto tryConverter = [&](const QString &program, const QStringList &args) -> bool {
        QProcess p;
        p.setProcessChannelMode(QProcess::SeparateChannels);
        p.start(program, args);
        return p.waitForStarted(3000) && p.waitForFinished(20000)
               && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0
               && QFileInfo::exists(outPath);
    };

    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (!ffmpeg.isEmpty()
        && tryConverter(ffmpeg, {"-y", "-hide_banner", "-loglevel", "error", "-i", path, outPath}))
        return outPath;

    const QString magick = QStandardPaths::findExecutable("magick");
    if (!magick.isEmpty() && tryConverter(magick, {path, outPath}))
        return outPath;

    return {};
}

// LIVP 解压提取候选图片/视频路径（线程安全：仅使用传入的 tempDirPath）
static bool extractLivpCandidatesStatic(const QString &livpPath, const QString &tempDirPath,
                                         QString &candidateImage, QString &candidateVideo)
{
    candidateImage.clear();
    candidateVideo.clear();

    const QString baseName = QFileInfo(livpPath).baseName();
    const QString hash = QString::number(qHash(livpPath));
    const QString outputDir = tempDirPath + QDir::separator() + baseName + "_" + hash;
    const QString zipPath   = tempDirPath + QDir::separator() + baseName + "_" + hash + ".zip";

    QDir outDir(outputDir);
    if (outDir.exists()) outDir.removeRecursively();
    QDir().mkpath(outputDir);

    if (QFile::exists(zipPath)) QFile::remove(zipPath);
    if (!QFile::copy(livpPath, zipPath)) return false;

    auto psq = [](const QString &s) { return "'" + QString(s).replace("'", "''") + "'"; };
    const QString cmd = QString("Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force")
                            .arg(psq(zipPath), psq(outputDir));

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start("powershell", {"-NoProfile", "-Command", cmd});
    if (!process.waitForStarted(3000)) return false;
    if (!process.waitForFinished(20000)) return false;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return false;

    QDirIterator it(outputDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString fext = QFileInfo(file).suffix().toLower();
        if (candidateImage.isEmpty() && thumbImageExtensions().contains(fext)) candidateImage = file;
        if (candidateVideo.isEmpty() && thumbVideoExtensions().contains(fext)) candidateVideo = file;
    }
    return true;
}

struct AlbumThumbRequest {
    QString path;               // 原始文件路径（视频帧提取用）
    QString resolvedImagePath;  // 已解析的图片路径（HEIC→PNG、LIVP→extracted image 等）
    bool isVideo = false;       // true 则从 path 提取视频首帧
    bool isLivp = false;        // true 则在线程内完整走 LIVP 解压流程
    bool showPlayOverlay = false;
    QString tempDirPath;        // LIVP / HEIC 转码需要的临时目录
    bool livpPreferVideo = true;
};

// 相册缩略图生成主体（线程安全自由函数）
static QIcon buildAlbumIconInThread(const AlbumThumbRequest &req)
{
    static constexpr int W = 96, H = 72;

    QPixmap thumb;
    bool overlay = req.showPlayOverlay;

    // 解码指定路径的图片到缩略图尺寸（避免全分辨率加载）
    auto readScaledThumb = [&](const QString &imgPath) -> QPixmap {
        // 用独立 reader 探测尺寸，避免部分插件（WebP/AVIF 等）调用 size() 后污染读取状态
        const QSize native = QImageReader(imgPath).size();

        QImageReader reader(imgPath);
        reader.setAutoTransform(true);
        if (native.isValid() && !native.isEmpty()) {
            reader.setScaledSize(
                native.scaled(W, H, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
        }
        QImage img = reader.read();
        // 部分插件不支持 setScaledSize，回退到全分辨率再缩放
        if (img.isNull()) {
            QImageReader fallback(imgPath);
            fallback.setAutoTransform(true);
            img = fallback.read();
            if (!img.isNull())
                img = img.scaled(W, H, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return img.isNull() ? QPixmap() : QPixmap::fromImage(img);
    };

    if (req.isLivp) {
        QString candidateImage, candidateVideo;
        if (extractLivpCandidatesStatic(req.path, req.tempDirPath, candidateImage, candidateVideo)) {
            if (req.livpPreferVideo && !candidateVideo.isEmpty()) {
                thumb = extractVideoFirstFrameStatic(candidateVideo, req.tempDirPath);
                overlay = true;
            } else if (!candidateImage.isEmpty()) {
                const QString resolved = transcodeHeicStaticIfNeeded(candidateImage, req.tempDirPath);
                if (!resolved.isEmpty()) thumb = readScaledThumb(resolved);
            }
        }
    } else if (req.isVideo) {
        thumb = extractVideoFirstFrameStatic(req.path, req.tempDirPath);
    } else if (!req.resolvedImagePath.isEmpty()) {
        thumb = readScaledThumb(req.resolvedImagePath);
    }

    if (thumb.isNull()) return QIcon();

    QPixmap canvas(W, H);
    canvas.fill(QColor(28, 30, 34));
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPixmap scaled = thumb.scaled(QSize(W, H), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawPixmap((W - scaled.width()) / 2, (H - scaled.height()) / 2, scaled);

    if (overlay) {
        const QPoint center(W / 2, H / 2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 220));
        painter.drawEllipse(center, 16, 16);
        QPolygon tri;
        tri << QPoint(center.x() - 4, center.y() - 7)
            << QPoint(center.x() - 4, center.y() + 7)
            << QPoint(center.x() + 8, center.y());
        painter.setBrush(QColor(44, 48, 56));
        painter.drawPolygon(tri);
    }

    return QIcon(canvas);
}

} // anonymous namespace

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
            const QImage orientedImage = loadDisplayImage(playablePath);
            if (!orientedImage.isNull()) {
                thumb = QPixmap::fromImage(orientedImage);
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
                const QImage orientedImage = loadDisplayImage(playableImage);
                if (!orientedImage.isNull()) {
                    thumb = QPixmap::fromImage(orientedImage);
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

// LQIP 占位图：对图片文件做极小分辨率读取后放大产生模糊效果；其他类型返回 buildListIcon
QIcon MainWindow::buildLqipIcon(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (!thumbImageExtensions().contains(ext)) return buildListIcon(path);

    QImageReader reader(path);
    if (!reader.canRead()) return buildListIcon(path);

    const QSize orig = reader.size();
    if (!orig.isValid() || orig.isEmpty()) return buildListIcon(path);

    const QSize tiny = orig.scaled(8, 6, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    reader.setScaledSize(tiny);
    const QImage tinyImg = reader.read();
    if (tinyImg.isNull()) return buildListIcon(path);

    const QPixmap blurred = QPixmap::fromImage(
        tinyImg.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    QPixmap canvas(kThumbWidth, kThumbHeight);
    canvas.fill(QColor(28, 30, 34));
    QPainter p(&canvas);
    p.setOpacity(0.55);
    p.drawPixmap((kThumbWidth - blurred.width()) / 2,
                 (kThumbHeight - blurred.height()) / 2,
                 blurred);
    return QIcon(canvas);
}

// 取消并清理所有挂起的异步缩略图 watcher
void MainWindow::cancelAllAlbumThumbWatchers()
{
    for (auto *watcher : std::as_const(m_albumThumbWatchers)) {
        watcher->cancel();
        // finished 信号仍会触发，isCanceled() 检查会提前退出并 deleteLater
    }
    m_albumThumbWatchers.clear();
    m_albumThumbRunning = 0;
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
    if (!m_albumListWidget || m_albumIconQueue.isEmpty()) return;

    int row = -1;
    while (!m_albumIconQueue.isEmpty()) {
        row = m_albumIconQueue.front();
        m_albumIconQueue.pop_front();
        m_albumIconQueuedSet.remove(row);
        if (row >= 0 && row < m_files.size()) break;
        row = -1;
    }

    if (row >= 0) {
        const QString &path = m_files.at(row);
        QListWidgetItem *item = m_albumListWidget->item(row);

        if (item) {
            // 1. 内存缓存命中 → 直接设置，无需派发
            if (m_albumIconCache.contains(path)) {
                item->setIcon(m_albumIconCache.value(path));
            }
            // 2. 磁盘缓存命中 → 写入内存缓存并设置
            else {
                QIcon diskIcon;
                if (loadAlbumIconFromDiskCache(path, diskIcon)) {
                    m_albumIconCache.insert(path, diskIcon);
                    item->setIcon(diskIcon);
                }
                // 3. 无缓存 && 尚无挂起 watcher → LQIP + 异步生成
                else if (!m_albumThumbWatchers.contains(row)) {
                    // LQIP 占位始终立即设置
                    item->setIcon(buildLqipIcon(path));

                    // 并发上限保护：超限则将当前行重新入队，等待有 watcher 完成再继续
                    if (m_albumThumbRunning >= kMaxAlbumThumbConcurrent) {
                        m_albumIconQueue.prepend(row);
                        m_albumIconQueuedSet.insert(row);
                        // 不在此处调度下一轮 pump，由 watcher callback 触发
                        return;
                    }

                    // 在主线程解析 HEIC 路径（利用已有缓存，通常零开销）
                    const QString ext = QFileInfo(path).suffix().toLower();
                    const bool isVideo = thumbVideoExtensions().contains(ext);
                    const bool isLivp  = (ext == "livp");
                    const bool isImage = !isVideo && !isLivp && thumbImageExtensions().contains(ext);

                    AlbumThumbRequest req;
                    req.path            = path;
                    req.isVideo         = isVideo;
                    req.isLivp          = isLivp;
                    req.showPlayOverlay = isVideo || (isLivp && m_livpPreferVideo);
                    req.tempDirPath     = m_tempDir.path();
                    req.livpPreferVideo = m_livpPreferVideo;

                    if (isImage) {
                        if (!canDecodeImage(path)) {
                            const QString conv = transcodeHeicToDisplayImage(path);
                            req.resolvedImagePath = conv.isEmpty() ? path : conv;
                        } else {
                            req.resolvedImagePath = path;
                        }
                    }

                    ++m_albumThumbRunning;
                    const int capturedRow    = row;
                    const QString capturedPath = path;

                    auto *watcher = new QFutureWatcher<QIcon>(this);
                    m_albumThumbWatchers.insert(row, watcher);

                    connect(watcher, &QFutureWatcher<QIcon>::finished, this,
                            [this, watcher, capturedRow, capturedPath]() {
                        m_albumThumbWatchers.remove(capturedRow);
                        watcher->deleteLater();
                        m_albumThumbRunning = qMax(0, m_albumThumbRunning - 1);

                        // 有 slot 空出来，若队列非空则恢复 pump
                        if (!m_albumIconQueue.isEmpty() && !m_albumIconPumpScheduled) {
                            m_albumIconPumpScheduled = true;
                            QTimer::singleShot(0, this, [this]() { pumpAlbumIconQueue(); });
                        }

                        if (watcher->isCanceled()) return;
                        if (capturedRow >= m_files.size()
                            || m_files.at(capturedRow) != capturedPath) return;
                        if (m_albumIconCache.contains(capturedPath)) return;

                        const QIcon icon = watcher->result();
                        const QIcon finalIcon = icon.isNull() ? buildListIcon(capturedPath) : icon;
                        m_albumIconCache.insert(capturedPath, finalIcon);
                        if (!icon.isNull()) saveAlbumIconToDiskCache(capturedPath, finalIcon);
                        if (m_albumListWidget) {
                            if (QListWidgetItem *it = m_albumListWidget->item(capturedRow)) {
                                it->setIcon(finalIcon);
                            }
                        }
                    });

                    watcher->setFuture(QtConcurrent::run([req]() -> QIcon {
                        return buildAlbumIconInThread(req);
                    }));
                }
            }
        }
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
        QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);    // 获取系统标准应用数据目录  AppData/Roaming
        if (baseDir.isEmpty()) {
            baseDir = QDir::tempPath() + QDir::separator() + "MediaPreviewClient";
        }
        m_albumThumbCacheDir = baseDir + QDir::separator() + "album_cache";
        QDir().mkpath(m_albumThumbCacheDir);
    }

    /**
     *  生成缓存文件路径的逻辑：
     *  1. 使用文件的绝对路径、最后修改时间、文件大小、当前 LIVP 解析偏好（优先视频或优先静态图）等信息构成一个原始字符串 keyRaw。
     *  2. 对 keyRaw 进行 SHA-1 哈希，得到一个固定长度的哈希值字符串 key，避免文件系统对长路径或特殊字符的限制，同时也能较好地分散缓存文件。
     *  3. 将 key 作为缓存文件名的一部分，存储在 albumThumbCacheDir 目录下，文件扩展名为 .png。
     */
    const QFileInfo info(path);
    const QString keyRaw = info.absoluteFilePath()
                           + "|" + QString::number(info.lastModified().toMSecsSinceEpoch())
                           + "|" + QString::number(info.size())
                           + "|" + QString(m_livpPreferVideo ? "1" : "0")
                           + "|thumb_v4";
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

    // 生成像素图
    QPixmap pix(cacheFile);
    if (pix.isNull()) {
        return false;
    }

    // 转成图标 
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
    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) {
        logLine("extractVideoFirstFrame: ffmpeg not found, skipping: " + videoPath);
        return QPixmap();
    }

    const QString outPath = m_tempDir.path() + QDir::separator()
        + QFileInfo(videoPath).completeBaseName()
        + "_" + QString::number(qHash(videoPath)) + "_frame.jpg";

    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(ffmpeg, {"-y", "-hide_banner", "-loglevel", "error",
                     "-ss", "0.1", "-i", videoPath,
                     "-vframes", "1", "-q:v", "3", outPath});
    if (!p.waitForStarted(3000) || !p.waitForFinished(10000)) {
        logLine("extractVideoFirstFrame ffmpeg timeout/failed: " + videoPath);
        return QPixmap();
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        logLine("extractVideoFirstFrame ffmpeg non-zero exit: " + videoPath);
        return QPixmap();
    }
    if (!QFileInfo::exists(outPath)) {
        logLine("extractVideoFirstFrame ffmpeg no output file: " + videoPath);
        return QPixmap();
    }

    const QImage img(outPath);
    QFile::remove(outPath);
    if (img.isNull()) {
        logLine("extractVideoFirstFrame image load failed: " + videoPath);
        return QPixmap();
    }

    logLine("extractVideoFirstFrame success: " + videoPath);
    return QPixmap::fromImage(img);
}
