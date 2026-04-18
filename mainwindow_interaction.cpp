#include "mainwindow.h"

#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QDir>
#include <QDirIterator>
#include <QKeyEvent>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QAudioOutput>
#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QModelIndex>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QUrl>
#include <QWheelEvent>
#include <QVideoWidget>
#include <QMenu>
#include <QActionGroup>
#include <QRandomGenerator>
#include <algorithm>

namespace {
constexpr int kHistoryRolePath = Qt::UserRole + 1;
constexpr int kHistoryRoleIsHeader = Qt::UserRole + 2;
constexpr int kHistoryRoleGroupId = Qt::UserRole + 3;
constexpr int kHistoryGroupSize = 50;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_videoWidget && watched == static_cast<QObject *>(m_videoWidget) && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            showPreviewContextMenu(mouseEvent->globalPosition().toPoint());
            return true;
        }
    }

    if (m_imageArea && (watched == m_imageArea->viewport() || watched == static_cast<QObject *>(m_imageLabel))) {
        const bool inImageMode = (m_previewStack && m_previewStack->currentWidget() == m_imageArea);
        const bool hasImage = !m_currentPixmap.isNull() || (m_currentMovie != nullptr);

        if (!inImageMode || !hasImage) {
            return QMainWindow::eventFilter(watched, event);
        }

        if (event->type() == QEvent::Wheel) {
            auto *wheelEvent = static_cast<QWheelEvent *>(event);
            if (wheelEvent->angleDelta().y() > 0) {
                zoomInImage();
            } else if (wheelEvent->angleDelta().y() < 0) {
                zoomOutImage();
            }
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::RightButton) {
                showPreviewContextMenu(mouseEvent->globalPosition().toPoint());
                return true;
            }
            if (mouseEvent->button() == Qt::LeftButton) {
                QScrollBar *hBar = m_imageArea->horizontalScrollBar();
                QScrollBar *vBar = m_imageArea->verticalScrollBar();
                const bool canPan = hBar && vBar
                    && (hBar->maximum() > hBar->minimum() || vBar->maximum() > vBar->minimum());
                if (canPan) {
                    m_isImagePanning = true;
                    m_lastImagePanPos = mouseEvent->globalPosition().toPoint();
                    m_imageArea->viewport()->setCursor(Qt::ClosedHandCursor);
                    m_imageArea->viewport()->grabMouse();
                    if (m_imageLabel) {
                        m_imageLabel->setCursor(Qt::ClosedHandCursor);
                    }
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (m_isImagePanning) {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (!(mouseEvent->buttons() & Qt::LeftButton)) {
                    m_isImagePanning = false;
                    m_imageArea->viewport()->unsetCursor();
                    m_imageArea->viewport()->releaseMouse();
                    if (m_imageLabel) {
                        m_imageLabel->unsetCursor();
                    }
                    return true;
                }
                QScrollBar *hBar = m_imageArea->horizontalScrollBar();
                QScrollBar *vBar = m_imageArea->verticalScrollBar();
                if (hBar && vBar) {
                    const QPoint now = mouseEvent->globalPosition().toPoint();
                    const QPoint delta = now - m_lastImagePanPos;
                    hBar->setValue(hBar->value() - delta.x());
                    vBar->setValue(vBar->value() - delta.y());
                    m_lastImagePanPos = now;
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_isImagePanning) {
                m_isImagePanning = false;
                m_imageArea->viewport()->unsetCursor();
                m_imageArea->viewport()->releaseMouse();
                if (m_imageLabel) {
                    m_imageLabel->unsetCursor();
                }
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateScaledImage();
    queueVisibleAlbumIcons();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (!event) {
        return;
    }

    const bool inVideoMode = (m_previewStack && m_previewStack->currentWidget() == m_videoPanel);

    switch (event->key()) {
    case Qt::Key_Space:
        togglePlayPause();
        event->accept();
        return;
    case Qt::Key_Left:
        if (inVideoMode) {
            seekByMs(-5000);
        } else {
            showPrev();
        }
        event->accept();
        return;
    case Qt::Key_Right:
        if (inVideoMode) {
            seekByMs(5000);
        } else {
            showNext();
        }
        event->accept();
        return;
    case Qt::Key_Up:
        if (inVideoMode && m_volumeSlider) {
            const int next = qMin(100, m_volumeSlider->value() + 5);
            m_volumeSlider->setValue(next);
            statusBar()->showMessage(QString("音量: %1%").arg(next), 1200);
        } else {
            showPrev();
        }
        event->accept();
        return;
    case Qt::Key_Down:
        if (inVideoMode && m_volumeSlider) {
            const int next = qMax(0, m_volumeSlider->value() - 5);
            m_volumeSlider->setValue(next);
            statusBar()->showMessage(QString("音量: %1%").arg(next), 1200);
        } else {
            showNext();
        }
        event->accept();
        return;
    default:
        break;
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::wheelEvent(QWheelEvent *event)
{
    if (!event) {
        return;
    }

    const bool hasImage = !m_currentPixmap.isNull() || (m_currentMovie != nullptr);
    if (m_previewStack->currentWidget() == m_imageArea && hasImage) {
        const QPoint localPos = event->position().toPoint();
        const QRect imageRect = QRect(m_imageArea->mapTo(this, QPoint(0, 0)), m_imageArea->size());
        if (imageRect.contains(localPos)) {
            if (event->angleDelta().y() > 0) {
                zoomInImage();
            } else if (event->angleDelta().y() < 0) {
                zoomOutImage();
            }
            event->accept();
            return;
        }
    }

    QMainWindow::wheelEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (!event) {
        return;
    }

    if (event->button() == Qt::LeftButton && m_previewStack
        && m_previewStack->currentWidget() == m_imageArea && m_imageArea) {
        const bool hasImage = !m_currentPixmap.isNull() || (m_currentMovie != nullptr);
        const QRect imageRect = QRect(m_imageArea->mapTo(this, QPoint(0, 0)), m_imageArea->size());
        if (hasImage && imageRect.contains(event->position().toPoint())) {
            QScrollBar *hBar = m_imageArea->horizontalScrollBar();
            QScrollBar *vBar = m_imageArea->verticalScrollBar();
            const bool canPan = hBar && vBar && (hBar->maximum() > hBar->minimum() || vBar->maximum() > vBar->minimum());
            if (canPan) {
                m_isImagePanning = true;
                m_lastImagePanPos = event->pos();
                m_imageArea->viewport()->setCursor(Qt::ClosedHandCursor);
                event->accept();
                return;
            }
        }
    }

    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!event) {
        return;
    }

    if (m_isImagePanning && m_imageArea) {
        QScrollBar *hBar = m_imageArea->horizontalScrollBar();
        QScrollBar *vBar = m_imageArea->verticalScrollBar();
        if (hBar && vBar) {
            const QPoint delta = event->pos() - m_lastImagePanPos;
            hBar->setValue(hBar->value() - delta.x());
            vBar->setValue(vBar->value() - delta.y());
            m_lastImagePanPos = event->pos();
            event->accept();
            return;
        }
    }

    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (!event) {
        return;
    }

    if (event->button() == Qt::LeftButton && m_isImagePanning) {
        m_isImagePanning = false;
        if (m_imageArea && m_imageArea->viewport()) {
            m_imageArea->viewport()->unsetCursor();
        }
        event->accept();
        return;
    }

    QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        return;
    }

    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        paths << url.toLocalFile();
    }

    if (paths.isEmpty()) {
        return;
    }

    QString firstSupportedFile;
    for (const QString &path : paths) {
        QFileInfo info(path);
        if (info.isFile() && isSupportedPath(path)) {
            firstSupportedFile = info.absoluteFilePath();
            break;
        }
    }

    if (paths.size() == 1 && QFileInfo(paths.front()).isDir()) {
        loadPaths(collectFilesFromFolder(paths.front()), true);
    } else if (!firstSupportedFile.isEmpty()) {
        loadPaths(collectFilesFromSameFolder(firstSupportedFile), true, firstSupportedFile);
    } else {
        statusBar()->showMessage("拖拽项中未找到可播放的图片/视频文件", 2500);
    }
}

void MainWindow::openFiles()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        "选择任意文件（将自动加载当前文件夹）",
        QString(),
        "All Files (*.*)"
    );

    if (!file.isEmpty()) {
        loadPaths(collectFilesFromSameFolder(file), true, file);
    }
}

void MainWindow::showPrev()
{
    if (m_files.isEmpty()) {
        return;
    }

    const int row = (m_currentIndex - 1 + m_files.size()) % m_files.size();
    if (m_albumListWidget) {
        m_albumListWidget->setCurrentRow(row);
    } else {
        m_currentIndex = row;
        showCurrent();
    }
}

void MainWindow::showNext()
{
    if (m_files.isEmpty()) {
        return;
    }

    const int row = (m_currentIndex + 1) % m_files.size();
    if (m_albumListWidget) {
        m_albumListWidget->setCurrentRow(row);
    } else {
        m_currentIndex = row;
        showCurrent();
    }
}

void MainWindow::togglePlayPause()
{
    if (m_previewStack->currentWidget() != m_videoPanel) {
        statusBar()->showMessage("当前不是视频模式", 2000);
        return;
    }

    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        m_player->play();
    }
}

