#include "tier/TierListView.h"

#include "logging/Logger.h"
#include "tier/TierDragController.h"
#include "tier/TierListDelegate.h"
#include "tier/TierListModel.h"

#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFileInfo>
#include <QScrollBar>
#include <QSet>
#include <QVariantAnimation>

#include <cmath>
#include <functional>
#include <utility>

namespace tlm {

namespace {
const QColor kGridLine(QStringLiteral("#c8d0da"));
const QColor kDropLine(QStringLiteral("#1677ff"));
constexpr int kRowReorderAnimationMs = 190;
constexpr int kImageReorderAnimationMs = 145;
constexpr int kInitialLayoutLineHeight = 84;
constexpr qreal kDockMaxScale = 1.30;

bool hasRowMime(const QMimeData* mimeData) {
    return mimeData && mimeData->hasFormat(TierDragController::rowMimeType());
}

qreal canvasBackgroundVisibility(const TierProject* project) {
    if (!project || project->canvas.value(QStringLiteral("backgroundImagePath")).toString().isEmpty()) {
        return 0.0;
    }
    return qBound<qreal>(
        0.0,
        project->canvas.value(QStringLiteral("backgroundVisibility"))
            .toDouble(project->canvas.value(QStringLiteral("backgroundImageOpacity")).toDouble(
                project->canvas.value(QStringLiteral("backgroundOpacity")).toDouble(1.0))),
        1.0);
}

QPainterPath placeholderClipPath(const QRectF& rect, bool firstRow, bool lastRow) {
    if (!firstRow && !lastRow) {
        QPainterPath path;
        path.addRect(rect);
        return path;
    }

    QPainterPath rounded;
    rounded.addRoundedRect(rect, TierListDelegate::outerRadius(), TierListDelegate::outerRadius());

    QPainterPath squareMask;
    if (!firstRow) {
        squareMask.addRect(QRectF(rect.left(), rect.top(), rect.width(),
                                  TierListDelegate::outerRadius() + 1));
    }
    if (!lastRow) {
        squareMask.addRect(QRectF(rect.left(), rect.bottom() - TierListDelegate::outerRadius() - 1,
                                  rect.width(), TierListDelegate::outerRadius() + 1));
    }
    return rounded.united(squareMask);
}

class RowDragCatchment final : public QWidget {
public:
    using MoveHandler = std::function<void(const QPoint&)>;
    using DropHandler = std::function<bool(const QPoint&)>;

    RowDragCatchment(QWidget* parent, MoveHandler moveHandler, DropHandler dropHandler)
        : QWidget(parent), m_moveHandler(std::move(moveHandler)), m_dropHandler(std::move(dropHandler)) {
        setAcceptDrops(true);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (!hasRowMime(event->mimeData())) {
            event->ignore();
            return;
        }
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (!hasRowMime(event->mimeData())) {
            event->ignore();
            return;
        }
        if (m_moveHandler) {
            m_moveHandler(mapToGlobal(event->position().toPoint()));
        }
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dropEvent(QDropEvent* event) override {
        if (!hasRowMime(event->mimeData())) {
            event->ignore();
            return;
        }
        const bool accepted = m_dropHandler && m_dropHandler(mapToGlobal(event->position().toPoint()));
        if (!accepted) {
            event->ignore();
            return;
        }
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

private:
    MoveHandler m_moveHandler;
    DropHandler m_dropHandler;
};
} // namespace

TierListView::TierListView(QWidget* parent) : QListView(parent) {
    setDragEnabled(false);
    setDropIndicatorShown(false);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionMode(QAbstractItemView::NoSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setSpacing(0);
    setUniformItemSizes(false);
    setMouseTracking(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setViewportMargins(0, 0, 0, 0);
    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(false);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setStyleSheet(QStringLiteral("QListView{background:transparent;outline:0;}"));
}

void TierListView::refreshLayoutMetrics() {
    TierListModel* model = tierModel();
    if (!model) {
        return;
    }

    const int rows = model->rowCount();
    if (rows <= 0) {
        return;
    }

    const int availableHeight = qMax(1, viewport()->height());
    const int viewportWidth = qMax(1, viewport()->width());
    const int labelWidth = tierDelegate() ? tierDelegate()->labelWidth()
                                          : TierListDelegate::minimumLabelWidth();
    auto imageCountAt = [this, model](int row) {
        if (m_imageDragActive) {
            return previewImageCountForRow(row);
        }
        const TierRow* tierRow = model->tierRowAt(row);
        return tierRow ? static_cast<int>(tierRow->imageIds.size()) : 0;
    };
    auto distributeHeights = [rows, availableHeight](const QVector<int>& units) {
        int totalUnits = 0;
        for (int unit : units) {
            totalUnits += qMax(1, unit);
        }

        const int baseUnitHeight = qMax(1, availableHeight / qMax(1, totalUnits));
        int remainingPixels = availableHeight - baseUnitHeight * totalUnits;
        QVector<int> heights;
        heights.reserve(rows);
        for (int row = 0; row < rows; ++row) {
            const int unit = qMax(1, units.at(row));
            const int extra = qBound(0, remainingPixels, unit);
            heights.append(unit * baseUnitHeight + extra);
            remainingPixels -= extra;
        }
        return heights;
    };

    QVector<int> rowUnits;
    rowUnits.reserve(rows);
    for (int row = 0; row < rows; ++row) {
        rowUnits.append(TierListDelegate::rowUnitsForImageCount(imageCountAt(row), viewportWidth,
                                                                kInitialLayoutLineHeight, labelWidth));
    }

    QVector<int> rowHeights;
    for (int pass = 0; pass < 10; ++pass) {
        rowHeights = distributeHeights(rowUnits);
        QVector<int> nextUnits;
        nextUnits.reserve(rows);
        bool changed = false;
        for (int row = 0; row < rows; ++row) {
            const int lineHeight = qMax(1, rowHeights.at(row) / qMax(1, rowUnits.at(row)));
            const int units = TierListDelegate::rowUnitsForImageCount(imageCountAt(row), viewportWidth,
                                                                      lineHeight, labelWidth);
            nextUnits.append(units);
            changed = changed || units != rowUnits.at(row);
        }
        rowUnits = std::move(nextUnits);
        if (!changed) {
            break;
        }
    }
    rowHeights = distributeHeights(rowUnits);

    model->setLayoutMetrics(std::move(rowHeights), std::move(rowUnits));
    doItemsLayout();
    viewport()->update();
}

qreal TierListView::visualOffsetForIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return 0.0;
    }
    return m_reorderRowOffsets.value(index.row(), 0.0);
}

bool TierListView::isReorderSourceIndex(const QModelIndex& index) const {
    return index.isValid() && m_reorderSourceRow >= 0 && index.row() == m_reorderSourceRow;
}

bool TierListView::isVisualFirstRow(const QModelIndex& index) const {
    if (!index.isValid()) {
        return false;
    }
    if (m_reorderSourceRow < 0) {
        return index.row() == 0;
    }
    if (isReorderSourceIndex(index)) {
        return false;
    }

    TierListModel* model = tierModel();
    const QRect currentRect = animatedVisualRect(index);
    if (!model || !currentRect.isValid()) {
        return index.row() == 0;
    }

    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex other = model->index(row, 0);
        if (!other.isValid() || isReorderSourceIndex(other)) {
            continue;
        }
        const QRect otherRect = animatedVisualRect(other);
        if (otherRect.isValid() && otherRect.top() < currentRect.top()) {
            return false;
        }
    }
    return true;
}

bool TierListView::isVisualLastRow(const QModelIndex& index) const {
    if (!index.isValid()) {
        return false;
    }
    TierListModel* model = tierModel();
    if (m_reorderSourceRow < 0) {
        return model ? index.row() == model->rowCount() - 1 : false;
    }
    if (isReorderSourceIndex(index)) {
        return false;
    }

    const QRect currentRect = animatedVisualRect(index);
    if (!model || !currentRect.isValid()) {
        return model ? index.row() == model->rowCount() - 1 : false;
    }

    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex other = model->index(row, 0);
        if (!other.isValid() || isReorderSourceIndex(other)) {
            continue;
        }
        const QRect otherRect = animatedVisualRect(other);
        if (otherRect.isValid() && otherRect.bottom() > currentRect.bottom()) {
            return false;
        }
    }
    return true;
}

bool TierListView::isImageDragSource(const QString& imageId) const {
    return m_imageDragActive && m_imageDragSourceRow >= 0 && imageId == m_imageDragId;
}

QPointF TierListView::visualOffsetForImage(const QString& imageId) const {
    return m_imageTileOffsets.value(imageId, QPointF());
}

qreal TierListView::dockScaleForImage(const QModelIndex& index, const QRect& tileRect,
                                      const QString& imageId) const {
    if (m_dockHoverProgress <= 0.001 || m_imageDragActive || m_rowDropActive ||
        !index.isValid() || index.row() != m_dockHoverRow || imageId.isEmpty()) {
        return 1.0;
    }

    const qreal lineDistance = std::abs(tileRect.center().y() - m_dockHoverPosition.y());
    if (lineDistance > qMax<qreal>(1.0, tileRect.height() * 0.72)) {
        return 1.0;
    }

    const qreal sigma = qMax<qreal>(1.0, tileRect.width() * 0.92);
    const qreal distance = tileRect.center().x() - m_dockHoverPosition.x();
    const qreal gaussian = std::exp(-(distance * distance) / (2.0 * sigma * sigma));
    return 1.0 + (kDockMaxScale - 1.0) * gaussian * m_dockHoverProgress;
}

QPointF TierListView::dockOffsetForImage(const QModelIndex& index, const QRect& tileRect,
                                         const QString& imageId) const {
    const qreal scale = dockScaleForImage(index, tileRect, imageId);
    if (qFuzzyCompare(scale, 1.0)) {
        return {};
    }

    const qreal dx = tileRect.center().x() - m_dockHoverPosition.x();
    const qreal sigma = qMax<qreal>(1.0, tileRect.width() * 0.92);
    const qreal gaussian = std::exp(-(dx * dx) / (2.0 * sigma * sigma)) * m_dockHoverProgress;
    const qreal direction = dx < -0.5 ? -1.0 : (dx > 0.5 ? 1.0 : 0.0);
    const qreal spread = tileRect.width() * 0.16 * gaussian;
    const qreal lift = tileRect.height() * 0.08 * gaussian;
    return QPointF(direction * spread, -lift);
}

QRect TierListView::imageSourceRect(const QString& imageId) const {
    TierListModel* model = tierModel();
    TierListDelegate* delegate = tierDelegate();
    if (!model || !delegate || imageId.isEmpty()) {
        return {};
    }

    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        const QRect local = delegate->imageRectForId(index, visualRect(index), imageId);
        if (local.isValid()) {
            return QRect(viewport()->mapTo(window(), local.topLeft()), local.size());
        }
    }
    return {};
}

