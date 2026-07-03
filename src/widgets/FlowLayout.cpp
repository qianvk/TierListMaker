#include "widgets/FlowLayout.h"

#include <QWidget>
#include <QWidgetItem>

namespace tlm {

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout() {
    QLayoutItem* item = nullptr;
    while ((item = takeAt(0)) != nullptr) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem* item) {
    m_items.append(item);
}

int FlowLayout::horizontalSpacing() const {
    return m_hSpace >= 0 ? m_hSpace : smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const {
    return m_vSpace >= 0 ? m_vSpace : smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

Qt::Orientations FlowLayout::expandingDirections() const {
    return {};
}

bool FlowLayout::hasHeightForWidth() const {
    return true;
}

int FlowLayout::heightForWidth(int width) const {
    return doLayout(QRect(0, 0, width, 0), true);
}

int FlowLayout::count() const {
    return static_cast<int>(m_items.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const {
    return m_items.value(index);
}

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (const QLayoutItem* item : m_items) {
        size = size.expandedTo(item->minimumSize());
    }
    QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

void FlowLayout::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const {
    return minimumSize();
}

QLayoutItem* FlowLayout::takeAt(int index) {
    if (index < 0 || index >= m_items.size()) {
        return nullptr;
    }
    return m_items.takeAt(index);
}

void FlowLayout::insertWidget(int index, QWidget* widget) {
    if (!widget) {
        return;
    }
    addChildWidget(widget);
    insertItem(index, new QWidgetItem(widget));
}

void FlowLayout::moveWidget(QWidget* widget, int index) {
    if (!widget) {
        return;
    }
    for (int i = 0; i < m_items.size(); ++i) {
        QLayoutItem* item = m_items.at(i);
        if (item && item->widget() == widget) {
            m_items.removeAt(i);
            if (i < index) {
                --index;
            }
            insertItem(index, item);
            return;
        }
    }
    insertWidget(index, widget);
}

QVector<QRect> FlowLayout::slotsForCount(const QRect& rect, int itemCount, const QSize& itemSize) const {
    QVector<QRect> slotRects;
    slotRects.reserve(qMax(0, itemCount));
    if (itemCount <= 0 || itemSize.isEmpty()) {
        return slotRects;
    }

    const QMargins margins = contentsMargins();
    const QRect effective = rect.adjusted(margins.left(), margins.top(),
                                          -margins.right(), -margins.bottom());
    const int spaceX = horizontalSpacing();
    const int spaceY = verticalSpacing();
    int x = effective.x();
    int y = effective.y();
    int lineHeight = 0;

    for (int index = 0; index < itemCount; ++index) {
        const int nextX = x + itemSize.width() + spaceX;
        if (nextX - spaceX > effective.right() && lineHeight > 0) {
            x = effective.x();
            y += lineHeight + spaceY;
            lineHeight = 0;
        }
        slotRects.append(QRect(QPoint(x, y), itemSize));
        x += itemSize.width() + spaceX;
        lineHeight = qMax(lineHeight, itemSize.height());
    }
    return slotRects;
}

void FlowLayout::insertItem(int index, QLayoutItem* item) {
    if (!item) {
        return;
    }
    m_items.insert(qBound(0, index, static_cast<int>(m_items.size())), item);
    invalidate();
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const {
    const QMargins margins = contentsMargins();
    QRect effective = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
    int x = effective.x();
    int y = effective.y();
    int lineHeight = 0;

    for (QLayoutItem* item : m_items) {
        if (!item || item->isEmpty()) {
            continue;
        }
        const int spaceX = horizontalSpacing();
        const int spaceY = verticalSpacing();
        const int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effective.right() && lineHeight > 0) {
            x = effective.x();
            y += lineHeight + spaceY;
            lineHeight = 0;
        }
        if (!testOnly) {
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
        }
        x += item->sizeHint().width() + spaceX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }
    return y + lineHeight - rect.y() + margins.bottom();
}

int FlowLayout::smartSpacing(QStyle::PixelMetric metric) const {
    QObject* parentObject = parent();
    if (!parentObject) {
        return -1;
    }
    if (parentObject->isWidgetType()) {
        auto* parentWidget = static_cast<QWidget*>(parentObject);
        return parentWidget->style()->pixelMetric(metric, nullptr, parentWidget);
    }
    return static_cast<QLayout*>(parentObject)->spacing();
}

} // namespace tlm