void MainWindow::onListSelectionChanged()
{
    if (!m_listWidget || m_updatingHistoryList) {
        return;
    }

    QListWidgetItem *item = m_listWidget->currentItem();
    if (!item) {
        return;
    }

    if (item->data(kHistoryRoleIsHeader).toBool()) {
        const int groupId = item->data(kHistoryRoleGroupId).toInt();
        if (m_historyCollapsedGroups.contains(groupId)) {
            m_historyCollapsedGroups.remove(groupId);
        } else {
            m_historyCollapsedGroups.insert(groupId);
        }
        applyHistoryGroupCollapse();
        QSignalBlocker blocker(m_listWidget);
        m_listWidget->setCurrentItem(nullptr);
        return;
    }

    const QString path = item->data(kHistoryRolePath).toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        statusBar()->showMessage("历史项文件不存在或已被移除", 2500);
        return;
    }

    logLine("History selected: " + path);
    loadPaths(collectFilesFromSameFolder(path), true, path);
    if (!m_toggleListAction->isChecked()) {
        m_toggleListAction->setChecked(true);
    }
}

void MainWindow::onAlbumSelectionChanged()
{
    if (!m_albumListWidget) {
        return;
    }

    const int row = m_albumListWidget->currentRow();
    if (row < 0 || row >= m_files.size() || row == m_currentIndex) {
        return;
    }

    m_currentIndex = row;
    logLine("Album selected row=" + QString::number(row) + ", file=" + m_files.at(row));
    showCurrent();
    QTimer::singleShot(0, this, [this, row]() {
        centerAlbumSelection(row);      
        QTimer::singleShot(0, this, [this, row]() {
            ensureAlbumIconsNear(row);
        });
    });
}