void TierListView::mousePressEvent(QMouseEvent* event) {
    resetPressState();
    if (event->button() != Qt::LeftButton) {
        QListView::mousePressEvent(event);
        return;
    }

    TierListDelegate* delegate = tierDelegate();
    TierListModel* model = tierModel();
    const QModelIndex index = indexAt(event->pos());
    if (!delegate || !model || !index.isValid()) {
        QListView::mousePressEvent(event);
        return;
    }

    const QRect rowRect = visualRect(index);
    m_pressPosition = event->pos();
    m_pressedIndex = index;
    m_pressedRowId = model->rowIdAt(index.row());

    if (delegate->labelRect(rowRect).contains(event->pos())) {
        m_pressKind = PressKind::RowLabel;
        setCursor(Qt::OpenHandCursor);
        setFocus(Qt::MouseFocusReason);
        Logger::debug(QStringLiteral("tier.list.mouse.press kind=row-label rowId=%1 row=%2 pos=(%3,%4)")
                          .arg(m_pressedRowId)
                          .arg(index.row())
                          .arg(event->pos().x())
                          .arg(event->pos().y()));
        event->accept();
        return;
    }

    const QString imageId = delegate->imageIdAt(index, rowRect, event->pos());
    if (!imageId.isEmpty()) {
        m_pressKind = PressKind::ImageTile;
        m_pressedImageId = imageId;
        m_activeImageId = imageId;
        m_activeImageIndex = index;
        setFocus(Qt::MouseFocusReason);
        Logger::debug(QStringLiteral("tier.list.mouse.press kind=image imageId=%1 rowId=%2 row=%3 pos=(%4,%5)")
                          .arg(imageId, m_pressedRowId)
                          .arg(index.row())
                          .arg(event->pos().x())
                          .arg(event->pos().y()));
        viewport()->update();
        event->accept();
        return;
    }

    QListView::mousePressEvent(event);
}

void TierListView::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton) || m_pressKind == PressKind::None) {
        updateDockHover(event->pos());
        QListView::mouseMoveEvent(event);
        return;
    }
    if ((event->pos() - m_pressPosition).manhattanLength() < QApplication::startDragDistance()) {
        return;
    }

    if (m_pressKind == PressKind::RowLabel) {
        animateDockHover(0.0);
        startRowDrag();
        event->accept();
        return;
    }
    if (m_pressKind == PressKind::ImageTile) {
        animateDockHover(0.0);
        startImageDrag();
        event->accept();
        return;
    }
}

void TierListView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_pressKind == PressKind::RowLabel &&
        !m_pressedRowId.isEmpty()) {
        emit rowEditRequested(m_pressedRowId);
        resetPressState();
        unsetCursor();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_pressKind == PressKind::ImageTile &&
        !m_pressedImageId.isEmpty()) {
        emit imageSelected(m_pressedImageId);
        Logger::debug(QStringLiteral("tier.list.image.select imageId=%1").arg(m_pressedImageId));
        resetPressState();
        unsetCursor();
        event->accept();
        return;
    }
    resetPressState();
    unsetCursor();
    QListView::mouseReleaseEvent(event);
}

void TierListView::mouseDoubleClickEvent(QMouseEvent* event) {
    TierListDelegate* delegate = tierDelegate();
    const QModelIndex index = indexAt(event->pos());
    if (delegate && index.isValid()) {
        const QRect rowRect = visualRect(index);
        const QString imageId = delegate->imageIdAt(index, rowRect, event->pos());
        if (!imageId.isEmpty()) {
            const QRect imageRect = delegate->imageRectForId(index, rowRect, imageId);
            m_activeImageId = imageId;
            m_activeImageIndex = index;
            Logger::info(QStringLiteral("tier.list.preview.request source=double-click imageId=%1 row=%2")
                             .arg(imageId)
                             .arg(index.row()));
            emit imagePreviewRequested(imageId,
                                       QRect(viewport()->mapTo(window(), imageRect.topLeft()),
                                             imageRect.size()));
            event->accept();
            return;
        }
    }
    QListView::mouseDoubleClickEvent(event);
}

void TierListView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !m_activeImageId.isEmpty()) {
        QRect source = viewportImageRect(m_activeImageIndex, m_activeImageId);
        if (!source.isValid()) {
            source = QRect(viewport()->rect().center() - QPoint(20, 20), QSize(40, 40));
        }
        Logger::info(QStringLiteral("tier.list.preview.request source=space imageId=%1").arg(m_activeImageId));
        emit imagePreviewRequested(m_activeImageId,
                                   QRect(viewport()->mapTo(window(), source.topLeft()), source.size()));
        event->accept();
        return;
    }
    QListView::keyPressEvent(event);
}

