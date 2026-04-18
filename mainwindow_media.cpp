#include "mainwindow.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QColorSpace>
#include <QMediaPlayer>
#include <QMovie>
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
#include <QRegularExpression>
#include <QLocale>
#include <QFontMetrics>
#include <QtEndian>
#include <QtConcurrent>

namespace {
quint16 readU16(const QByteArray &data, int offset, bool littleEndian, bool *ok)
{
    if (offset < 0 || offset + 2 > data.size()) {
        *ok = false;
        return 0;
    }
    *ok = true;
    const uchar *p = reinterpret_cast<const uchar *>(data.constData() + offset);
    return littleEndian ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
}

quint32 readU32(const QByteArray &data, int offset, bool littleEndian, bool *ok)
{
    if (offset < 0 || offset + 4 > data.size()) {
        *ok = false;
        return 0;
    }
    *ok = true;
    const uchar *p = reinterpret_cast<const uchar *>(data.constData() + offset);
    return littleEndian ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
}

int jpegExifOrientation(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }

    const QByteArray data = file.readAll();
    if (data.size() < 4) {
        return 0;
    }

    const uchar *raw = reinterpret_cast<const uchar *>(data.constData());
    if (!(raw[0] == 0xFF && raw[1] == 0xD8)) {
        return 0;
    }

