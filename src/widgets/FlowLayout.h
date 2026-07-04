#pragma once

#include <QLayout>
#include <QStyle>
#include <QVector>

class QWidget;

namespace tlm {

/** Simple wrapping layout used for compact tile groups. */
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;
    void insertWidget(int index, QWidget* widget);
    void moveWidget(QWidget* widget, int index);
    QVector<QRect> slotsForCount(const QRect& rect, int itemCount, const QSize& itemSize) const;

private:
    void insertItem(int index, QLayoutItem* item);
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric metric) const;

    QList<QLayoutItem*> m_items;
    int m_hSpace;
    int m_vSpace;
};

} // namespace tlm