void TierListView::leaveEvent(QEvent* event) {
    animateDockHover(0.0);
    QListView::leaveEvent(event);
}

void TierListView::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    refreshLayoutMetrics();
}

void TierListView::paintEvent(QPaintEvent* event) {
    {
        QPainter backgroundPainter(viewport());
        backgroundPainter.setRenderHint(QPainter::Antialiasing);
        paintCanvasBackground(&backgroundPainter);
    }

    QListView::paintEvent(event);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_rowDropActive && !m_rowDropId.isEmpty() && m_rowDropIndex >= 0) {
        const qreal y = m_placeholderY;
        if (y >= 0.0) {
            QColor fill = kDropLine;
            fill.setAlpha(26);
            const int sourceHeight = qMax(2, m_reorderSourceRect.height());
            const QRectF placeholderRect(0.5, y, qMax(1, viewport()->width() - 1), sourceHeight);
            const bool firstVisualRow = placeholderRect.top() <= 0.5;
            const bool lastVisualRow = placeholderRect.bottom() >= viewport()->height() - 1.5;
            const QPainterPath clipPath = placeholderClipPath(placeholderRect, firstVisualRow, lastVisualRow);
            painter.fillPath(clipPath, fill);
        }
    }

    if (m_imageDragActive && m_imagePlaceholderRect.isValid()) {
        QColor fill = kDropLine;
        fill.setAlpha(42);
        painter.fillRect(m_imagePlaceholderRect, fill);
    }

    const QRectF outline = QRectF(viewport()->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(kGridLine, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outline, TierListDelegate::outerRadius(), TierListDelegate::outerRadius());
}

void TierListView::dragEnterEvent(QDragEnterEvent* event) {
    if (acceptsTierDrag(event->mimeData())) {
        Logger::debug(QStringLiteral("tier.list.drag.enter rowMime=%1 imageMime=%2 pos=(%3,%4)")
                          .arg(event->mimeData()->hasFormat(TierDragController::rowMimeType()))
                          .arg(event->mimeData()->hasFormat(TierDragController::imageMimeType()))
                          .arg(event->position().toPoint().x())
                          .arg(event->position().toPoint().y()));
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }
    QListView::dragEnterEvent(event);
}

void TierListView::dragMoveEvent(QDragMoveEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (!acceptsTierDrag(mimeData)) {
        QListView::dragMoveEvent(event);
        return;
    }

    if (mimeData->hasFormat(TierDragController::rowMimeType())) {
        m_rowDropId = TierDragController::rowIdFromMimeData(mimeData);
        updateRowDropIntent(event->position().toPoint(), m_rowDropId);
        clearImageDropState();
    } else if (mimeData->hasFormat(TierDragController::imageMimeType())) {
        const QString imageId = TierDragController::imageIdFromMimeData(mimeData);
        beginImageDragVisuals(imageId);
        m_rowDropActive = false;
        updateImageDropIntentAt(event->position().toPoint());
    }

    viewport()->update();
    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void TierListView::dragLeaveEvent(QDragLeaveEvent* event) {
    if (m_reorderSourceRow >= 0) {
        clearImageDropState();
    } else {
        clearDropState();
    }
    if (m_imageDragActive && m_imageDragSourceRow < 0) {
        finishImageDragVisuals();
    } else {
        updateImageDropIntent({}, -1);
    }
    viewport()->update();
    QListView::dragLeaveEvent(event);
}

void TierListView::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (!acceptsTierDrag(mimeData)) {
        QListView::dropEvent(event);
        return;
    }

    if (mimeData->hasFormat(TierDragController::rowMimeType())) {
        const QString rowId = TierDragController::rowIdFromMimeData(mimeData);
        const int destinationIndex = rowDropIndexForPosition(event->position().toPoint(), rowId);
        commitRowDrop(rowId, destinationIndex, "viewDrop");
    } else if (mimeData->hasFormat(TierDragController::imageMimeType())) {
        const QString imageId = TierDragController::imageIdFromMimeData(mimeData);
        updateImageDropIntentAt(event->position().toPoint());
        const QModelIndex target = m_imageDropIndex;
        if (!imageId.isEmpty() && target.isValid()) {
            if (TierListModel* model = tierModel()) {
                const int insertionIndex = m_imageDropInsertionIndex;
                if (insertionIndex >= 0) {
                    Logger::info(QStringLiteral("tier.list.image.drop imageId=%1 rowId=%2 row=%3 index=%4 reason=viewDrop")
                                     .arg(imageId, model->rowIdAt(target.row()))
                                     .arg(target.row())
                                     .arg(insertionIndex));
                    emit imageDropped(imageId, model->rowIdAt(target.row()), insertionIndex);
                }
            }
        }
    }

    finishImageDragVisuals();
    clearDropState();
    viewport()->update();
    event->setDropAction(Qt::MoveAction);
    event->accept();
}

TierListModel* TierListView::tierModel() const {
    return qobject_cast<TierListModel*>(model());
}

TierListDelegate* TierListView::tierDelegate() const {
    return qobject_cast<TierListDelegate*>(itemDelegate());
}

void TierListView::resetPressState() {
    m_pressKind = PressKind::None;
    m_pressedIndex = QPersistentModelIndex();
    m_pressedRowId.clear();
    m_pressedImageId.clear();
}

void TierListView::beginRowReorderVisuals(const QModelIndex& index) {
    stopReorderAnimations();
    m_reorderSourceRow = index.row();
    m_reorderSourceRect = visualRect(index);
    m_placeholderY = m_reorderSourceRect.top();
    m_reorderRowOffsets.clear();
    Logger::debug(QStringLiteral("tier.list.row.visual.begin sourceRow=%1 height=%2")
                      .arg(m_reorderSourceRow)
                      .arg(m_reorderSourceRect.height()));
    viewport()->update();
}

void TierListView::finishRowReorderVisuals(bool accepted) {
    if (accepted) {
        stopReorderAnimations();
        m_reorderRowOffsets.clear();
        m_reorderSourceRow = -1;
        m_reorderSourceRect = QRect();
        m_placeholderY = 0.0;
        viewport()->update();
        return;
    }

    const auto rows = m_reorderRowOffsets.keys();
    for (int row : rows) {
        animateRowOffset(row, 0.0);
    }
    m_reorderSourceRow = -1;
    m_reorderSourceRect = QRect();
    m_placeholderY = 0.0;
    viewport()->update();
}

void TierListView::animateReorderToIndex(int destinationIndex) {
    if (m_reorderSourceRow < 0 || destinationIndex < 0) {
        return;
    }

    const QHash<int, qreal> targets = targetOffsetsForDestination(destinationIndex);
    QSet<int> rows;
    for (auto it = m_reorderRowOffsets.cbegin(); it != m_reorderRowOffsets.cend(); ++it) {
        rows.insert(it.key());
    }
    for (auto it = m_reorderRowAnimations.cbegin(); it != m_reorderRowAnimations.cend(); ++it) {
        rows.insert(it.key());
    }
    for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
        rows.insert(it.key());
    }

    for (int row : std::as_const(rows)) {
        animateRowOffset(row, targets.value(row, 0.0));
    }
    animatePlaceholder(placeholderYForDestination(destinationIndex));
    viewport()->update();
}