void MainWindow::onPlayerPositionChanged(qint64 position)
{
    if (!m_isSeeking) {
        m_seekSlider->setValue(static_cast<int>(position));
    }
    updateVideoTimeLabel(position, m_player->duration());
}

// 标定进度条刻度，让UI时间轴与视频实际时间对齐
void MainWindow::onPlayerDurationChanged(qint64 duration)
{
    m_seekSlider->setRange(0, static_cast<int>(duration));
    updateVideoTimeLabel(m_player->position(), duration);
}


void MainWindow::onSeekSliderPressed()
{
    m_isSeeking = true;
}

void MainWindow::onSeekSliderReleased()
{
    m_isSeeking = false;
    m_player->setPosition(m_seekSlider->value());
}

void MainWindow::onVolumeChanged(int value)
{
    m_audioOutput->setVolume(static_cast<float>(value) / 100.0f);
}

// 当视频无法正常播放时自动回退到静态图显示（如果有的话）
void MainWindow::setFitToWindowMode()
{
    m_imageScaleMode = ImageScaleMode::FitToWindow;
    m_zoomFactor = 1.0;
    updateScaledImage();
}

void MainWindow::setActualSizeMode()
{
    m_imageScaleMode = ImageScaleMode::ActualSize;
    m_zoomFactor = 1.0;
    updateScaledImage();
}

void MainWindow::zoomInImage()
{
    applyZoom(1.25);
}

void MainWindow::zoomOutImage()
{
    applyZoom(0.8);
}

/**
 *  确保指定行的相册图标已生成，若未生成则生成并设置到对应的 QListWidgetItem 上
 */
