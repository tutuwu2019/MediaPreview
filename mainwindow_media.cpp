#include "mainwindow.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMediaPlayer>
#include <QPixmap>
#include <QProcess>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>

namespace {
QStringList supportedExtensions()
{
    return {
        "jpg", "jpeg", "png", "bmp", "gif", "webp", "heic", "livp",
        "mp4", "mov", "m4v", "avi", "mkv"
    };
}

QStringList imageExtensions()
{
    return {"jpg", "jpeg", "png", "bmp", "gif", "webp", "heic"};
}

QStringList videoExtensions()
{
    return {"mp4", "mov", "m4v", "avi", "mkv"};
}
}

bool MainWindow::isSupportedPath(const QString &path) const
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return supportedExtensions().contains(ext);
}

bool MainWindow::isImageFile(const QString &path) const
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return imageExtensions().contains(ext);
}

bool MainWindow::isVideoFile(const QString &path) const
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return videoExtensions().contains(ext);
}

bool MainWindow::canDecodeImage(const QString &path) const
{
    QImageReader reader(path);
    return reader.canRead();
}

QString MainWindow::transcodeHeicToDisplayImage(const QString &sourcePath)
{
    const QString ext = QFileInfo(sourcePath).suffix().toLower();
    if (ext != "heic" && ext != "heif") {
        return QString();
    }

    if (m_heicTranscodeCache.contains(sourcePath)) {
        const QString cachedPath = m_heicTranscodeCache.value(sourcePath);
        if (!cachedPath.isEmpty() && QFileInfo::exists(cachedPath)) {
            return cachedPath;
        }
    }

    if (!m_tempDir.isValid()) {
        logLine("transcodeHeic aborted: temporary directory invalid. src=" + sourcePath);
        return QString();
    }

    const QString outPath = m_tempDir.path() + QDir::separator()
                            + QFileInfo(sourcePath).completeBaseName()
                            + "_" + QString::number(qHash(sourcePath)) + ".png";
    if (QFileInfo::exists(outPath)) {
        m_heicTranscodeCache.insert(sourcePath, outPath);
        return outPath;
    }

    auto runConverter = [&](const QString &program, const QStringList &args, QString &stderrText) {
        QProcess process;
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(program, args);
        if (!process.waitForStarted(3000)) {
            stderrText = "process not started";
            return false;
        }

        const bool finished = process.waitForFinished(20000);
        stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return finished && process.exitStatus() == QProcess::NormalExit
               && process.exitCode() == 0 && QFileInfo::exists(outPath);
    };

    QString err;
    const QString ffmpegPath = QStandardPaths::findExecutable("ffmpeg");
    if (!ffmpegPath.isEmpty()) {
        const QStringList args = {
            "-y", "-hide_banner", "-loglevel", "error",
            "-i", sourcePath,
            outPath
        };
        if (runConverter(ffmpegPath, args, err)) {
            logLine("transcodeHeic success via ffmpeg: " + sourcePath + " -> " + outPath);
            m_heicTranscodeCache.insert(sourcePath, outPath);
            return outPath;
        }
        logLine("transcodeHeic ffmpeg failed: " + sourcePath + ", err=" + err);
    }

    const QString magickPath = QStandardPaths::findExecutable("magick");
    if (!magickPath.isEmpty()) {
        const QStringList args = {
            sourcePath,
            outPath
        };
        if (runConverter(magickPath, args, err)) {
            logLine("transcodeHeic success via magick: " + sourcePath + " -> " + outPath);
            m_heicTranscodeCache.insert(sourcePath, outPath);
            return outPath;
        }
        logLine("transcodeHeic magick failed: " + sourcePath + ", err=" + err);
    }

    logLine("transcodeHeic unavailable: no usable converter for " + sourcePath);
    m_heicTranscodeCache.insert(sourcePath, QString());
    return QString();
}