void TierListView::animateRowOffset(int row, qreal targetOffset) {
    const qreal currentOffset = m_reorderRowOffsets.value(row, 0.0);
    if (QVariantAnimation* existing = m_reorderRowAnimations.take(row)) {
        existing->stop();
        existing->deleteLater();
    }

    if (qFuzzyCompare(currentOffset + 1.0, targetOffset + 1.0)) {
        if (qFuzzyIsNull(targetOffset)) {
            m_reorderRowOffsets.remove(row);
        } else {
            m_reorderRowOffsets.insert(row, targetOffset);
        }
        viewport()->update();
        return;
    }

    auto* animation = new QVariantAnimation(this);
    m_reorderRowAnimations.insert(row, animation);
    animation->setDuration(kRowReorderAnimationMs);
    animation->setEasingCurve(QEasingCurve::OutQuint);
    animation->setStartValue(currentOffset);
    animation->setEndValue(targetOffset);
    connect(animation, &QVariantAnimation::valueChanged, this, [this, row](const QVariant& value) {
        const qreal offset = value.toReal();
        if (qFuzzyIsNull(offset)) {
            m_reorderRowOffsets.remove(row);
        } else {
            m_reorderRowOffsets.insert(row, offset);
        }
        viewport()->update();
    });
    connect(animation, &QVariantAnimation::finished, this, [this, row, animation, targetOffset]() {
        if (m_reorderRowAnimations.value(row) == animation) {
            m_reorderRowAnimations.remove(row);
        }
        if (qFuzzyIsNull(targetOffset)) {
            m_reorderRowOffsets.remove(row);
        } else {
            m_reorderRowOffsets.insert(row, targetOffset);
        }
        animation->deleteLater();
        viewport()->update();
    });
    animation->start();
}

void TierListView::animatePlaceholder(qreal targetY) {
    if (m_placeholderAnimation) {
        m_placeholderAnimation->stop();
        m_placeholderAnimation->deleteLater();
        m_placeholderAnimation = nullptr;
    }

    if (qFuzzyCompare(m_placeholderY + 1.0, targetY + 1.0)) {
        m_placeholderY = targetY;
        viewport()->update();
        return;
    }

    m_placeholderAnimation = new QVariantAnimation(this);
    m_placeholderAnimation->setDuration(kRowReorderAnimationMs);
    m_placeholderAnimation->setEasingCurve(QEasingCurve::OutQuint);
    m_placeholderAnimation->setStartValue(m_placeholderY);
    m_placeholderAnimation->setEndValue(targetY);
    connect(m_placeholderAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_placeholderY = value.toReal();
        viewport()->update();
    });
    connect(m_placeholderAnimation, &QVariantAnimation::finished, this, [this, targetY]() {
        m_placeholderY = targetY;
        if (m_placeholderAnimation) {
            m_placeholderAnimation->deleteLater();
            m_placeholderAnimation = nullptr;
        }
        viewport()->update();
    });
    m_placeholderAnimation->start();
}

QHash<int, qreal> TierListView::targetOffsetsForDestination(int destinationIndex) const {
    QHash<int, qreal> offsets;
    TierListModel* model = tierModel();
    if (!model || m_reorderSourceRow < 0 || !m_reorderSourceRect.isValid()) {
        return offsets;
    }

    const int rows = model->rowCount();
    destinationIndex = qBound(0, destinationIndex, rows - 1);
    const qreal step = m_reorderSourceRect.height() + spacing();
    if (destinationIndex > m_reorderSourceRow) {
        for (int row = m_reorderSourceRow + 1; row <= destinationIndex; ++row) {
            offsets.insert(row, -step);
        }
    } else if (destinationIndex < m_reorderSourceRow) {
        for (int row = destinationIndex; row < m_reorderSourceRow; ++row) {
            offsets.insert(row, step);
        }
    }
    return offsets;
}

qreal TierListView::placeholderYForDestination(int destinationIndex) const {
    TierListModel* model = tierModel();
    if (!model || m_reorderSourceRow < 0 || !m_reorderSourceRect.isValid()) {
        return -1.0;
    }

    const int rows = model->rowCount();
    destinationIndex = qBound(0, destinationIndex, rows - 1);
    if (destinationIndex == m_reorderSourceRow) {
        return m_reorderSourceRect.top();
    }

    const QModelIndex target = model->index(destinationIndex, 0);
    const QRect targetRect = visualRect(target);
    if (!targetRect.isValid()) {
        return m_reorderSourceRect.top();
    }

    if (destinationIndex > m_reorderSourceRow) {
        return targetRect.bottom() + 1.0 - m_reorderSourceRect.height();
    }
    return targetRect.top();
}

void TierListView::stopReorderAnimations() {
    for (QVariantAnimation* animation : std::as_const(m_reorderRowAnimations)) {
        if (animation) {
            animation->stop();
            animation->deleteLater();
        }
    }
    m_reorderRowAnimations.clear();
    if (m_placeholderAnimation) {
        m_placeholderAnimation->stop();
        m_placeholderAnimation->deleteLater();
        m_placeholderAnimation = nullptr;
    }
}

void TierListView::beginImageDragVisuals(const QString& imageId) {
    if (imageId.isEmpty()) {
        return;
    }
    if (m_imageDragActive && m_imageDragId == imageId) {
        return;
    }

    finishImageDragVisuals();
    m_imageDragActive = true;
    m_imageDragId = imageId;
    stopDockHoverAnimation();
    m_dockHoverProgress = 0.0;
    m_dockHoverRow = -1;
    m_dockHoverImageId.clear();
    m_imageDragSourceRow = -1;
    m_imageDropInsertionIndex = -1;
    m_imagePlaceholderRect = {};

    TierListModel* model = tierModel();
    TierListDelegate* delegate = tierDelegate();
    if (model && delegate) {
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            const QStringList imageIds = index.data(TierListModel::ImageIdsRole).toStringList();
            const int imageIndex = static_cast<int>(imageIds.indexOf(imageId));
            if (imageIndex < 0) {
                continue;
            }

            m_imageDragSourceRow = row;
            m_imageDropIndex = QPersistentModelIndex(index);
            m_imageDropInsertionIndex = imageIndex;
            m_imagePlaceholderRect = delegate->imageRectForId(index, visualRect(index), imageId);
            break;
        }
    }

    Logger::debug(QStringLiteral("tier.list.image.visual.begin imageId=%1 sourceRow=%2 sourceIndex=%3")
                      .arg(m_imageDragId)
                      .arg(m_imageDragSourceRow)
                      .arg(m_imageDropInsertionIndex));
    viewport()->update();
}

void TierListView::finishImageDragVisuals() {
    if (!m_imageDragActive && m_imageTileOffsets.isEmpty() && m_imagePlaceholderRect.isNull()) {
        return;
    }

    stopImageAnimations();
    m_imageTileOffsets.clear();
    m_imageDragActive = false;
    m_imageDragId.clear();
    m_imageDragSourceRow = -1;
    m_imagePreviewTargetRow = -1;
    m_imageDropInsertionIndex = -1;
    m_imagePlaceholderRect = {};
    clearImageDropState();
    refreshLayoutMetrics();
    viewport()->update();
}

void TierListView::updateImageDropIntentAt(const QPoint& viewportPoint) {
    if (!m_imageDragActive) {
        return;
    }

    QModelIndex target = imageDropIndexForPosition(viewportPoint);
    applyImagePreviewTargetRow(target.isValid() ? target.row() : -1);

    // Re-read the row after preview metrics are applied. A cross-row drag may change row heights,
    // so the cursor can legitimately land in a neighboring row after the first projection.
    QModelIndex adjustedTarget = imageDropIndexForPosition(viewportPoint);
    if (adjustedTarget.isValid() != target.isValid() ||
        (adjustedTarget.isValid() && adjustedTarget.row() != target.row())) {
        target = adjustedTarget;
        applyImagePreviewTargetRow(target.isValid() ? target.row() : -1);
    }

    if (!target.isValid()) {
        updateImageDropIntent({}, -1);
        return;
    }

    updateImageDropIntent(target, imageInsertionIndexForPosition(target, viewportPoint));
}

