#pragma once

#include <QMainWindow>
#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QPixmap>
#include <QSet>
#include <QTemporaryDir>

class QAction;      // 命令抽象
class QAudioOutput;     // 音频输出路由
class QImage;           // 图像数据对象
class QLabel;           // 文本/图片显示
class QListWidget;      // 列表显示
class QMediaPlayer;     // 媒体播放控制
class QMovie;           // 动图播放控制
class QIcon;            // 图标封装
class QSlider;          // 滑动条控件
class QScrollArea;      // 滚动区域容器
class QSplitter;        // 分割布局容器
class QStackedWidget;   // 页面切换容器
class QTimer;           // 定时器
class QCloseEvent;      // 关闭事件
class QThread;          // 线程
class QToolBar;
class QToolButton;      // 工具栏按钮
class QVideoWidget;     // 视频渲染窗口
class QWidget;          // 基础窗口组件

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
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

    enum class AlbumSortKey {
        FileName,
        Random,
        FileSize,
        Extension,
        CreatedDate,
        AccessedDate,
        ModifiedDate
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
    void updateMediaInfoBar();
    QString formatFileSize(qint64 sizeBytes) const;
    QString detectColorProfileText(const QString &path) const;
    void showPreviewContextMenu(const QPoint &globalPos);
    void applyAlbumSort(bool reshuffleRandom);
    QString albumSortLabel() const;

    void loadPaths(const QStringList &paths, bool clearExisting = true, const QString &preferredPath = QString());
    QStringList collectFilesFromFolder(const QString &folderPath) const;
    QStringList collectFilesFromSameFolder(const QString &filePath) const;

    bool isSupportedPath(const QString &path) const;
    bool isImageFile(const QString &path) const;
    bool isVideoFile(const QString &path) const;
    bool canDecodeImage(const QString &path) const;
    QImage loadDisplayImage(const QString &path) const;
    QString transcodeHeicToDisplayImage(const QString &sourcePath);

    ResolvedMedia resolveMedia(const QString &path);
    ResolvedMedia resolveLivp(const QString &path);
    bool extractLivpCandidates(const QString &path, QString &candidateImage, QString &candidateVideo);

    void showCurrent();
    void showMedia(const ResolvedMedia &media);
    void showImage(const ResolvedMedia &media);
    void showVideo(const ResolvedMedia &media);
    void stopVideo();
    void clearAnimatedImage();
    void showImagePlaceholder(const QString &path);
    void cancelImageLoad();
    void updateScaledImage();
    void centerImageInView();
    void applyZoom(double ratio);
    QString formatMs(qint64 ms) const;
    void updateVideoTimeLabel(qint64 positionMs, qint64 durationMs);
    void seekByMs(qint64 deltaMs);
    QIcon buildListIcon(const QString &path) const;
    QIcon buildAlbumIcon(const QString &path);
    QIcon buildLqipIcon(const QString &path);
    void cancelAllAlbumThumbWatchers();
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
    QLabel *m_mediaInfoLabel = nullptr;

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
    QAction *m_toggleAlbumAction = nullptr;
    QAction *m_toggleToolbarAction = nullptr;

    QToolBar *m_mainToolBar = nullptr;

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QTimer *m_videoStartWatchdog = nullptr;
    QMovie *m_currentMovie = nullptr;

    QStringList m_files;
    int m_currentIndex = -1;
    bool m_isSeeking = false;
    bool m_livpPreferVideo = true;
    bool m_waitingVideoStart = false;
    bool m_isImagePanning = false;
    QPoint m_lastImagePanPos;
    QString m_pendingFallbackImagePath;
    QString m_pendingFallbackOriginalPath;
    ImageScaleMode m_imageScaleMode = ImageScaleMode::FitToWindow;
    double m_zoomFactor = 1.0;

    QPixmap m_currentPixmap;
    QString m_currentSourcePath;
    QString m_currentColorProfileText;
    QTemporaryDir m_tempDir;        // 临时目录，各个系统位置不同，win:C:\Users\Administrator\AppData\Local\Temp\{random}  linux:/tmp/{random}
    QString m_logFilePath;
    QHash<QString, QIcon> m_albumIconCache;
    QHash<QString, QString> m_heicTranscodeCache;
    QList<HistoryEntry> m_historyEntries;
    bool m_updatingHistoryList = false;
    QSet<int> m_historyCollapsedGroups;
    QList<int> m_albumIconQueue;        //  待生成图标的行号队列，优先级由位置决定，靠近当前选择的行优先
    QSet<int> m_albumIconQueuedSet;     // 已经在队列中的行号集合，用于快速去重
    bool m_albumIconPumpScheduled = false;  // 是否已经安排了 pumpAlbumIconQueue 的调用，避免重复安排
    QString m_albumThumbCacheDir;
    bool m_showAlbumPanel = true;
    AlbumSortKey m_albumSortKey = AlbumSortKey::FileName;
    Qt::SortOrder m_albumSortOrder = Qt::AscendingOrder;

    // 异步图片加载
    QFutureWatcher<QImage> *m_imageLoadWatcher = nullptr;
    quint64 m_imageLoadSeq = 0;

    // 异步相册缩略图生成（key = 行号）
    QHash<int, QFutureWatcher<QIcon> *> m_albumThumbWatchers;
    int m_albumThumbRunning = 0;                    // 当前正在执行的缩略图异步任务数
    static constexpr int kMaxAlbumThumbConcurrent = 3; // 最大并发缩略图任务数

    // 退出时的临时文件清理线程
    QThread *m_cleanupThread = nullptr;
};