MainWindow::ResolvedMedia MainWindow::resolveMedia(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    logLine("resolveMedia path=" + path + ", ext=" + ext);
    if (ext == "livp") {
        return resolveLivp(path);
    }

    ResolvedMedia media;
    media.originalPath = path;

    if (isImageFile(path)) {
        if (canDecodeImage(path)) {
            media.type = MediaType::Image;
            media.playablePath = path;
        } else {
            const QString converted = transcodeHeicToDisplayImage(path);
            if (!converted.isEmpty()) {
                media.type = MediaType::Image;
                media.playablePath = converted;
                logLine("resolveMedia use transcoded image: " + converted);
            } else {
                media.type = MediaType::Unknown;
                logLine("resolveMedia image decode failed and transcode unavailable: " + path);
            }
        }
    } else if (isVideoFile(path)) {
        media.type = MediaType::Video;
        media.playablePath = path;
    }

    return media;
}

MainWindow::ResolvedMedia MainWindow::resolveLivp(const QString &path)
{
    ResolvedMedia media;
    media.originalPath = path;
    logLine("resolveLivp start: " + path);

    if (!m_tempDir.isValid()) {
        logLine("resolveLivp aborted: temporary directory invalid.");
        return media;
    }

    QString candidateImage;
    QString candidateVideo;

    if (extractLivpCandidates(path, candidateImage, candidateVideo)) {
        logLine("resolveLivp strategy preferVideo=" + QString(m_livpPreferVideo ? "true" : "false")
                + ", candidateImage=" + (candidateImage.isEmpty() ? QString("<none>") : candidateImage)
                + ", candidateVideo=" + (candidateVideo.isEmpty() ? QString("<none>") : candidateVideo));

        auto tryImage = [this, &media](const QString &imagePath) -> bool {
            if (imagePath.isEmpty()) {
                return false;
            }
            QString playableImage = imagePath;
            if (!canDecodeImage(imagePath)) {
                const QString converted = transcodeHeicToDisplayImage(imagePath);
                if (converted.isEmpty()) {
                    logLine("resolveLivp image decoder unavailable and transcode failed: " + imagePath);
                    return false;
                }
                playableImage = converted;
            }

            QPixmap probe(playableImage);
            if (!probe.isNull()) {
                logLine("resolveLivp choose image: " + playableImage);
                media.type = MediaType::Image;
                media.playablePath = playableImage;
                return true;
            }
            logLine("resolveLivp image unsupported by decoder: " + playableImage);
            return false;
        };

        auto tryVideo = [this, &media, &candidateImage](const QString &videoPath) -> bool {
            if (videoPath.isEmpty()) {
                return false;
            }
            logLine("resolveLivp choose video: " + videoPath);
            media.type = MediaType::Video;
            media.playablePath = videoPath;

            if (!candidateImage.isEmpty()) {
                QString fallbackImage = candidateImage;
                if (!canDecodeImage(fallbackImage)) {
                    const QString converted = transcodeHeicToDisplayImage(fallbackImage);
                    if (!converted.isEmpty()) {
                        fallbackImage = converted;
                    }
                }

                QPixmap probe(fallbackImage);
                if (!probe.isNull()) {
                    media.fallbackImagePath = fallbackImage;
                    logLine("resolveLivp video fallback image ready: " + fallbackImage);
                }
            }
            return true;
        };

        if (m_livpPreferVideo) {
            if (tryVideo(candidateVideo) || tryImage(candidateImage)) {
                return media;
            }
        } else {
            if (tryImage(candidateImage) || tryVideo(candidateVideo)) {
                return media;
            }
        }

        logLine("resolveLivp extracted but no supported image/video found.");
    }

    media.type = MediaType::Unknown;
    media.playablePath.clear();
    logLine("resolveLivp fallback -> Unknown (no decodable HEIC/video candidate). path=" + path);

    return media;
}