void TierListView::updateImageDropIntent(const QModelIndex& target, int insertionIndex) {
    if (!m_imageDragActive) {
        return;
    }

    if (!target.isValid()) {
        applyImagePreviewTargetRow(-1);
        if (m_imageDragSourceRow >= 0) {
            TierListModel* model = tierModel();
            if (model) {
                const QModelIndex source = model->index(m_imageDragSourceRow, 0);
                const QStringList imageIds = source.data(TierListModel::ImageIdsRole).toStringList();
                updateImageDropIntent(source, static_cast<int>(imageIds.indexOf(m_imageDragId)));
            }
        } else {
            clearImageDropState();
            animateImagePlaceholder({});
        }
        return;
    }

    applyImagePreviewTargetRow(target.row());

    QRectF placeholderRect;
    const QHash<QString, QPointF> targets = targetImageOffsets(target, insertionIndex, &placeholderRect);
    const bool sameIntent = m_imageDropIndex == target && m_imageDropInsertionIndex == insertionIndex &&
                            m_imagePlaceholderRect.isValid() &&
                            qAbs(m_imagePlaceholderRect.x() - placeholderRect.x()) < 0.5 &&
                            qAbs(m_imagePlaceholderRect.y() - placeholderRect.y()) < 0.5 &&
                            qAbs(m_imagePlaceholderRect.width() - placeholderRect.width()) < 0.5 &&
                            qAbs(m_imagePlaceholderRect.height() - placeholderRect.height()) < 0.5;
    if (sameIntent) {
        return;
    }
    QSet<QString> imageIds;
    for (auto it = m_imageTileOffsets.cbegin(); it != m_imageTileOffsets.cend(); ++it) {
        imageIds.insert(it.key());
    }
    for (auto it = m_imageTileAnimations.cbegin(); it != m_imageTileAnimations.cend(); ++it) {
        imageIds.insert(it.key());
    }
    for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
        imageIds.insert(it.key());
    }

    for (const QString& imageId : std::as_const(imageIds)) {
        animateImageOffset(imageId, targets.value(imageId, QPointF()));
    }
    animateImagePlaceholder(placeholderRect);
    m_imageDropIndex = QPersistentModelIndex(target);
    m_imageDropInsertionIndex = insertionIndex;
    Logger::debug(QStringLiteral("tier.list.image.drag.intent imageId=%1 row=%2 index=%3 placeholder=(%4,%5,%6,%7)")
                      .arg(m_imageDragId)
                      .arg(target.row())
                      .arg(insertionIndex)
                      .arg(qRound(placeholderRect.x()))
                      .arg(qRound(placeholderRect.y()))
                      .arg(qRound(placeholderRect.width()))
                      .arg(qRound(placeholderRect.height())));
    viewport()->update();
}

void TierListView::applyImagePreviewTargetRow(int targetRow) {
    TierListModel* model = tierModel();
    if (!m_imageDragActive || !model) {
        targetRow = -1;
    }
    if (model && targetRow >= model->rowCount()) {
        targetRow = -1;
    }
    if (m_imagePreviewTargetRow == targetRow) {
        return;
    }

    const int previousTarget = m_imagePreviewTargetRow;
    m_imagePreviewTargetRow = targetRow;
    Logger::debug(QStringLiteral("tier.list.image.preview.metrics imageId=%1 previousTarget=%2 target=%3")
                      .arg(m_imageDragId)
                      .arg(previousTarget)
                      .arg(m_imagePreviewTargetRow));
    refreshLayoutMetrics();
}

int TierListView::previewImageCountForRow(int row) const {
    TierListModel* model = tierModel();
    const TierRow* tierRow = model ? model->tierRowAt(row) : nullptr;
    if (!tierRow) {
        return 0;
    }

    int count = static_cast<int>(tierRow->imageIds.size());
    if (!m_imageDragActive) {
        return count;
    }

    if (m_imageDragSourceRow >= 0 && row == m_imageDragSourceRow &&
        m_imagePreviewTargetRow != m_imageDragSourceRow) {
        --count;
    }
    if (m_imagePreviewTargetRow == row && m_imageDragSourceRow != row) {
        ++count;
    }
    return qMax(0, count);
}

int TierListView::imageInsertionIndexForPosition(const QModelIndex& target, const QPoint& point) const {
    TierListDelegate* delegate = tierDelegate();
    if (!delegate || !target.isValid()) {
        return -1;
    }

    const QStringList imageIds = target.data(TierListModel::ImageIdsRole).toStringList();
    const int sourceIndex = static_cast<int>(imageIds.indexOf(m_imageDragId));
    int visualCount = static_cast<int>(imageIds.size());
    if (sourceIndex >= 0) {
        --visualCount;
    }

    int visualInsertion = delegate->insertionIndexForPosition(target, visualRect(target), point,
                                                             qMax(0, visualCount));
    if (target.row() == m_imageDragSourceRow && sourceIndex >= 0 && visualInsertion > sourceIndex) {
        ++visualInsertion;
    }
    return qBound(0, visualInsertion, static_cast<int>(imageIds.size()));
}

void TierListView::animateImageOffset(const QString& imageId, const QPointF& targetOffset) {
    const QPointF currentOffset = m_imageTileOffsets.value(imageId, QPointF());
    if (QVariantAnimation* running = m_imageTileAnimations.value(imageId)) {
        const QPointF runningTarget = running->endValue().toPointF();
        if (qAbs(runningTarget.x() - targetOffset.x()) < 0.5 &&
            qAbs(runningTarget.y() - targetOffset.y()) < 0.5) {
            return;
        }
    }
    if (QVariantAnimation* existing = m_imageTileAnimations.take(imageId)) {
        existing->stop();
        existing->deleteLater();
    }

    const bool sameTarget = qAbs(currentOffset.x() - targetOffset.x()) < 0.5 &&
                            qAbs(currentOffset.y() - targetOffset.y()) < 0.5;
    if (sameTarget) {
        if (qAbs(targetOffset.x()) < 0.5 && qAbs(targetOffset.y()) < 0.5) {
            m_imageTileOffsets.remove(imageId);
        } else {
            m_imageTileOffsets.insert(imageId, targetOffset);
        }
        viewport()->update();
        return;
    }

    auto* animation = new QVariantAnimation(this);
    m_imageTileAnimations.insert(imageId, animation);
    animation->setDuration(kImageReorderAnimationMs);
    animation->setEasingCurve(QEasingCurve::OutQuint);
    animation->setStartValue(currentOffset);
    animation->setEndValue(targetOffset);
    connect(animation, &QVariantAnimation::valueChanged, this, [this, imageId](const QVariant& value) {
        const QPointF offset = value.toPointF();
        if (qAbs(offset.x()) < 0.5 && qAbs(offset.y()) < 0.5) {
            m_imageTileOffsets.remove(imageId);
        } else {
            m_imageTileOffsets.insert(imageId, offset);
        }
        viewport()->update();
    });
    connect(animation, &QVariantAnimation::finished, this, [this, imageId, animation, targetOffset]() {
        if (m_imageTileAnimations.value(imageId) == animation) {
            m_imageTileAnimations.remove(imageId);
        }
        if (qAbs(targetOffset.x()) < 0.5 && qAbs(targetOffset.y()) < 0.5) {
            m_imageTileOffsets.remove(imageId);
        } else {
            m_imageTileOffsets.insert(imageId, targetOffset);
        }
        animation->deleteLater();
        viewport()->update();
    });
    animation->start();
}