void MainWindow::loadPaths(const QStringList &paths, bool clearExisting, const QString &preferredPath)
{
    logLine("loadPaths invoked, input count=" + QString::number(paths.size()));

    QStringList filtered;
    filtered.reserve(paths.size());

    for (const QString &path : paths) {
        QFileInfo info(path);
        if (info.isDir() || !isSupportedPath(path)) {
            continue;
        }
        filtered << info.absoluteFilePath();
    }

    if (filtered.isEmpty()) {
        logLine("No supported files found in provided paths.");
        QMessageBox::information(this, "提示", "未找到可预览的媒体文件");
        return;
    }

    /**
     *  更新文件列表和界面项目，如果 clearExisting 为 true 则替换现有列表，否则追加到现有列表后面
     */
    if (clearExisting) {
        cancelImageLoad();
        cancelAllAlbumThumbWatchers();
        m_files = filtered;
        m_albumIconCache.clear();
        m_heicTranscodeCache.clear();
        m_albumIconQueue.clear();
        m_albumIconQueuedSet.clear();
        m_albumIconPumpScheduled = false;
        if (m_albumListWidget) {
            m_albumListWidget->clear();
        }
    } else {
        m_files.append(filtered);
    }

    applyAlbumSort(false);

    for (const QString &file : m_files) {
        QFileInfo info(file);
        if (m_albumListWidget) {
            auto *albumItem = new QListWidgetItem(buildListIcon(file), info.fileName());
            albumItem->setToolTip(info.absoluteFilePath());     // 鼠标悬停显示完整路径
            m_albumListWidget->addItem(albumItem);      // 预先生成相册图标，避免后续切换时界面卡顿
        }
    }

    int selectRow = 0;
    if (!preferredPath.isEmpty()) {
        const auto normalizePath = [](const QString &p) {
            const QFileInfo info(p);
            const QString canonical = info.canonicalFilePath();
            const QString abs = info.absoluteFilePath();
            const QString normalized = canonical.isEmpty() ? abs : canonical;
            return QDir::toNativeSeparators(normalized);
        };

        const QString preferNorm = normalizePath(preferredPath);
        int idx = -1;
        for (int i = 0; i < m_files.size(); ++i) {
            const QString fileNorm = normalizePath(m_files.at(i));
#ifdef Q_OS_WIN
            const bool samePath = (QString::compare(fileNorm, preferNorm, Qt::CaseInsensitive) == 0);
#else
            const bool samePath = (fileNorm == preferNorm);
#endif
            if (samePath) {
                idx = i;
                break;
            }
        }

        if (idx < 0) {
            logLine("preferredPath not found, fallback row=0, preferred=" + preferNorm);
        }
        if (idx >= 0) {
            selectRow = idx;
        }
    }

    if (m_albumListWidget) {
        QSignalBlocker blocker(m_albumListWidget);
        m_albumListWidget->setCurrentRow(selectRow);
    }

    const int albumRow = m_albumListWidget ? m_albumListWidget->currentRow() : -1;
    logLine("post-select rows: albumRow=" + QString::number(albumRow)
            + ", selectRow=" + QString::number(selectRow));

    // 某些情况下 currentRowChanged 可能不会按预期触发，显式刷新一次预览。
    if (selectRow >= 0 && selectRow < m_files.size()) {
        m_currentIndex = selectRow;
        if (m_albumListWidget) {
            const QModelIndex idx = m_albumListWidget->model()->index(selectRow, 0);
            if (idx.isValid()) {
                m_albumListWidget->scrollTo(idx);
            }
        }
        showCurrent();
        QTimer::singleShot(0, this, [this, selectRow]() {
            centerAlbumSelection(selectRow);
            QTimer::singleShot(0, this, [this, selectRow]() {
                ensureAlbumIconsNear(selectRow);
                queueVisibleAlbumIcons();
            });
        });
    }

    logLine("Files loaded, filtered count=" + QString::number(filtered.size()) + ", total=" + QString::number(m_files.size()));
    statusBar()->showMessage(QString("已加载 %1 个文件").arg(m_files.size()), 3000);
}

QStringList MainWindow::collectFilesFromFolder(const QString &folderPath) const
{
    QStringList files;

    QDirIterator it(folderPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        files << file;
    }

    files.sort(Qt::CaseInsensitive);
    return files;
}

QStringList MainWindow::collectFilesFromSameFolder(const QString &filePath) const
{
    QFileInfo info(filePath);
    QDir dir = info.absoluteDir();
    QStringList files;

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        files << entry.absoluteFilePath();
    }
    return files;
}