bool MainWindow::extractLivpCandidates(const QString &path, QString &candidateImage, QString &candidateVideo)
{
    candidateImage.clear();
    candidateVideo.clear();

    const QString baseName = QFileInfo(path).baseName();
    const QString outputDir = m_tempDir.path() + QDir::separator() + baseName + "_" + QString::number(qHash(path));

    QDir outDir(outputDir);
    if (outDir.exists()) {
        outDir.removeRecursively();
    }
    QDir().mkpath(outputDir);

    const QString zipPath = m_tempDir.path() + QDir::separator() + baseName + "_" + QString::number(qHash(path)) + ".zip";
    if (QFile::exists(zipPath)) {
        QFile::remove(zipPath);
    }
    if (!QFile::copy(path, zipPath)) {
        logLine("resolveLivp failed: copy livp to zip failed. src=" + path + ", dst=" + zipPath);
        return false;
    }
    logLine("resolveLivp zip mirror created: " + zipPath);

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    const QString command = QString("Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force")
                                .arg(psQuote(zipPath), psQuote(outputDir));
    logLine("resolveLivp command: " + command);

    process.start("powershell", {"-NoProfile", "-Command", command});
    if (!process.waitForStarted(3000)) {
        logLine("resolveLivp failed: powershell did not start.");
        return false;
    }
    const bool finished = process.waitForFinished(20000);
    const QString stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError());
    logLine("resolveLivp finished=" + QString(finished ? "true" : "false")
            + ", exitCode=" + QString::number(process.exitCode())
            + ", exitStatus=" + QString::number(static_cast<int>(process.exitStatus())));
    if (!stdoutText.trimmed().isEmpty()) {
        logLine("resolveLivp stdout: " + stdoutText.trimmed());
    }
    if (!stderrText.trimmed().isEmpty()) {
        logLine("resolveLivp stderr: " + stderrText.trimmed());
    }

    if (!(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)) {
        return false;
    }

    QDirIterator it(outputDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        logLine("resolveLivp extracted file: " + file);
        if (candidateImage.isEmpty() && isImageFile(file)) {
            candidateImage = file;
        }
        if (candidateVideo.isEmpty() && isVideoFile(file)) {
            candidateVideo = file;
        }
    }

    return true;
}

void MainWindow::showMedia(const ResolvedMedia &media)
{
    logLine("showMedia type=" + QString::number(static_cast<int>(media.type))
            + ", playablePath=" + media.playablePath);

    if (media.type == MediaType::Image) {
        showImage(media);
        return;
    }

    if (media.type == MediaType::Video) {
        showVideo(media);
        return;
    }

    stopVideo();
    m_previewStack->setCurrentWidget(m_imageArea);
    m_imageLabel->setText("无法识别该文件格式");
}

void MainWindow::showImage(const ResolvedMedia &media)
{
    stopVideo();
    m_seekSlider->setRange(0, 0);
    updateVideoTimeLabel(0, 0);

    m_currentPixmap = QPixmap(media.playablePath);
    if (m_currentPixmap.isNull()) {
        logLine("showImage failed: " + media.playablePath);
        m_imageLabel->setText("图片读取失败: " + media.originalPath);
    } else {
        logLine("showImage success: " + media.playablePath);
        if (m_imageScaleMode != ImageScaleMode::ManualZoom) {
            m_imageScaleMode = ImageScaleMode::FitToWindow;
            m_zoomFactor = 1.0;
        }
        updateScaledImage();
        QTimer::singleShot(0, this, [this]() {
            centerImageInView();
        });
    }

    m_previewStack->setCurrentWidget(m_imageArea);
    statusBar()->showMessage("图片: " + media.originalPath, 3000);
}