void TierListView::animateImagePlaceholder(const QRectF& targetRect) {
    if (m_imagePlaceholderAnimation && targetRect.isValid()) {
        const QRectF runningTarget = m_imagePlaceholderAnimation->endValue().toRectF();
        if (qAbs(runningTarget.x() - targetRect.x()) < 0.5 &&
            qAbs(runningTarget.y() - targetRect.y()) < 0.5 &&
            qAbs(runningTarget.width() - targetRect.width()) < 0.5 &&
            qAbs(runningTarget.height() - targetRect.height()) < 0.5) {
            return;
        }
    }
    if (m_imagePlaceholderAnimation) {
        m_imagePlaceholderAnimation->stop();
        m_imagePlaceholderAnimation->deleteLater();
        m_imagePlaceholderAnimation = nullptr;
    }

    if (!targetRect.isValid()) {
        m_imagePlaceholderRect = {};
        viewport()->update();
        return;
    }

    if (!m_imagePlaceholderRect.isValid()) {
        m_imagePlaceholderRect = targetRect;
        viewport()->update();
        return;
    }

    const bool sameTarget = qAbs(m_imagePlaceholderRect.x() - targetRect.x()) < 0.5 &&
                            qAbs(m_imagePlaceholderRect.y() - targetRect.y()) < 0.5 &&
                            qAbs(m_imagePlaceholderRect.width() - targetRect.width()) < 0.5 &&
                            qAbs(m_imagePlaceholderRect.height() - targetRect.height()) < 0.5;
    if (sameTarget) {
        m_imagePlaceholderRect = targetRect;
        viewport()->update();
        return;
    }

    m_imagePlaceholderAnimation = new QVariantAnimation(this);
    m_imagePlaceholderAnimation->setDuration(kImageReorderAnimationMs);
    m_imagePlaceholderAnimation->setEasingCurve(QEasingCurve::OutQuint);
    m_imagePlaceholderAnimation->setStartValue(m_imagePlaceholderRect);
    m_imagePlaceholderAnimation->setEndValue(targetRect);
    connect(m_imagePlaceholderAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_imagePlaceholderRect = value.toRectF();
        viewport()->update();
    });
    connect(m_imagePlaceholderAnimation, &QVariantAnimation::finished, this, [this, targetRect]() {
        m_imagePlaceholderRect = targetRect;
        if (m_imagePlaceholderAnimation) {
            m_imagePlaceholderAnimation->deleteLater();
            m_imagePlaceholderAnimation = nullptr;
        }
        viewport()->update();
    });
    m_imagePlaceholderAnimation->start();
}

void TierListView::stopImageAnimations() {
    for (QVariantAnimation* animation : std::as_const(m_imageTileAnimations)) {
        if (animation) {
            animation->stop();
            animation->deleteLater();
        }
    }
    m_imageTileAnimations.clear();
    if (m_imagePlaceholderAnimation) {
        m_imagePlaceholderAnimation->stop();
        m_imagePlaceholderAnimation->deleteLater();
        m_imagePlaceholderAnimation = nullptr;
    }
}

QHash<QString, QPointF> TierListView::targetImageOffsets(const QModelIndex& target, int insertionIndex,
                                                         QRectF* placeholderRect) const {
    QHash<QString, QPointF> offsets;
    if (placeholderRect) {
        *placeholderRect = {};
    }

    TierListModel* model = tierModel();
    TierListDelegate* delegate = tierDelegate();
    if (!model || !delegate || !target.isValid()) {
        return offsets;
    }

    const int targetRow = target.row();
    const int rows = model->rowCount();
    for (int row = 0; row < rows; ++row) {
        if (row != targetRow && row != m_imageDragSourceRow) {
            continue;
        }

        const QModelIndex index = model->index(row, 0);
        const QRect rowRect = visualRect(index);
        const QStringList originalIds = index.data(TierListModel::ImageIdsRole).toStringList();
        const QVector<QRect> originalRects = delegate->tileRects(index, rowRect);
        QHash<QString, QRect> originalRectById;
        for (int i = 0; i < originalIds.size() && i < originalRects.size(); ++i) {
            originalRectById.insert(originalIds.at(i), originalRects.at(i));
        }

        QStringList visualIds = originalIds;
        const int originalSourceIndex = static_cast<int>(visualIds.indexOf(m_imageDragId));
        if (originalSourceIndex >= 0) {
            visualIds.removeAt(originalSourceIndex);
        }

        int placeholderIndex = -1;
        if (row == targetRow) {
            placeholderIndex = insertionIndex;
            if (row == m_imageDragSourceRow && originalSourceIndex >= 0 && originalSourceIndex < placeholderIndex) {
                --placeholderIndex;
            }
            placeholderIndex = qBound(0, placeholderIndex, static_cast<int>(visualIds.size()));
            visualIds.insert(placeholderIndex, QString());
        }

        const QVector<QRect> targetRects =
            delegate->tileRectsForCount(index, rowRect, static_cast<int>(visualIds.size()));
        for (int i = 0; i < visualIds.size() && i < targetRects.size(); ++i) {
            const QString id = visualIds.at(i);
            if (id.isEmpty()) {
                if (placeholderRect) {
                    *placeholderRect = targetRects.at(i);
                }
                continue;
            }
            const QRect original = originalRectById.value(id);
            if (original.isValid()) {
                offsets.insert(id, QPointF(targetRects.at(i).topLeft() - original.topLeft()));
            }
        }
    }

    return offsets;
}

void TierListView::startRowDrag() {
    if (!m_pressedIndex.isValid() || m_pressedRowId.isEmpty()) {
        Logger::warn(QStringLiteral("tier.list.row.drag.start rejected invalid pressed index or row id"));
        resetPressState();
        return;
    }

    const QString draggedRowId = m_pressedRowId;
    const int sourceRow = m_pressedIndex.row();
    auto* drag = new QDrag(this);
    drag->setMimeData(TierDragController::createRowMimeData(draggedRowId));
    const QRect rowRect = visualRect(m_pressedIndex);
    QPixmap pixmap = viewport()->grab(rowRect);
    if (!pixmap.isNull()) {
        drag->setPixmap(pixmap);
        drag->setHotSpot(m_pressPosition - rowRect.topLeft());
    }

    m_rowDropId = draggedRowId;
    m_rowDropIndex = m_pressedIndex.row();
    m_rowDropActive = true;
    m_rowDragCommitted = false;
    stopDockHoverAnimation();
    m_dockHoverProgress = 0.0;
    m_dockHoverRow = -1;
    m_dockHoverImageId.clear();
    beginRowReorderVisuals(m_pressedIndex);
    resetPressState();
    setCursor(Qt::ClosedHandCursor);
    Logger::info(QStringLiteral("tier.list.row.drag.start rowId=%1 sourceRow=%2").arg(draggedRowId).arg(sourceRow));

    QWidget* catchment = createRowDragCatchment();
    const Qt::DropAction result = drag->exec(Qt::MoveAction);
    if (catchment) {
        catchment->hide();
        catchment->deleteLater();
    }

    Logger::info(QStringLiteral("tier.list.row.drag.finish rowId=%1 result=%2 committed=%3 destination=%4")
                     .arg(draggedRowId)
                     .arg(static_cast<int>(result))
                     .arg(m_rowDragCommitted)
                     .arg(m_rowDropIndex));
    finishRowReorderVisuals(m_rowDragCommitted);
    clearDropState();
    unsetCursor();
    viewport()->update();
}