QString MainWindow::albumSortLabel() const
{
    QString key;
    switch (m_albumSortKey) {
    case AlbumSortKey::FileName:
        key = "文件名";
        break;
    case AlbumSortKey::Random:
        key = "随机";
        break;
    case AlbumSortKey::FileSize:
        key = "文件大小";
        break;
    case AlbumSortKey::Extension:
        key = "扩展名";
        break;
    case AlbumSortKey::CreatedDate:
        key = "创建日期";
        break;
    case AlbumSortKey::AccessedDate:
        key = "访问日期";
        break;
    case AlbumSortKey::ModifiedDate:
        key = "修改日期";
        break;
    }
    return key + (m_albumSortOrder == Qt::AscendingOrder ? " 升序" : " 降序");
}

void MainWindow::applyAlbumSort(bool reshuffleRandom)
{
    if (m_files.isEmpty()) {
        return;
    }

    if (m_albumSortKey == AlbumSortKey::Random && reshuffleRandom) {
        for (int i = m_files.size() - 1; i > 0; --i) {
            const int j = QRandomGenerator::global()->bounded(i + 1);
            m_files.swapItemsAt(i, j);
        }
        if (m_albumSortOrder == Qt::DescendingOrder) {
            std::reverse(m_files.begin(), m_files.end());
        }
        return;
    }

    auto keyFor = [this](const QString &path) {
        const QFileInfo info(path);
        switch (m_albumSortKey) {
        case AlbumSortKey::FileName:
            return info.fileName().toLower();
        case AlbumSortKey::Random:
            return info.fileName().toLower();
        case AlbumSortKey::FileSize:
            return QString::number(info.size()).rightJustified(20, '0');
        case AlbumSortKey::Extension:
            return info.suffix().toLower() + "|" + info.fileName().toLower();
        case AlbumSortKey::CreatedDate:
            return info.birthTime().toString(Qt::ISODateWithMs);
        case AlbumSortKey::AccessedDate:
            return info.lastRead().toString(Qt::ISODateWithMs);
        case AlbumSortKey::ModifiedDate:
            return info.lastModified().toString(Qt::ISODateWithMs);
        }
        return info.fileName().toLower();
    };

    std::sort(m_files.begin(), m_files.end(), [this, &keyFor](const QString &a, const QString &b) {
        const QString ka = keyFor(a);
        const QString kb = keyFor(b);
        int cmp = QString::compare(ka, kb, Qt::CaseInsensitive);
        if (cmp == 0) {
            cmp = QString::compare(QFileInfo(a).fileName(), QFileInfo(b).fileName(), Qt::CaseInsensitive);
        }
        if (m_albumSortOrder == Qt::DescendingOrder) {
            cmp = -cmp;
        }
        return cmp < 0;
    });
}