void MainWindow::showVideo(const ResolvedMedia &media)
{
    m_currentPixmap = QPixmap();
    m_previewStack->setCurrentWidget(m_videoPanel);
    m_pendingFallbackImagePath = media.fallbackImagePath;
    m_pendingFallbackOriginalPath = media.originalPath;

    if (!m_pendingFallbackImagePath.isEmpty()) {
        logLine("showVideo fallback image armed: " + m_pendingFallbackImagePath);
    }

    m_player->setSource(QUrl::fromLocalFile(media.playablePath));
    logLine("showVideo setSource: " + media.playablePath);
    m_player->play();

    m_waitingVideoStart = !m_pendingFallbackImagePath.isEmpty();
    if (m_waitingVideoStart && m_videoStartWatchdog) {
        m_videoStartWatchdog->start();
        logLine("showVideo watchdog armed for startup fallback.");
    }

    statusBar()->showMessage("视频: " + media.originalPath, 3000);
}

void MainWindow::stopVideo()
{
    m_pendingFallbackImagePath.clear();
    m_pendingFallbackOriginalPath.clear();
    m_waitingVideoStart = false;
    if (m_videoStartWatchdog) {
        m_videoStartWatchdog->stop();
    }
    if (m_player->playbackState() != QMediaPlayer::StoppedState) {
        logLine("stopVideo called.");
        m_player->stop();
    }
}

// 根据当前缩放模式和缩放因子计算并设置适合的预览图像显示尺寸，同时在更新后居中显示
void MainWindow::updateScaledImage()
{
    if (m_currentPixmap.isNull() || !m_imageArea) {
        return;
    }

    QPixmap scaled;
    if (m_imageScaleMode == ImageScaleMode::FitToWindow) {
        const QSize viewportSize = m_imageArea->viewport()->size();
        scaled = m_currentPixmap.scaled(
            viewportSize,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
    } else if (m_imageScaleMode == ImageScaleMode::ActualSize) {
        scaled = m_currentPixmap;
    } else {
        const QSize size(
            static_cast<int>(m_currentPixmap.width() * m_zoomFactor),
            static_cast<int>(m_currentPixmap.height() * m_zoomFactor)
        );
        scaled = m_currentPixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    m_imageLabel->setPixmap(scaled);
    QTimer::singleShot(0, this, [this]() {
        centerImageInView();
    });
}

void MainWindow::centerImageInView()
{
    if (!m_imageArea || !m_imageLabel || m_currentPixmap.isNull()) {
        return;
    }

    QScrollBar *hBar = m_imageArea->horizontalScrollBar();
    QScrollBar *vBar = m_imageArea->verticalScrollBar();
    if (!hBar || !vBar) {
        return;
    }

    hBar->setValue((hBar->minimum() + hBar->maximum()) / 2);
    vBar->setValue((vBar->minimum() + vBar->maximum()) / 2);
}

void MainWindow::applyZoom(double ratio)
{
    if (m_previewStack->currentWidget() != m_imageArea || m_currentPixmap.isNull()) {
        return;
    }

    m_imageScaleMode = ImageScaleMode::ManualZoom;
    m_zoomFactor *= ratio;
    m_zoomFactor = qBound(0.1, m_zoomFactor, 8.0);
    updateScaledImage();
}

QString MainWindow::psQuote(const QString &text) const
{
    QString escaped = text;
    escaped.replace("'", "''");
    return "'" + escaped + "'";
}

QString MainWindow::formatMs(qint64 ms) const
{
    const qint64 totalSec = qMax<qint64>(0, ms / 1000);
    const qint64 min = totalSec / 60;
    const qint64 sec = totalSec % 60;
    return QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}

/**
 *   更新 视频播放进度条 信息 
 */
void MainWindow::updateVideoTimeLabel(qint64 positionMs, qint64 durationMs)
{
    if (!m_timeLabel) {
        return;
    }
    m_timeLabel->setText(formatMs(positionMs) + " / " + formatMs(durationMs));
}

void MainWindow::seekByMs(qint64 deltaMs)
{
    if (m_previewStack->currentWidget() != m_videoPanel) {
        return;
    }

    const qint64 duration = m_player->duration();
    if (duration <= 0) {
        return;
    }

    const qint64 current = m_player->position();
    const qint64 target = qBound<qint64>(0, current + deltaMs, duration);
    m_player->setPosition(target);
}