    int pos = 2;
    while (pos + 4 <= data.size()) {
        if (static_cast<uchar>(data.at(pos)) != 0xFF) {
            break;
        }
        const int marker = static_cast<uchar>(data.at(pos + 1));
        pos += 2;

        if (marker == 0xD9 || marker == 0xDA) {
            break;
        }

        const int segmentLen = (static_cast<uchar>(data.at(pos)) << 8)
                               | static_cast<uchar>(data.at(pos + 1));
        if (segmentLen < 2 || pos + segmentLen > data.size()) {
            break;
        }

        if (marker == 0xE1 && segmentLen >= 10) {
            const int exifStart = pos + 2;
            if (exifStart + 6 <= data.size() && data.mid(exifStart, 6) == QByteArray("Exif\0\0", 6)) {
                const int tiff = exifStart + 6;
                if (tiff + 8 > data.size()) {
                    return 0;
                }

                const QByteArray order = data.mid(tiff, 2);
                const bool littleEndian = (order == "II");
                const bool bigEndian = (order == "MM");
                if (!littleEndian && !bigEndian) {
                    return 0;
                }

                bool ok = false;
                const quint32 ifd0Offset = readU32(data, tiff + 4, littleEndian, &ok);
                if (!ok) {
                    return 0;
                }

                const int ifd0 = tiff + static_cast<int>(ifd0Offset);
                const quint16 entryCount = readU16(data, ifd0, littleEndian, &ok);
                if (!ok) {
                    return 0;
                }

                for (quint16 i = 0; i < entryCount; ++i) {
                    const int entry = ifd0 + 2 + i * 12;
                    const quint16 tag = readU16(data, entry, littleEndian, &ok);
                    if (!ok) {
                        return 0;
                    }

                    if (tag != 0x0112) {
                        continue;
                    }

                    const quint16 type = readU16(data, entry + 2, littleEndian, &ok);
                    if (!ok) {
                        return 0;
                    }
                    const quint32 count = readU32(data, entry + 4, littleEndian, &ok);
                    if (!ok) {
                        return 0;
                    }

                    if (type == 3 && count >= 1) {
                        const quint16 orientation = readU16(data, entry + 8, littleEndian, &ok);
                        if (ok && orientation >= 1 && orientation <= 8) {
                            return static_cast<int>(orientation);
                        }
                    }
                    return 0;
                }
            }

            // Some cameras/editors store orientation in XMP APP1 metadata instead of EXIF.
            const QByteArray segment = data.mid(exifStart, qMax(0, segmentLen - 2));
            const QString xmp = QString::fromUtf8(segment);
            const QRegularExpression attrPattern(R"(tiff:Orientation\s*=\s*['"]([1-8])['"])");
            const QRegularExpression elemPattern(R"(<tiff:Orientation>\s*([1-8])\s*</tiff:Orientation>)");
            QRegularExpressionMatch m = attrPattern.match(xmp);
            if (!m.hasMatch()) {
                m = elemPattern.match(xmp);
            }
            if (m.hasMatch()) {
                bool ok = false;
                const int orientation = m.captured(1).toInt(&ok);
                if (ok && orientation >= 1 && orientation <= 8) {
                    return orientation;
                }
            }
        }

        pos += segmentLen;
    }

    return 0;
}

QImage applyExifOrientation(const QImage &src, int orientation)
{
    if (src.isNull()) {
        return src;
    }

    switch (orientation) {
    case 2:
        return src.flipped(Qt::Horizontal);
    case 3:
        return src.transformed(QTransform().rotate(180));
    case 4:
        return src.flipped(Qt::Vertical);
    case 5:
        return src.transformed(QTransform(0, 1, 1, 0, 0, 0));
    case 6:
        return src.transformed(QTransform().rotate(90));
    case 7:
        return src.transformed(QTransform(0, -1, -1, 0, 0, 0));
    case 8:
        return src.transformed(QTransform().rotate(-90));
    default:
        return src;
    }
}

QStringList supportedExtensions()
{
    return {
        // Image formats (suffix-level filtering; actual decode depends on Qt plugins/codecs).
        "jpg", "jpeg", "jpe", "jfif",
        "png", "bmp", "dib", "rle",
        "gif", "webp",
        "heic", "heif",
        "tif", "tiff",
        "ico", "cur", "icns",
        "svg", "svgz",
        "avif", "jxl", "jp2", "j2k", "jxr",
        "tga", "ppm", "pgm", "pbm", "xbm", "xpm",

        // Video formats (suffix-level filtering; actual playback depends on backend/codecs).
        "mp4", "m4v", "mov", "qt",
        "avi", "mkv", "mk3d", "webm",
        "wmv", "asf",
        "flv", "f4v",
        "mpeg", "mpg", "mpe", "m2v", "mpv",
        "ts", "mts", "m2ts",
        "vob", "3gp", "3g2",
        "ogv", "ogm",
        "rm", "rmvb", "dv", "divx", "xvid",

        // Live photo container used by Apple ecosystem.
        "livp"
    };
}

QStringList imageExtensions()
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

QStringList videoExtensions()
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

// iPhone HEIC / AVIF 等宽色域图像通常携带 Display P3 ICC profile。
// Qt 不会自动将像素值转换到显示器的 sRGB 色彩空间，直接渲染会导致色偏或灰化。
// 读取后若检测到非 sRGB 色彩空间，主动转换到 sRGB。
static QImage normalizeColorSpace(QImage img)
{
    if (img.isNull() || !img.colorSpace().isValid()) return img;
    const QColorSpace srgb(QColorSpace::SRgb);
    if (img.colorSpace() == srgb) return img;
    img.convertToColorSpace(srgb);
    return img;
}

// 线程安全版本：无成员访问、无日志，供 QtConcurrent::run 调用
QImage loadDisplayImageStatic(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();

    QImageReader autoReader(path);
    autoReader.setAutoTransform(true);
    const QImage autoImage = normalizeColorSpace(autoReader.read());

    QImageReader reader(path);
    if (ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif") {
        reader.setAutoTransform(false);
        const QImage raw = normalizeColorSpace(reader.read());
        if (raw.isNull()) return autoImage;

        const int orientation = jpegExifOrientation(path);
        const QImage manual = applyExifOrientation(raw, orientation);

        const bool hasOrientation = (orientation >= 1 && orientation <= 8);
        if (hasOrientation && orientation != 1 && !manual.isNull()) return manual;
        if (!autoImage.isNull()) return autoImage;
        return raw;
    }

    if (!autoImage.isNull()) return autoImage;

    reader.setAutoTransform(true);
    return normalizeColorSpace(reader.read());
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

QImage MainWindow::loadDisplayImage(const QString &path) const
{
    const QString ext = QFileInfo(path).suffix().toLower();

    QImageReader autoReader(path);
    autoReader.setAutoTransform(true);
    const QImage autoImage = normalizeColorSpace(autoReader.read());

    QImageReader reader(path);
    if (ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif") {
        reader.setAutoTransform(false);
        const QImage raw = normalizeColorSpace(reader.read());
        if (raw.isNull()) {
            logLine("loadDisplayImage jpeg raw read failed, fallback auto: " + path);
            return autoImage;
        }

        const int orientation = jpegExifOrientation(path);
        const QImage manual = applyExifOrientation(raw, orientation);

        const bool hasAuto = !autoImage.isNull();
        const bool hasOrientation = (orientation >= 1 && orientation <= 8);
        const bool rotate90Like = (orientation == 5 || orientation == 6 || orientation == 7 || orientation == 8);
        const bool autoChangedSize = hasAuto && (autoImage.size() != raw.size());

        logLine("loadDisplayImage jpeg inspect: " + path
            + ", orientation=" + QString::number(orientation)
            + ", raw=" + QString::number(raw.width()) + "x" + QString::number(raw.height())
            + ", auto=" + (hasAuto ? (QString::number(autoImage.width()) + "x" + QString::number(autoImage.height())) : QString("<none>"))
            + ", manual=" + QString::number(manual.width()) + "x" + QString::number(manual.height())
            + ", autoChangedSize=" + QString(autoChangedSize ? "true" : "false")
            + ", rotate90Like=" + QString(rotate90Like ? "true" : "false"));

        if (hasOrientation && orientation != 1 && !manual.isNull()) {
            logLine("loadDisplayImage jpeg use MANUAL orientation: " + path
                    + ", orientation=" + QString::number(orientation)
                    + ", raw=" + QString::number(raw.width()) + "x" + QString::number(raw.height())
                    + ", manual=" + QString::number(manual.width()) + "x" + QString::number(manual.height())
                    + ", auto=" + (hasAuto ? (QString::number(autoImage.width()) + "x" + QString::number(autoImage.height())) : QString("<none>")));
            return manual;
        }

        if (hasAuto) {
            logLine("loadDisplayImage jpeg use AUTO orientation: " + path
                    + ", orientation=" + QString::number(orientation)
                    + ", raw=" + QString::number(raw.width()) + "x" + QString::number(raw.height())
                    + ", auto=" + QString::number(autoImage.width()) + "x" + QString::number(autoImage.height())
                    + ", autoChangedSize=" + QString(autoChangedSize ? "true" : "false")
                    + ", rotate90Like=" + QString(rotate90Like ? "true" : "false"));
            return autoImage;
        }

        logLine("loadDisplayImage jpeg use RAW fallback: " + path
                + ", orientation=" + QString::number(orientation)
                + ", raw=" + QString::number(raw.width()) + "x" + QString::number(raw.height()));
        return raw;
    }

    if (!autoImage.isNull()) {
        return autoImage;
    }

    reader.setAutoTransform(true);
    return normalizeColorSpace(reader.read());
}


/**
 *  对于 HEIC/HEIF 格式的图片，Qt 的支持可能不太好，尤其是在 Windows 平台上。为了能够显示这些图片，可以尝试调用外部工具（如 ffmpeg 或 ImageMagick）将其转换为 PNG 格式的临时文件，然后再加载显示。
 *  转换后的临时文件会保存在系统的临时目录中，并且会有一个简单的缓存机制，避免对同一个 HEIC 文件重复转换。
 *  todo：这个函数可能会比较慢，因为涉及到外部进程的调用和文件的读写，所以在实际使用中可能需要放在后台线程中执行，以避免阻塞 UI 线程。
 */
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
        // -pix_fmt rgb24  强制输出 8-bit sRGB，避免 10-bit HEIC → 16-bit PNG 导致 Qt 显示发黑
        // -map_metadata 0 尽量将 ICC profile 传递到输出 PNG，供 normalizeColorSpace 使用
        const QStringList args = {
            "-y", "-hide_banner", "-loglevel", "error",
            "-i", sourcePath,
            "-pix_fmt", "rgb24",
            "-map_metadata", "0",
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

/**
 *  从 LIVP 文件中提取候选的静态图和视频.
 *  todo：
 *      1. 后面调整为后台线程处理 process (GUI 线程调用，livp 解压会导致界面拉顿)
 *      2. qHash(path) 可能存在碰撞风险，后续可以改为更安全的哈希算法或者在文件系统中使用目录结构分散缓存文件  后续改用 QCryptographicHash::Md5 或者 Sha1 来生成哈希值，碰撞概率更低
 *      3. Windows 平台依赖 PowerShell 的 Expand-Archive 命令来解压 LIVP（本质上是 ZIP 格式）。改用原生 zip库 QuaZIP
 *      4. 临时文件的管理，这里产生的临时文件包括：zip文件、解码后的文件。后续统一处理
 */
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
    clearAnimatedImage();
    m_previewStack->setCurrentWidget(m_imageArea);
    m_imageLabel->setText("无法识别该文件格式");
}

void MainWindow::cancelImageLoad()
{
    ++m_imageLoadSeq;
    if (m_imageLoadWatcher) {
        m_imageLoadWatcher->cancel();
        m_imageLoadWatcher->deleteLater();
        m_imageLoadWatcher = nullptr;
    }
}

// Phase 1：极速读取极低分辨率版本并放大，产生模糊占位图，立即显示
void MainWindow::showImagePlaceholder(const QString &path)
{
    QImageReader reader(path);
    const QSize orig = reader.size();
    if (!orig.isValid() || orig.isEmpty()) return;

    const QSize tiny = orig.scaled(16, 12, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    reader.setScaledSize(tiny);
    const QImage tinyImg = reader.read();
    if (tinyImg.isNull()) return;

    const QSize viewSize = m_imageArea ? m_imageArea->viewport()->size() : QSize();
    const QSize upSize = (viewSize.isValid() && !viewSize.isEmpty())
                             ? viewSize
                             : QSize(kThumbWidth * 4, kThumbHeight * 4);
    const QImage blurred = tinyImg.scaled(upSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_currentPixmap = QPixmap::fromImage(blurred);
    updateScaledImage();
    QTimer::singleShot(0, this, [this]() { centerImageInView(); });
}

void MainWindow::showImage(const ResolvedMedia &media)
{
    stopVideo();
    clearAnimatedImage();
    cancelImageLoad();

    m_seekSlider->setRange(0, 0);
    updateVideoTimeLabel(0, 0);
    m_imageScaleMode = ImageScaleMode::FitToWindow;
    m_zoomFactor = 1.0;

    const QString ext = QFileInfo(media.playablePath).suffix().toLower();
    if (ext == "gif") {
        auto *movie = new QMovie(media.playablePath, QByteArray(), this);
        if (movie->isValid()) {
            m_currentSourcePath = media.originalPath;
            m_currentColorProfileText = detectColorProfileText(media.playablePath);
            m_currentMovie = movie;
            m_currentPixmap = QPixmap();
            m_imageLabel->clear();
            m_imageLabel->setMovie(m_currentMovie);

            updateScaledImage();
            m_currentMovie->start();
            QTimer::singleShot(0, this, [this]() { centerImageInView(); });

            logLine("showImage gif animated playback: " + media.playablePath);
            m_previewStack->setCurrentWidget(m_imageArea);
            updateMediaInfoBar();
            statusBar()->showMessage("图片: " + media.originalPath, 3000);
            return;
        }
        movie->deleteLater();
        logLine("showImage gif fallback to static frame: " + media.playablePath);
    }

    m_currentSourcePath = media.originalPath;
    m_currentColorProfileText = QString();
    m_previewStack->setCurrentWidget(m_imageArea);
    statusBar()->showMessage("图片: " + media.originalPath, 3000);

    // Phase 1：模糊占位（同步，极速）
    showImagePlaceholder(media.playablePath);
    updateMediaInfoBar();

    // Phase 2：后台加载全分辨率原图
    ++m_imageLoadSeq;
    const quint64 seq = m_imageLoadSeq;
    const QString loadPath = media.playablePath;
    const QString origPath = media.originalPath;

    auto *watcher = new QFutureWatcher<QImage>(this);
    m_imageLoadWatcher = watcher;

    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, seq, loadPath, origPath]() {
        watcher->deleteLater();
        if (m_imageLoadWatcher == watcher) m_imageLoadWatcher = nullptr;
        if (seq != m_imageLoadSeq || m_currentSourcePath != origPath) return;

        const QImage img = watcher->result();
        const QColorSpace sRgbSpace(QColorSpace::SRgb);
        m_currentColorProfileText = img.colorSpace().isValid()
            ? ((img.colorSpace() == sRgbSpace) ? QString("sRGB") : QString("not-sRGB"))
            : detectColorProfileText(loadPath);

        if (img.isNull()) {
            logLine("showImage async failed: " + loadPath);
            m_currentPixmap = QPixmap();
            m_imageLabel->setText("图片读取失败: " + origPath);
        } else {
            logLine("showImage async success: " + loadPath);
            m_currentPixmap = QPixmap::fromImage(img);
            updateScaledImage();
            QTimer::singleShot(0, this, [this]() { centerImageInView(); });
        }
        updateMediaInfoBar();
    });

    watcher->setFuture(QtConcurrent::run([loadPath]() -> QImage {
        return loadDisplayImageStatic(loadPath);
    }));
}

void MainWindow::showVideo(const ResolvedMedia &media)
{
    clearAnimatedImage();
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

    m_currentSourcePath = media.originalPath;
    m_currentColorProfileText = "N/A";
    updateMediaInfoBar();
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

void MainWindow::clearAnimatedImage()
{
    if (m_currentMovie) {
        m_currentMovie->stop();
        m_imageLabel->setMovie(nullptr);
        m_currentMovie->deleteLater();
        m_currentMovie = nullptr;
    }
}

// 根据当前缩放模式和缩放因子计算并设置适合的预览图像显示尺寸，同时在更新后居中显示
void MainWindow::updateScaledImage()
{
    if (!m_imageArea) {
        return;
    }

    if (m_currentMovie && m_currentMovie->isValid()) {
        QSize sourceSize = m_currentMovie->currentImage().size();
        if (!sourceSize.isValid() || sourceSize.isEmpty()) {
            sourceSize = m_currentMovie->frameRect().size();
        }
        if (sourceSize.isValid() && !sourceSize.isEmpty()) {
            QSize targetSize;
            if (m_imageScaleMode == ImageScaleMode::FitToWindow) {
                m_imageArea->setWidgetResizable(true);
                const QSize viewportSize = m_imageArea->viewport()->size();
                targetSize = sourceSize.scaled(viewportSize, Qt::KeepAspectRatio);
            } else if (m_imageScaleMode == ImageScaleMode::ActualSize) {
                m_imageArea->setWidgetResizable(false);
                targetSize = sourceSize;
            } else {
                m_imageArea->setWidgetResizable(false);
                targetSize = QSize(
                    static_cast<int>(sourceSize.width() * m_zoomFactor),
                    static_cast<int>(sourceSize.height() * m_zoomFactor)
                );
            }
            m_currentMovie->setScaledSize(targetSize);
            m_imageLabel->resize(targetSize);
        }
        QTimer::singleShot(0, this, [this]() {
            centerImageInView();
        });
        return;
    }

    if (m_currentPixmap.isNull()) {
        return;
    }

    QPixmap scaled;
    if (m_imageScaleMode == ImageScaleMode::FitToWindow) {
        m_imageArea->setWidgetResizable(true);
        const QSize viewportSize = m_imageArea->viewport()->size();
        scaled = m_currentPixmap.scaled(
            viewportSize,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
    } else if (m_imageScaleMode == ImageScaleMode::ActualSize) {
        m_imageArea->setWidgetResizable(false);
        scaled = m_currentPixmap;
    } else {
        m_imageArea->setWidgetResizable(false);
        const QSize size(
            static_cast<int>(m_currentPixmap.width() * m_zoomFactor),
            static_cast<int>(m_currentPixmap.height() * m_zoomFactor)
        );
        scaled = m_currentPixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    m_imageLabel->setPixmap(scaled);
    if (m_imageScaleMode != ImageScaleMode::FitToWindow) {
        m_imageLabel->resize(scaled.size());
    }
    QTimer::singleShot(0, this, [this]() {
        centerImageInView();
    });
    updateMediaInfoBar();
}

void MainWindow::centerImageInView()
{
    const bool hasStatic = !m_currentPixmap.isNull();
    const bool hasAnimated = (m_currentMovie != nullptr);
    if (!m_imageArea || !m_imageLabel || (!hasStatic && !hasAnimated)) {
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

QString MainWindow::formatFileSize(qint64 sizeBytes) const
{
    if (sizeBytes < 0) {
        return "-";
    }

    static const QStringList units = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(sizeBytes);
    int unitIndex = 0;
    while (size >= 1024.0 && unitIndex < units.size() - 1) {
        size /= 1024.0;
        ++unitIndex;
    }
    return QLocale().toString(size, 'f', unitIndex == 0 ? 0 : 2) + " " + units.at(unitIndex);
}

QString MainWindow::detectColorProfileText(const QString &path) const
{
    if (path.isEmpty()) {
        return "未知";
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull() || !image.colorSpace().isValid()) {
        return "未知";
    }
    const QColorSpace sRgbSpace(QColorSpace::SRgb);
    return image.colorSpace() == sRgbSpace ? "sRGB" : "not-sRGB";
}

void MainWindow::updateMediaInfoBar()
{
    if (!m_mediaInfoLabel) {
        return;
    }

    if (m_files.isEmpty() || m_currentIndex < 0 || m_currentIndex >= m_files.size()) {
        const QString emptyText = "图片名称 | 0/0 个文件 | 100% | 0x0 | 0 B | 未知 | 文件信息";
        m_mediaInfoLabel->setText(emptyText);
        m_mediaInfoLabel->setToolTip(emptyText);
        return;
    }

    QFileInfo info(m_currentSourcePath.isEmpty() ? m_files.at(m_currentIndex) : m_currentSourcePath);
    const QString fileName = info.fileName();
    const QString indexText = QString("%1/%2 个文件").arg(m_currentIndex + 1).arg(m_files.size());

    QSize sourceSize;
    if (!m_currentPixmap.isNull()) {
        sourceSize = m_currentPixmap.size();
    } else if (m_currentMovie) {
        sourceSize = m_currentMovie->frameRect().size();
    }

    int zoomPercent = 100;
    if (m_imageScaleMode == ImageScaleMode::ManualZoom) {
        zoomPercent = qRound(m_zoomFactor * 100.0);
    } else if (m_imageScaleMode == ImageScaleMode::FitToWindow && sourceSize.isValid() && !sourceSize.isEmpty() && m_imageLabel) {
        const QSize shownSize = m_imageLabel->size();
        if (shownSize.isValid() && !shownSize.isEmpty()) {
            const double sx = static_cast<double>(shownSize.width()) / static_cast<double>(sourceSize.width());
            const double sy = static_cast<double>(shownSize.height()) / static_cast<double>(sourceSize.height());
            zoomPercent = qMax(1, qRound(qMin(sx, sy) * 100.0));
        }
    }

    const QString pixelText = sourceSize.isValid() && !sourceSize.isEmpty()
        ? QString("%1x%2").arg(sourceSize.width()).arg(sourceSize.height())
        : QString("-");
    const QString fileSizeText = formatFileSize(info.exists() ? info.size() : -1);
    const QString colorText = m_currentColorProfileText.isEmpty() ? QString("未知") : m_currentColorProfileText;
    const QString fileInfoText = QString("%1 | %2")
        .arg(info.suffix().toUpper())
        .arg(info.exists() ? info.lastModified().toString("yyyy-MM-dd HH:mm") : QString("未知时间"));

    QStringList segments = {
        fileName,
        indexText,
        QString::number(zoomPercent) + "%",
        pixelText,
        fileSizeText,
        colorText,
        fileInfoText
    };

    auto compose = [](const QStringList &parts) {
        return parts.join(" | ");
    };

    QString infoText = compose(segments);
    const QFontMetrics fm(m_mediaInfoLabel->font());
    const int availableWidth = qMax(120, m_mediaInfoLabel->width() - 12);

    if (fm.horizontalAdvance(infoText) > availableWidth) {
        QStringList compact = segments;
        compact[6] = "...";
        infoText = compose(compact);

        if (fm.horizontalAdvance(infoText) > availableWidth) {
            compact[5] = "...";
            infoText = compose(compact);
        }

        if (fm.horizontalAdvance(infoText) > availableWidth) {
            const int keepTail = qMin(5, compact.size());
            QStringList tail = compact.mid(compact.size() - keepTail, keepTail);
            const QString tailText = compose(tail);
            const int remaining = qMax(40, availableWidth - fm.horizontalAdvance(" | " + tailText));
            compact[0] = fm.elidedText(segments[0], Qt::ElideMiddle, remaining);
            infoText = compose(compact);
        }

        if (fm.horizontalAdvance(infoText) > availableWidth) {
            QStringList minimal = {segments[0], segments[1], segments[2], "..."};
            minimal[0] = fm.elidedText(segments[0], Qt::ElideMiddle, qMax(40, availableWidth / 2));
            infoText = compose(minimal);
        }
    }

    m_mediaInfoLabel->setText(infoText);
    m_mediaInfoLabel->setToolTip(compose(segments));
}