void TierListView::startImageDrag() {
    if (!m_pressedIndex.isValid() || m_pressedImageId.isEmpty()) {
        Logger::warn(QStringLiteral("tier.list.image.drag.start rejected invalid pressed index or image id"));
        resetPressState();
        return;
    }

    const QString imageId = m_pressedImageId;
    auto* drag = new QDrag(this);
    drag->setMimeData(TierDragController::createMimeData(imageId));
    const QRect imageRect = viewportImageRect(m_pressedIndex, imageId);
    QPixmap pixmap = viewport()->grab(imageRect);
    if (!pixmap.isNull()) {
        drag->setPixmap(pixmap);
        drag->setHotSpot(m_pressPosition - imageRect.topLeft());
    }

    beginImageDragVisuals(imageId);
    resetPressState();
    setCursor(Qt::ClosedHandCursor);
    Logger::info(QStringLiteral("tier.list.image.drag.start imageId=%1").arg(imageId));
    const Qt::DropAction result = drag->exec(Qt::MoveAction);
    Logger::info(QStringLiteral("tier.list.image.drag.finish imageId=%1 result=%2")
                     .arg(imageId)
                     .arg(static_cast<int>(result)));
    finishImageDragVisuals();
    unsetCursor();
}

QWidget* TierListView::createRowDragCatchment() {
    QWidget* host = window();
    if (!host || !viewport()) {
        return nullptr;
    }

    const QRect geometry = rowDragCatchmentGeometry(host);
    if (!geometry.isValid()) {
        return nullptr;
    }

    auto* catchment = new RowDragCatchment(
        host,
        [this](const QPoint& globalPoint) {
            updateRowDropIntent(viewportPointFromGlobal(globalPoint), m_rowDropId);
        },
        [this](const QPoint& globalPoint) {
            const QPoint viewportPoint = viewportPointFromGlobal(globalPoint);
            updateRowDropIntent(viewportPoint, m_rowDropId);
            return commitRowDrop(m_rowDropId, m_rowDropIndex, "catchmentDrop");
        });
    catchment->setObjectName(QStringLiteral("tierRowDragCatchment"));
    catchment->setGeometry(geometry);
    catchment->raise();
    catchment->show();
    Logger::debug(QStringLiteral("tier.list.row.catchment geometry=(%1,%2,%3,%4)")
                      .arg(geometry.x())
                      .arg(geometry.y())
                      .arg(geometry.width())
                      .arg(geometry.height()));
    return catchment;
}

QRect TierListView::rowDragCatchmentGeometry(QWidget* host) const {
    if (!host || !viewport()) {
        return {};
    }

    const QRect viewportGlobal(viewport()->mapToGlobal(QPoint(0, 0)), viewport()->size());
    QRect local(host->mapFromGlobal(viewportGlobal.topLeft()), viewportGlobal.size());
    local.setTop(host->rect().top());
    local.setBottom(host->rect().bottom());
    return local.intersected(host->rect());
}

QPoint TierListView::viewportPointFromGlobal(const QPoint& globalPoint) const {
    QPoint point = viewport()->mapFromGlobal(globalPoint);
    const QRect bounds = viewport()->rect();
    if (bounds.isValid()) {
        point.setX(qBound(bounds.left(), point.x(), bounds.right()));
        point.setY(qBound(bounds.top(), point.y(), bounds.bottom()));
    }
    return point;
}

void TierListView::updateRowDropIntent(const QPoint& viewportPoint, const QString& rowId) {
    const int nextIndex = rowDropIndexForPosition(viewportPoint, rowId);
    if (m_rowDropActive && m_rowDropIndex == nextIndex && m_rowDropId == rowId) {
        return;
    }

    m_rowDropId = rowId;
    m_rowDropIndex = nextIndex;
    m_rowDropActive = !rowId.isEmpty() && nextIndex >= 0;
    animateReorderToIndex(nextIndex);
    Logger::debug(QStringLiteral("tier.list.row.drag.intent rowId=%1 destination=%2 pos=(%3,%4)")
                      .arg(rowId)
                      .arg(nextIndex)
                      .arg(viewportPoint.x())
                      .arg(viewportPoint.y()));
    viewport()->update();
}

bool TierListView::commitRowDrop(const QString& rowId, int destinationIndex, const char* reason) {
    if (m_rowDragCommitted) {
        Logger::debug(QStringLiteral("tier.list.row.drop ignored already committed rowId=%1 reason=%2")
                          .arg(rowId, QString::fromUtf8(reason ? reason : "unknown")));
        return true;
    }
    if (rowId.isEmpty() || destinationIndex < 0) {
        Logger::warn(QStringLiteral("tier.list.row.drop rejected rowId=%1 destination=%2 reason=%3")
                         .arg(rowId)
                         .arg(destinationIndex)
                         .arg(QString::fromUtf8(reason ? reason : "unknown")));
        return false;
    }

    Logger::info(QStringLiteral("tier.list.row.drop.commit rowId=%1 destination=%2 reason=%3")
                     .arg(rowId)
                     .arg(destinationIndex)
                     .arg(QString::fromUtf8(reason ? reason : "unknown")));
    m_rowDragCommitted = true;
    emit rowMovedToIndex(rowId, destinationIndex);
    return true;
}

void TierListView::clearDropState() {
    m_rowDropActive = false;
    m_rowDropIndex = -1;
    m_rowDropId.clear();
    clearImageDropState();
}

void TierListView::clearImageDropState() {
    m_imageDropIndex = QPersistentModelIndex();
    m_imageDropInsertionIndex = -1;
}

void TierListView::updateDockHover(const QPoint& viewportPoint) {
    if (m_imageDragActive || m_rowDropActive || m_reorderSourceRow >= 0) {
        animateDockHover(0.0);
        return;
    }

    TierListDelegate* delegate = tierDelegate();
    const QModelIndex index = indexAt(viewportPoint);
    QString imageId;
    if (delegate && index.isValid()) {
        imageId = delegate->imageIdAt(index, visualRect(index), viewportPoint);
    }

    m_dockHoverPosition = viewportPoint;
    if (imageId.isEmpty()) {
        animateDockHover(0.0);
        return;
    }

    if (m_dockHoverImageId != imageId || m_dockHoverRow != index.row()) {
        m_dockHoverImageId = imageId;
        m_dockHoverRow = index.row();
        Logger::debug(QStringLiteral("tier.list.image.hover imageId=%1 row=%2 pos=(%3,%4)")
                          .arg(imageId)
                          .arg(index.row())
                          .arg(viewportPoint.x())
                          .arg(viewportPoint.y()));
    }
    animateDockHover(1.0);
    viewport()->update();
}

void TierListView::animateDockHover(qreal targetProgress) {
    targetProgress = qBound<qreal>(0.0, targetProgress, 1.0);
    if (m_dockHoverAnimation &&
        qAbs(m_dockHoverAnimation->endValue().toReal() - targetProgress) < 0.01) {
        viewport()->update();
        return;
    }
    if (qAbs(m_dockHoverProgress - targetProgress) < 0.01 &&
        ((targetProgress > 0.0 && !m_dockHoverImageId.isEmpty()) || targetProgress <= 0.0)) {
        if (targetProgress <= 0.0 && m_dockHoverProgress <= 0.001) {
            m_dockHoverImageId.clear();
            m_dockHoverRow = -1;
        }
        viewport()->update();
        return;
    }

    stopDockHoverAnimation();
    auto* animation = new QVariantAnimation(this);
    m_dockHoverAnimation = animation;
    animation->setDuration(targetProgress > m_dockHoverProgress ? 150 : 180);
    animation->setEasingCurve(targetProgress > m_dockHoverProgress ? QEasingCurve::OutCubic
                                                                    : QEasingCurve::OutQuint);
    animation->setStartValue(m_dockHoverProgress);
    animation->setEndValue(targetProgress);
    connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_dockHoverProgress = value.toReal();
        viewport()->update();
    });
    connect(animation, &QVariantAnimation::finished, this, [this, animation, targetProgress]() {
        if (m_dockHoverAnimation == animation) {
            m_dockHoverAnimation = nullptr;
        }
        m_dockHoverProgress = targetProgress;
        if (targetProgress <= 0.0) {
            m_dockHoverImageId.clear();
            m_dockHoverRow = -1;
        }
        animation->deleteLater();
        viewport()->update();
    });
    animation->start();
}

