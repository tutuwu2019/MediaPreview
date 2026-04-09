#pragma once

#include <QMainWindow>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QSet>
#include <QTemporaryDir>

class QAction;      // 命令抽象
class QAudioOutput;     // 音频输出路由
class QLabel;           // 文本/图片显示
class QListWidget;      // 列表显示
class QMediaPlayer;     // 媒体播放控制
class QIcon;            // 图标封装
class QSlider;          // 滑动条控件
class QScrollArea;      // 滚动区域容器
class QSplitter;        // 分割布局容器
class QStackedWidget;   // 页面切换容器
class QTimer;           // 定时器
class QToolButton;      // 工具栏按钮
class QVideoWidget;     // 视频渲染窗口
class QWidget;          // 基础窗口组件

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void openFiles();
    void showPrev();
    void showNext();
    void togglePlayPause();
    void onListSelectionChanged();
    void onAlbumSelectionChanged();
    void onPlayerPositionChanged(qint64 position);
    void onPlayerDurationChanged(qint64 duration);
    void onSeekSliderPressed();
    void onSeekSliderReleased();
    void onVolumeChanged(int value);
    void setFitToWindowMode();
    void setActualSizeMode();
    void zoomInImage();
    void zoomOutImage();

private:
    static constexpr int kThumbWidth = 96;
    static constexpr int kThumbHeight = 72;

    enum class ImageScaleMode {
        FitToWindow,
        ActualSize,
        ManualZoom
    };

    enum class MediaType {
        Unknown,
        Image,
        Video
    };

    struct ResolvedMedia {
        MediaType type = MediaType::Unknown;
        QString originalPath;
        QString playablePath;
        QString fallbackImagePath;
    };

    struct HistoryEntry {
        QString path;
        QDateTime timestamp;
    };

    void setupUi();
    void setupConnections();
    void setupLogging();
    void logLine(const QString &line) const;

    void loadPaths(const QStringList &paths, bool clearExisting = true, const QString &preferredPath = QString());
    QStringList collectFilesFromFolder(const QString &folderPath) const;
    QStringList collectFilesFromSameFolder(const QString &filePath) const;

    bool isSupportedPath(const QString &path) const;
    bool isImageFile(const QString &path) const;
    bool isVideoFile(const QString &path) const;
    bool canDecodeImage(const QString &path) const;
    QString transcodeHeicToDisplayImage(const QString &sourcePath);

    ResolvedMedia resolveMedia(const QString &path);
    ResolvedMedia resolveLivp(const QString &path);
    bool extractLivpCandidates(const QString &path, QString &candidateImage, QString &candidateVideo);

    void showCurrent();
    void showMedia(const ResolvedMedia &media);
    void showImage(const ResolvedMedia &media);
    void showVideo(const ResolvedMedia &media);
    void stopVideo();
    void updateScaledImage();
    void centerImageInView();
    void applyZoom(double ratio);
    QString formatMs(qint64 ms) const;
    void updateVideoTimeLabel(qint64 positionMs, qint64 durationMs);
    void seekByMs(qint64 deltaMs);
    QIcon buildListIcon(const QString &path) const;
    QIcon buildAlbumIcon(const QString &path);
    void centerAlbumSelection(int row);
    void appendHistoryEntry(const QString &path);
    void rebuildHistoryList();
    void applyHistoryGroupCollapse();
    void ensureAlbumIcon(int row);
    void ensureAlbumIconsNear(int centerRow, int radius = 2);
    void queueAlbumIcon(int row, bool highPriority = false);
    void rebuildAlbumQueueAround(int centerRow, int radius = 2);
    void queueVisibleAlbumIcons(int overscan = 2);
    void pumpAlbumIconQueue();
    QPair<int, int> albumVisibleRange(int overscan = 2) const;
    QString albumThumbCacheFilePath(const QString &path);
    bool loadAlbumIconFromDiskCache(const QString &path, QIcon &icon);
    void saveAlbumIconToDiskCache(const QString &path, const QIcon &icon);
    QPixmap extractVideoFirstFrame(const QString &videoPath) const;

    QString psQuote(const QString &text) const;

private:
    QListWidget *m_listWidget = nullptr;
    QListWidget *m_albumListWidget = nullptr;
    QStackedWidget *m_previewStack = nullptr;
    QScrollArea *m_imageArea = nullptr;
    QLabel *m_imageLabel = nullptr;
    QVideoWidget *m_videoWidget = nullptr;
    QWidget *m_videoPanel = nullptr;
    QSlider *m_seekSlider = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QToolButton *m_playPauseButton = nullptr;
    QToolButton *m_prevOverlayButton = nullptr;
    QToolButton *m_nextOverlayButton = nullptr;
    QSplitter *m_mainSplitter = nullptr;

    QAction *m_openFilesAction = nullptr;
    QAction *m_prevAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_playPauseAction = nullptr;
    QAction *m_toggleListAction = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_actualSizeAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_livpPreferVideoAction = nullptr;

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QTimer *m_videoStartWatchdog = nullptr;

    QStringList m_files;
    int m_currentIndex = -1;
    bool m_isSeeking = false;
    bool m_livpPreferVideo = true;
    bool m_waitingVideoStart = false;
    QString m_pendingFallbackImagePath;
    QString m_pendingFallbackOriginalPath;
    ImageScaleMode m_imageScaleMode = ImageScaleMode::FitToWindow;
    double m_zoomFactor = 1.0;

    QPixmap m_currentPixmap;
    QTemporaryDir m_tempDir;
    QString m_logFilePath;
    QHash<QString, QIcon> m_albumIconCache;
    QHash<QString, QString> m_heicTranscodeCache;
    QList<HistoryEntry> m_historyEntries;
    bool m_updatingHistoryList = false;
    QSet<int> m_historyCollapsedGroups;
    QList<int> m_albumIconQueue;
    QSet<int> m_albumIconQueuedSet;
    bool m_albumIconPumpScheduled = false;
    QString m_albumThumbCacheDir;
};
