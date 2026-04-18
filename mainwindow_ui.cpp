#include "mainwindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QAudioOutput>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QMenuBar>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QFrame>
#include <QSizePolicy>

/**
 * UI setup and signal-slot connections are organized in this file to keep mainwindow.cpp focused on core logic and media handling.
 */
void MainWindow::setupUi()
{
    // 工具栏和菜单
    m_mainToolBar = addToolBar("Main");
    m_mainToolBar->setMovable(false);

    m_openFilesAction = m_mainToolBar->addAction("打开文件");
    m_toggleListAction = m_mainToolBar->addAction("显示浏览记录");
    m_toggleListAction->setCheckable(true);
    m_toggleListAction->setChecked(false);
    m_mainToolBar->addSeparator();

    m_prevAction = m_mainToolBar->addAction("上一项");
    m_nextAction = m_mainToolBar->addAction("下一项");
    m_playPauseAction = m_mainToolBar->addAction("播放/暂停");
    m_mainToolBar->addSeparator();

    m_fitAction = m_mainToolBar->addAction("适应窗口");
    m_actualSizeAction = m_mainToolBar->addAction("原始尺寸");
    m_zoomInAction = m_mainToolBar->addAction("放大");
    m_zoomOutAction = m_mainToolBar->addAction("缩小");
    m_mainToolBar->addSeparator();

    m_livpPreferVideoAction = m_mainToolBar->addAction("动态图优先视频");
    m_livpPreferVideoAction->setCheckable(true);
    m_livpPreferVideoAction->setChecked(m_livpPreferVideo);

    m_toggleAlbumAction = m_mainToolBar->addAction("显示相册画板");
    m_toggleAlbumAction->setCheckable(true);
    m_toggleAlbumAction->setChecked(true);
    m_toggleAlbumAction->setText("隐藏相册画板");

    auto *fileMenu = menuBar()->addMenu("菜单栏");
    fileMenu->addAction(m_openFilesAction);

    // 中央区域：顶部信息栏 + 下方主分割器
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(6, 4, 6, 6);
    centralLayout->setSpacing(6);

    auto *infoFrame = new QFrame(central);
    infoFrame->setObjectName("mediaInfoFrame");
    infoFrame->setStyleSheet(
        "QFrame#mediaInfoFrame {"
        "  background: rgba(240, 247, 255, 235);"
        "  border: 1px solid #D5E4FA;"
        "  border-radius: 6px;"
        "}"
    );
    auto *infoLayout = new QHBoxLayout(infoFrame);
    infoLayout->setContentsMargins(8, 3, 8, 3);
    infoLayout->setSpacing(4);

    m_mediaInfoLabel = new QLabel("图片名称 | 0/0 个文件 | 100% | 0x0 | 0 B | 未知 | 文件信息", infoFrame);
    m_mediaInfoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_mediaInfoLabel->setMinimumWidth(0);
    m_mediaInfoLabel->setStyleSheet(
        "QLabel {"
        "  color: #3A4B61;"
        "  font-size: 12px;"
        "}"
    );
    infoLayout->addWidget(m_mediaInfoLabel);
    centralLayout->addWidget(infoFrame);

    m_mainSplitter = new QSplitter(central);
    centralLayout->addWidget(m_mainSplitter, 1);
    setCentralWidget(central);

    // 主预览区，包含图片和视频两种显示模式
    // 左侧文件列表，初始隐藏
    m_listWidget = new QListWidget(m_mainSplitter);
    m_listWidget->setMinimumWidth(300);
    m_listWidget->setIconSize(QSize(kThumbWidth, kThumbHeight));
    m_listWidget->setUniformItemSizes(true);
    m_listWidget->setVisible(false);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listWidget->setMouseTracking(true);
    m_listWidget->setSpacing(4);
    m_listWidget->setStyleSheet(
        "QListWidget {"
        "  background: #F7FAFF;"
        "  border: 1px solid #DDEAFE;"
        "  border-radius: 8px;"
        "  padding: 6px;"
        "  outline: none;"
        "}"
        "QListWidget::item {"
        "  border: 1px solid transparent;"
        "  border-radius: 8px;"
        "  padding: 6px 8px;"
        "  margin: 1px 0;"
        "  color: #1E2A3A;"
        "}"
        "QListWidget::item:hover {"
        "  border: 1px solid #B9D7FF;"
        "  background: rgba(233, 243, 255, 220);"
        "}"
        "QListWidget::item:selected {"
        "  border: 1px solid #2F80ED;"
        "  background: rgba(47, 128, 237, 32);"
        "  color: #123A72;"
        "}"
    );


    // 右侧预览和底部相册画板
    auto *rightPanelSplitter = new QSplitter(Qt::Vertical, m_mainSplitter);

    auto *previewNavContainer = new QWidget(rightPanelSplitter);
    auto *previewNavLayout = new QHBoxLayout(previewNavContainer);
    previewNavLayout->setContentsMargins(4, 4, 4, 4);
    previewNavLayout->setSpacing(6);

    // LIVP格式可能包含视频和图片两种资源，这里预留两个按钮用于切换显示（如果两者都存在的话）
    m_prevOverlayButton = new QToolButton(previewNavContainer);
    m_prevOverlayButton->setText("<");
    m_prevOverlayButton->setToolTip("上一项");
    m_prevOverlayButton->setFixedWidth(34);
    m_prevOverlayButton->setCursor(Qt::PointingHandCursor);
    m_prevOverlayButton->setStyleSheet(
        "QToolButton {"
        "  min-height: 34px;"
        "  border: 1px solid #BFD8FF;"
        "  border-radius: 17px;"
        "  background: rgba(255, 255, 255, 230);"
        "  color: #2F80ED;"
        "  font-weight: 600;"
        "}"
        "QToolButton:hover {"
        "  background: rgba(235, 244, 255, 245);"
        "  border-color: #9FC6FF;"
        "}"
        "QToolButton:pressed {"
        "  background: rgba(220, 236, 255, 245);"
        "}"
    );
    previewNavLayout->addWidget(m_prevOverlayButton);

    m_previewStack = new QStackedWidget(previewNavContainer);
    previewNavLayout->addWidget(m_previewStack, 1);

    m_nextOverlayButton = new QToolButton(previewNavContainer);
    m_nextOverlayButton->setText(">");
    m_nextOverlayButton->setToolTip("下一项");
    m_nextOverlayButton->setFixedWidth(34);
    m_nextOverlayButton->setCursor(Qt::PointingHandCursor);
    m_nextOverlayButton->setStyleSheet(
        "QToolButton {"
        "  min-height: 34px;"
        "  border: 1px solid #BFD8FF;"
        "  border-radius: 17px;"
        "  background: rgba(255, 255, 255, 230);"
        "  color: #2F80ED;"
        "  font-weight: 600;"
        "}"
        "QToolButton:hover {"
        "  background: rgba(235, 244, 255, 245);"
        "  border-color: #9FC6FF;"
        "}"
        "QToolButton:pressed {"
        "  background: rgba(220, 236, 255, 245);"
        "}"
    );
    previewNavLayout->addWidget(m_nextOverlayButton);

    // 图片显示区域，支持缩放和平移
    m_imageLabel = new QLabel;
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setText("请选择图片或视频文件");
    m_imageLabel->setMinimumSize(400, 300);
    m_imageLabel->installEventFilter(this);

    // 视频显示区域，包含视频窗口和控制面板
    m_imageArea = new QScrollArea;
    m_imageArea->setWidgetResizable(true);
    m_imageArea->setAlignment(Qt::AlignCenter);
    m_imageArea->setWidget(m_imageLabel);
    m_imageArea->viewport()->installEventFilter(this);

    // 视频面板在下面单独构建，包含视频窗口和控制按钮
    m_videoPanel = new QWidget;
    auto *videoLayout = new QVBoxLayout(m_videoPanel);
    videoLayout->setContentsMargins(8, 8, 8, 8);
    videoLayout->setSpacing(8);

    m_videoWidget = new QVideoWidget;
    videoLayout->addWidget(m_videoWidget, 1);
    m_videoWidget->installEventFilter(this);

    // 视频控制按钮布局，包括播放/暂停、进度条、时间显示和音量控制
    auto *controlsLayout = new QHBoxLayout;
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(8);

    m_playPauseButton = new QToolButton;
    m_playPauseButton->setText("播放");
    m_playPauseButton->setCursor(Qt::PointingHandCursor);
    m_playPauseButton->setStyleSheet(
        "QToolButton {"
        "  border: 1px solid #BFD8FF;"
        "  border-radius: 8px;"
        "  padding: 4px 12px;"
        "  background: rgba(255, 255, 255, 230);"
        "  color: #2F80ED;"
        "  font-weight: 600;"
        "}"
        "QToolButton:hover {"
        "  background: rgba(235, 244, 255, 245);"
        "  border-color: #9FC6FF;"
        "}"
        "QToolButton:pressed {"
        "  background: rgba(220, 236, 255, 245);"
        "}"
    );
    controlsLayout->addWidget(m_playPauseButton);


    //  进度条和时间显示
    //  进度条使用自定义样式，提升视觉效果，其中已经将 handle 的 hover 和 pressed 状态也做了区分
    //  时间显示在进度条右侧，显示格式为 "当前时间 / 总时间"，例如 "01:23 / 04:56"
    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, 0);
    m_seekSlider->setFixedHeight(22);
    m_seekSlider->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  height: 6px;"
        "  border-radius: 3px;"
        "  background: #DDEAFE;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  height: 6px;"
        "  border-radius: 3px;"
        "  background: #2F80ED;"
        "}"
        "QSlider::add-page:horizontal {"
        "  height: 6px;"
        "  border-radius: 3px;"
        "  background: #DDEAFE;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 14px;"
        "  height: 14px;"
        "  margin: -4px 0;"
        "  border-radius: 7px;"
        "  background: #FFFFFF;"
        "  border: 2px solid #2F80ED;"
        "}"
        "QSlider::handle:horizontal:hover {"
        "  background: #F7FAFF;"
        "}"
        "QSlider::handle:horizontal:pressed {"
        "  background: #EAF2FF;"
        "}"
    );
    controlsLayout->addWidget(m_seekSlider, 1);

    m_timeLabel = new QLabel("00:00 / 00:00");
    controlsLayout->addWidget(m_timeLabel);

    auto *volumeText = new QLabel("音量");
    controlsLayout->addWidget(volumeText);

    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedWidth(120);
    m_volumeSlider->setFixedHeight(22);
    m_volumeSlider->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  height: 6px;"
        "  border-radius: 3px;"
        "  background: #DDEAFE;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  height: 6px;"
        "  border-radius: 3px;"
        "  background: #2F80ED;"
        "}"
        "QSlider::add-page:horizontal {"
        "  height: 6px;"
        "  border-radius: 3px;"
        "  background: #DDEAFE;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 14px;"
        "  height: 14px;"
        "  margin: -4px 0;"
        "  border-radius: 7px;"
        "  background: #FFFFFF;"
        "  border: 2px solid #2F80ED;"
        "}"
        "QSlider::handle:horizontal:hover {"
        "  background: #F7FAFF;"
        "}"
        "QSlider::handle:horizontal:pressed {"
        "  background: #EAF2FF;"
        "}"
    );
    controlsLayout->addWidget(m_volumeSlider);

    videoLayout->addLayout(controlsLayout);

    m_previewStack->addWidget(m_imageArea);
    m_previewStack->addWidget(m_videoPanel);

    // 底部相册画板，显示当前文件所在文件夹的其他媒体文件缩略图
    m_albumListWidget = new QListWidget(rightPanelSplitter);        // 相册画板
    m_albumListWidget->setViewMode(QListView::IconMode);            //  图标模式显示
    m_albumListWidget->setUniformItemSizes(true);                    //  所有项使用相同大小，提升性能
    m_albumListWidget->setFlow(QListView::LeftToRight);             //  从左到右排列
    m_albumListWidget->setResizeMode(QListView::Fixed);             //  固定模式，避免缩略图回填时重排导致滚动位置跳回起点
    m_albumListWidget->setMovement(QListView::Static);              //  静态布局，不允许用户拖动调整
    m_albumListWidget->setWrapping(false);                          //  不换行，超出部分显示滚动条
    m_albumListWidget->setSpacing(6);                   //  项间距
    m_albumListWidget->setIconSize(QSize(kThumbWidth, kThumbHeight));          //  图标大小
    m_albumListWidget->setMaximumHeight(150);                       //  最大高度，超过部分显示滚动条
    m_albumListWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);  //  水平滚动模式，按像素滚动
    m_albumListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);      //  始终隐藏垂直滚动条
    m_albumListWidget->setSelectionMode(QAbstractItemView::SingleSelection);    //  单选模式    
    m_albumListWidget->setMouseTracking(true);
    m_albumListWidget->viewport()->setMouseTracking(true);
    m_albumListWidget->setStyleSheet(
        "QListWidget::item {"
        "  border: 1px solid transparent;"
        "  border-radius: 6px;"
        "  padding: 2px;"
        "}"
        "QListWidget::item:hover {"
        "  border: 1px solid #4D97F3;"
        "  background: rgba(77, 151, 243, 26);"
        "}"
        "QListWidget::item:selected {"
        "  border: 1px solid #2F80ED;"
        "  background: rgba(47, 128, 237, 38);"
        "}"
    );

    m_mainSplitter->setStretchFactor(0, 0);       //  历史浏览列表固定宽度
    m_mainSplitter->setStretchFactor(1, 1);       //  预览区占满剩余空间
    m_mainSplitter->setSizes({0, 1100});          //  初始状态预览区占满，历史列表隐藏
    rightPanelSplitter->setStretchFactor(0, 1);     //  预览区占满剩余空间
    rightPanelSplitter->setStretchFactor(1, 0);     //  相册画板固定高度
    rightPanelSplitter->setSizes({580, 150});       //  初始状态预览区占满，相册画板显示

    // 媒体播放器和相关组件初始化
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoWidget);
    m_audioOutput->setVolume(0.8f);

    // 视频启动监视器，防止某些视频文件无法正常播放时界面一直卡在黑屏状态
    m_videoStartWatchdog = new QTimer(this);        
    m_videoStartWatchdog->setSingleShot(true);      //  视频启动监视器，防止某些视频文件无法正常播放时界面一直卡在黑屏状态
    m_videoStartWatchdog->setInterval(1800);        // 1.8秒内视频未成功开始播放则自动回退到静态图显示（如果有的话）


    connect(m_videoStartWatchdog, &QTimer::timeout, this, [this]() {
        if (!m_waitingVideoStart || m_pendingFallbackImagePath.isEmpty()) {
            return;
        }
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            return;
        }

        const QString fallbackImage = m_pendingFallbackImagePath;
        const QString originalPath = m_pendingFallbackOriginalPath;
        m_pendingFallbackImagePath.clear();
        m_pendingFallbackOriginalPath.clear();
        m_waitingVideoStart = false;

        logLine("watchdog fallback to image (video start timeout): " + fallbackImage);
        ResolvedMedia fallback;
        fallback.type = MediaType::Image;
        fallback.originalPath = originalPath;
        fallback.playablePath = fallbackImage;
        showImage(fallback);
        statusBar()->showMessage("视频启动超时，已回退静态图", 4000);
    });

    updateMediaInfoBar();
    statusBar()->showMessage("就绪");
}