void TierListView::stopDockHoverAnimation() {
    if (m_dockHoverAnimation) {
        m_dockHoverAnimation->stop();
        m_dockHoverAnimation->deleteLater();
        m_dockHoverAnimation = nullptr;
    }
}

int TierListView::rowDropIndexForPosition(const QPoint& point, const QString& rowId) const {
    TierListModel* model = tierModel();
    if (!model || model->rowCount() <= 0 || rowId.isEmpty()) {
        return -1;
    }

    const int rows = model->rowCount();
    const int sourceRow = model->rowForId(rowId);
    if (sourceRow < 0) {
        return -1;
    }

    const QRect sourceRect = visualRect(model->index(sourceRow, 0));
    if (!sourceRect.isValid()) {
        return qBound(0, sourceRow, rows - 1);
    }

    // Mouse position is projected onto the stable layout with the source row removed.
    // Animated row offsets are paint-only and must not participate in hit testing.
    int insertion = 0;
    for (int row = 0; row < rows; ++row) {
        if (row == sourceRow) {
            continue;
        }
        QRect candidate = visualRect(model->index(row, 0));
        if (!candidate.isValid()) {
            continue;
        }
        if (row > sourceRow) {
            candidate.translate(0, -sourceRect.height());
        }
        if (point.y() < candidate.center().y()) {
            return qBound(0, insertion, rows - 1);
        }
        ++insertion;
    }
    return rows - 1;
}

QModelIndex TierListView::imageDropIndexForPosition(const QPoint& point) const {
    TierListModel* model = tierModel();
    if (!model || model->rowCount() <= 0) {
        return {};
    }

    // The board has no vertical scrolling; y-only row targeting keeps drops stable while
    // preview row heights animate underneath the cursor.
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        const QRect rect = visualRect(index);
        if (rect.isValid() && point.y() >= rect.top() && point.y() <= rect.bottom()) {
            return index;
        }
    }

    const QModelIndex first = model->index(0, 0);
    const QModelIndex last = model->index(model->rowCount() - 1, 0);
    if (visualRect(first).isValid() && point.y() < visualRect(first).top()) {
        return first;
    }
    if (visualRect(last).isValid() && point.y() > visualRect(last).bottom()) {
        return last;
    }
    return {};
}

QRect TierListView::animatedVisualRect(const QModelIndex& index) const {
    QRect rect = visualRect(index);
    if (rect.isValid()) {
        rect.translate(0, qRound(visualOffsetForIndex(index)));
    }
    return rect;
}

QModelIndex TierListView::animatedIndexAt(const QPoint& point) const {
    TierListModel* model = tierModel();
    if (m_reorderSourceRow < 0 || !model) {
        return indexAt(point);
    }

    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        if (!index.isValid() || isReorderSourceIndex(index)) {
            continue;
        }
        const QRect rect = animatedVisualRect(index);
        if (rect.isValid() && rect.contains(point)) {
            return index;
        }
    }
    return {};
}

QRect TierListView::viewportImageRect(const QModelIndex& index, const QString& imageId) const {
    if (TierListDelegate* delegate = tierDelegate()) {
        return delegate->imageRectForId(index, visualRect(index), imageId);
    }
    return {};
}

void TierListView::paintCanvasBackground(QPainter* painter) {
    if (!painter) {
        return;
    }

    const QString backgroundPath = resolvedCanvasBackgroundPath();
    if (backgroundPath.isEmpty()) {
        return;
    }

    const QPixmap pixmap = canvasBackgroundPixmap(backgroundPath);
    if (pixmap.isNull()) {
        return;
    }
    TierListModel* model = tierModel();
    const TierProject* project = model ? model->project() : nullptr;
    const qreal backgroundVisibility = canvasBackgroundVisibility(project);

    const QRectF bounds = QRectF(viewport()->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (!bounds.isValid()) {
        return;
    }

    const int labelWidth = tierDelegate() ? tierDelegate()->labelWidth()
                                          : TierListDelegate::minimumLabelWidth();
    const QRectF contentBounds = bounds.adjusted(labelWidth, 0.0, 0.0, 0.0);
    if (!contentBounds.isValid()) {
        return;
    }

    QPainterPath outerClip;
    outerClip.addRoundedRect(bounds, TierListDelegate::outerRadius(), TierListDelegate::outerRadius());
    QPainterPath contentClip;
    contentClip.addRect(contentBounds);
    painter->save();
    painter->setClipPath(outerClip.intersected(contentClip));
    painter->fillRect(contentBounds, palette().color(QPalette::Base));

    const QSizeF targetSize = contentBounds.size();
    const QSize sourceSize = pixmap.size();
    const qreal targetRatio = targetSize.width() / qMax<qreal>(1.0, targetSize.height());
    const qreal sourceRatio = static_cast<qreal>(sourceSize.width()) / qMax(1, sourceSize.height());
    QRect sourceRect;
    if (sourceRatio > targetRatio) {
        const int cropWidth = qRound(sourceSize.height() * targetRatio);
        sourceRect = QRect((sourceSize.width() - cropWidth) / 2, 0, cropWidth, sourceSize.height());
    } else {
        const int cropHeight = qRound(sourceSize.width() / targetRatio);
        sourceRect = QRect(0, (sourceSize.height() - cropHeight) / 2, sourceSize.width(), cropHeight);
    }
    // Crop from the original cached pixmap on every paint so resize changes never distort the ratio.
    painter->setOpacity(backgroundVisibility);
    painter->drawPixmap(contentBounds, pixmap, sourceRect);
    painter->restore();
}

QString TierListView::resolvedCanvasBackgroundPath() const {
    TierListModel* model = tierModel();
    const TierProject* project = model ? model->project() : nullptr;
    if (!project) {
        return {};
    }

    const QString storedPath = project->canvas.value(QStringLiteral("backgroundImagePath")).toString();
    if (storedPath.isEmpty()) {
        return {};
    }
    const QFileInfo info(storedPath);
    if (info.isAbsolute()) {
        return info.absoluteFilePath();
    }
    if (!project->filePath.isEmpty()) {
        return QDir(QFileInfo(project->filePath).absolutePath()).filePath(storedPath);
    }
    return storedPath;
}

QPixmap TierListView::canvasBackgroundPixmap(const QString& path) {
    if (path.isEmpty()) {
        m_canvasBackgroundCachePath.clear();
        m_canvasBackgroundCache = {};
        return {};
    }
    if (m_canvasBackgroundCachePath != path) {
        m_canvasBackgroundCachePath = path;
        m_canvasBackgroundCache = QPixmap(path);
        Logger::debug(QStringLiteral("tier.list.background.cache path=\"%1\" valid=%2")
                          .arg(path)
                          .arg(!m_canvasBackgroundCache.isNull()));
    }
    return m_canvasBackgroundCache;
}

bool TierListView::acceptsTierDrag(const QMimeData* mimeData) const {
    return mimeData && (mimeData->hasFormat(TierDragController::rowMimeType()) ||
                        mimeData->hasFormat(TierDragController::imageMimeType()));
}

} // namespace tlm