void MainWindow::showPreviewContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);

    QAction *toggleToolbar = menu.addAction("显示工具栏");
    toggleToolbar->setCheckable(true);
    toggleToolbar->setChecked(m_mainToolBar ? m_mainToolBar->isVisible() : true);

    QAction *toggleAlbum = menu.addAction("显示相册画板");
    toggleAlbum->setCheckable(true);
    toggleAlbum->setChecked(m_showAlbumPanel);

    menu.addSeparator();
    QMenu *sortMenu = menu.addMenu("相册画板加载顺序");
    QActionGroup *sortGroup = new QActionGroup(sortMenu);
    sortGroup->setExclusive(true);

    auto addSortAction = [sortMenu, sortGroup, this](const QString &text, AlbumSortKey key) {
        QAction *a = sortMenu->addAction(text);
        a->setCheckable(true);
        a->setData(static_cast<int>(key));
        a->setChecked(m_albumSortKey == key);
        sortGroup->addAction(a);
        return a;
    };

    addSortAction("文件名", AlbumSortKey::FileName);
    addSortAction("随机", AlbumSortKey::Random);
    addSortAction("文件大小", AlbumSortKey::FileSize);
    addSortAction("扩展名", AlbumSortKey::Extension);
    addSortAction("创建日期", AlbumSortKey::CreatedDate);
    addSortAction("访问日期", AlbumSortKey::AccessedDate);
    addSortAction("修改日期", AlbumSortKey::ModifiedDate);

    sortMenu->addSeparator();
    QActionGroup *orderGroup = new QActionGroup(sortMenu);
    orderGroup->setExclusive(true);
    QAction *ascAction = sortMenu->addAction("升序");
    ascAction->setCheckable(true);
    ascAction->setChecked(m_albumSortOrder == Qt::AscendingOrder);
    orderGroup->addAction(ascAction);
    QAction *descAction = sortMenu->addAction("降序");
    descAction->setCheckable(true);
    descAction->setChecked(m_albumSortOrder == Qt::DescendingOrder);
    orderGroup->addAction(descAction);

    QAction *picked = menu.exec(globalPos);
    if (!picked) {
        return;
    }

    if (picked == toggleToolbar) {
        const bool show = picked->isChecked();
        if (m_mainToolBar) {
            m_mainToolBar->setVisible(show);
        }
        return;
    }

    if (picked == toggleAlbum) {
        const bool show = picked->isChecked();
        m_showAlbumPanel = show;
        if (m_albumListWidget) {
            m_albumListWidget->setVisible(show);
        }
        if (m_toggleAlbumAction) {
            m_toggleAlbumAction->setChecked(show);
        }
        return;
    }

    const QString currentPath = (m_currentIndex >= 0 && m_currentIndex < m_files.size())
        ? m_files.at(m_currentIndex)
        : QString();

    bool sortChanged = false;
    if (picked->parent() == sortMenu && picked->data().isValid()) {
        const AlbumSortKey newKey = static_cast<AlbumSortKey>(picked->data().toInt());
        if (newKey != m_albumSortKey) {
            m_albumSortKey = newKey;
            sortChanged = true;
        }
    }
    if (picked == ascAction && m_albumSortOrder != Qt::AscendingOrder) {
        m_albumSortOrder = Qt::AscendingOrder;
        sortChanged = true;
    }
    if (picked == descAction && m_albumSortOrder != Qt::DescendingOrder) {
        m_albumSortOrder = Qt::DescendingOrder;
        sortChanged = true;
    }

    if (!sortChanged) {
        return;
    }

    applyAlbumSort(true);
    if (m_albumListWidget) {
        QSignalBlocker blocker(m_albumListWidget);
        m_albumListWidget->clear();
        for (const QString &file : m_files) {
            QFileInfo info(file);
            auto *albumItem = new QListWidgetItem(buildListIcon(file), info.fileName());
            albumItem->setToolTip(info.absoluteFilePath());
            m_albumListWidget->addItem(albumItem);
        }
    }

    int row = 0;
    if (!currentPath.isEmpty()) {
        const int idx = m_files.indexOf(currentPath);
        if (idx >= 0) {
            row = idx;
        }
    }
    m_currentIndex = row;
    if (m_albumListWidget) {
        m_albumListWidget->setCurrentRow(row);
    }
    showCurrent();
    statusBar()->showMessage("相册排序: " + albumSortLabel(), 2500);
}

void MainWindow::showCurrent()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_files.size()) {
        return;
    }

    const QString file = m_files.at(m_currentIndex);
    if (isImageFile(file) || QFileInfo(file).suffix().compare("livp", Qt::CaseInsensitive) == 0) {
        m_imageScaleMode = ImageScaleMode::FitToWindow;
        m_zoomFactor = 1.0;
        m_isImagePanning = false;
        if (m_imageArea && m_imageArea->viewport()) {
            m_imageArea->viewport()->unsetCursor();
        }
        if (m_imageLabel) {
            m_imageLabel->unsetCursor();
        }
    }

    appendHistoryEntry(file);
    const ResolvedMedia media = resolveMedia(file);
    showMedia(media);
    QTimer::singleShot(0, this, [this]() {
        centerAlbumSelection(m_currentIndex);
    });
}

void MainWindow::appendHistoryEntry(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    HistoryEntry entry;
    entry.path = QFileInfo(path).absoluteFilePath();
    entry.timestamp = QDateTime::currentDateTime();
    m_historyEntries.append(entry);

    rebuildHistoryList();
}