void MainWindow::setupConnections()
{
    connect(m_openFilesAction, &QAction::triggered, this, &MainWindow::openFiles);
    connect(m_toggleListAction, &QAction::toggled, this,
            [this](bool checked) {
                m_listWidget->setVisible(checked);
                m_toggleListAction->setText(checked ? "隐藏浏览记录" : "显示浏览记录");
                if (!m_mainSplitter) {
                    return;
                }
                if (checked) {
                    const int total = qMax(1, m_mainSplitter->size().width());
                    const int historyWidth = qBound(260, total / 4, 420);
                    m_mainSplitter->setSizes({historyWidth, qMax(1, total - historyWidth)});
                } else {
                    const int total = qMax(1, m_mainSplitter->size().width());
                    m_mainSplitter->setSizes({0, total});
                }
            });
    connect(m_prevAction, &QAction::triggered, this, &MainWindow::showPrev);
    connect(m_nextAction, &QAction::triggered, this, &MainWindow::showNext);
    connect(m_playPauseAction, &QAction::triggered, this, &MainWindow::togglePlayPause);
    connect(m_fitAction, &QAction::triggered, this, &MainWindow::setFitToWindowMode);
    connect(m_actualSizeAction, &QAction::triggered, this, &MainWindow::setActualSizeMode);
    connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::zoomInImage);
    connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOutImage);
    connect(m_toggleAlbumAction, &QAction::toggled, this, [this](bool checked) {
        m_showAlbumPanel = checked;
        if (m_albumListWidget) {
            m_albumListWidget->setVisible(checked);
        }
        m_toggleAlbumAction->setText(checked ? "隐藏相册画板" : "显示相册画板");
    });
    connect(m_livpPreferVideoAction, &QAction::toggled, this,
            [this](bool checked) {
                m_livpPreferVideo = checked;
                logLine("LIVP prefer video toggled=" + QString(checked ? "true" : "false"));
                statusBar()->showMessage(checked ? "LIVP 模式: 优先视频" : "LIVP 模式: 优先静态图", 2500);

                // 清空相册图标缓存，强制刷新所有列表项图标以反映新的 LIVP 解析策略
                m_albumIconCache.clear();
                m_albumIconQueue.clear();
                m_albumIconQueuedSet.clear();
                m_albumIconPumpScheduled = false;

                const int itemCount = qMin(m_files.size(), m_albumListWidget ? m_albumListWidget->count() : 0);
                for (int i = 0; i < itemCount; ++i) {
                    const QIcon icon = buildListIcon(m_files.at(i));
                    if (m_albumListWidget) {
                        if (QListWidgetItem *albumItem = m_albumListWidget->item(i)) {
                            albumItem->setIcon(icon);
                        }
                    }
                }

                if (m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
                    showCurrent();
                    QTimer::singleShot(0, this, [this]() {
                        ensureAlbumIconsNear(m_currentIndex);
                        queueVisibleAlbumIcons();
                    });
                }
            });
    connect(m_prevOverlayButton, &QToolButton::clicked, this, &MainWindow::showPrev);
    connect(m_nextOverlayButton, &QToolButton::clicked, this, &MainWindow::showNext);
    connect(m_playPauseButton, &QToolButton::clicked, this, &MainWindow::togglePlayPause);

    connect(m_listWidget, &QListWidget::currentRowChanged, this, &MainWindow::onListSelectionChanged);
    connect(m_albumListWidget, &QListWidget::currentRowChanged, this, &MainWindow::onAlbumSelectionChanged);
    if (m_albumListWidget && m_albumListWidget->horizontalScrollBar()) {
        connect(m_albumListWidget->horizontalScrollBar(), &QScrollBar::valueChanged, this,
                [this](int) {
                    queueVisibleAlbumIcons();
                });
    }
    connect(m_seekSlider, &QSlider::sliderPressed, this, &MainWindow::onSeekSliderPressed);     
    connect(m_seekSlider, &QSlider::sliderReleased, this, &MainWindow::onSeekSliderReleased);   
    connect(m_volumeSlider, &QSlider::valueChanged, this, &MainWindow::onVolumeChanged);
    connect(m_player, &QMediaPlayer::positionChanged, this, &MainWindow::onPlayerPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MainWindow::onPlayerDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                logLine("player playbackStateChanged=" + QString::number(static_cast<int>(state)));
                // 状态收敛
                if (state == QMediaPlayer::PlayingState) {
                    m_waitingVideoStart = false;
                    if (m_videoStartWatchdog) {
                        m_videoStartWatchdog->stop();
                    }
                }
                if (state == QMediaPlayer::PlayingState) {
                    m_playPauseButton->setText("暂停");
                } else {
                    m_playPauseButton->setText("播放");
                }
            });

    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &errorString) {
                logLine("player errorOccurred: " + errorString);
                m_waitingVideoStart = false;
                if (m_videoStartWatchdog) {
                    m_videoStartWatchdog->stop();
                }
                if (!m_pendingFallbackImagePath.isEmpty()) {
                    const QString fallbackImage = m_pendingFallbackImagePath;
                    const QString originalPath = m_pendingFallbackOriginalPath;
                    m_pendingFallbackImagePath.clear();
                    m_pendingFallbackOriginalPath.clear();

                    logLine("player error fallback to image: " + fallbackImage);
                    ResolvedMedia fallback;
                    fallback.type = MediaType::Image;
                    fallback.originalPath = originalPath;
                    fallback.playablePath = fallbackImage;
                    showImage(fallback);
                    statusBar()->showMessage("视频播放失败，已回退静态图", 4000);
                    return;
                }
                statusBar()->showMessage("视频播放错误: " + errorString, 5000);
            });
}