void MainWindow::rebuildHistoryList()
{
    if (!m_listWidget) {
        return;
    }

    m_updatingHistoryList = true;               
    QSignalBlocker blocker(m_listWidget);       // 重建历史列表时阻止触发 selectionChanged 信号，避免界面卡顿和不必要的加载
    m_listWidget->clear();

    // 将历史记录分组，每组 kHistoryGroupSize 条，生成对应的组头，默认折叠除最后一组以外的所有组
    const int groupCount = (m_historyEntries.size() + kHistoryGroupSize - 1) / kHistoryGroupSize;  
    m_historyCollapsedGroups.clear();
    for (int group = 0; group < qMax(0, groupCount - 1); ++group) {
        m_historyCollapsedGroups.insert(group);
    }

    for (int start = 0; start < m_historyEntries.size(); start += kHistoryGroupSize) {
        const int groupId = start / kHistoryGroupSize;
        const HistoryEntry &first = m_historyEntries.at(start);
        const QString groupTitle = "历史组 "
            + QString::number(groupId + 1)
            + "  ·  "
            + first.timestamp.toString("yyyy-MM-dd HH:mm")
            + "  ·  " + QString::number(qMin(kHistoryGroupSize, m_historyEntries.size() - start)) + " 条";

        auto *header = new QListWidgetItem(groupTitle);
        QFont f = header->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 1);
        header->setFont(f);
        header->setFlags((header->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled) & ~Qt::ItemIsDragEnabled);
        header->setData(kHistoryRoleIsHeader, true);
        header->setData(kHistoryRoleGroupId, groupId);
        header->setForeground(QColor(74, 108, 154));
        header->setBackground(QColor(232, 241, 255));
        header->setSizeHint(QSize(0, 30));
        m_listWidget->addItem(header);

        const int end = qMin(start + kHistoryGroupSize, m_historyEntries.size());
        for (int i = start; i < end; ++i) {
            const HistoryEntry &entry = m_historyEntries.at(i);
            QFileInfo info(entry.path);
            const QString title = entry.timestamp.toString("HH:mm:ss") + "  ·  " + info.fileName();

            // 用 buildListIcon（纯绘图，<1ms）避免在主线程做图片/视频解码
            auto *item = new QListWidgetItem(buildListIcon(entry.path), title);
            item->setToolTip(entry.path);
            item->setData(kHistoryRolePath, entry.path);
            item->setData(kHistoryRoleIsHeader, false);
            item->setData(kHistoryRoleGroupId, groupId);
            item->setForeground(QColor(30, 42, 58));
            item->setSizeHint(QSize(0, 28));
            m_listWidget->addItem(item);
        }
    }

    applyHistoryGroupCollapse();

    // 如果有历史记录，默认选中最后一条，并滚动到可见位置
    if (m_listWidget->count() > 0) {
        m_listWidget->scrollToBottom();
    }
    m_updatingHistoryList = false;
}

void MainWindow::applyHistoryGroupCollapse()
{
    if (!m_listWidget) {
        return;
    }

    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem *item = m_listWidget->item(i);
        if (!item) {
            continue;
        }

        const bool isHeader = item->data(kHistoryRoleIsHeader).toBool();
        const int groupId = item->data(kHistoryRoleGroupId).toInt();
        const bool collapsed = m_historyCollapsedGroups.contains(groupId);

        if (isHeader) {
            QString text = item->text();
            if (text.startsWith("▼ ") || text.startsWith("▶ ")) {
                text = text.mid(2);
            }
            item->setText(QString(collapsed ? "▶ " : "▼ ") + text);
            item->setHidden(false);
        } else {
            item->setHidden(collapsed);
        }
    }
}

void MainWindow::centerAlbumSelection(int row)
{
    if (!m_albumListWidget || row < 0 || row >= m_albumListWidget->count()) {
        return;
    }

    QListWidgetItem *item = m_albumListWidget->item(row);
    if (!item) {
        return;
    }

    m_albumListWidget->scrollToItem(item, QAbstractItemView::PositionAtCenter);

    const QRect rect = m_albumListWidget->visualItemRect(item);
    if (!rect.isValid()) {
        return;
    }

    QScrollBar *hBar = m_albumListWidget->horizontalScrollBar();
    if (!hBar) {
        return;
    }

    const int viewportWidth = m_albumListWidget->viewport()->width();
    const int contentCenterX = rect.center().x() + hBar->value();
    const int target = contentCenterX - viewportWidth / 2;
    hBar->setValue(qBound(hBar->minimum(), target, hBar->maximum()));

    QTimer::singleShot(0, this, [this, row]() {
        if (!m_albumListWidget || row < 0 || row >= m_albumListWidget->count()) {
            return;
        }
        QListWidgetItem *againItem = m_albumListWidget->item(row);
        if (!againItem) {
            return;
        }
        const QRect againRect = m_albumListWidget->visualItemRect(againItem);
        QScrollBar *againBar = m_albumListWidget->horizontalScrollBar();
        if (!againRect.isValid() || !againBar) {
            return;
        }
        const int againCenterX = againRect.center().x() + againBar->value();
        const int againTarget = againCenterX - m_albumListWidget->viewport()->width() / 2;
        againBar->setValue(qBound(againBar->minimum(), againTarget, againBar->maximum()));
    });
}
