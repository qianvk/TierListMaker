#include "export/TierListExporter.h"

#include <QFileInfo>
#include <QFontMetrics>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>

#include <algorithm>

namespace tlm {

namespace {
constexpr int kBaseWidth = 1200;
constexpr int kMargin = 28;
constexpr int kGap = 8;
constexpr int kLabelWidth = 96;
constexpr int kTile = 82;
constexpr int kRadius = 12;

QImage loadPreviewImage(const QString& path, QSize target) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    if (source.isValid()) {
        reader.setScaledSize(source.scaled(target, Qt::KeepAspectRatio));
    }
    return reader.read();
}

void drawRoundedImage(QPainter& painter, const QRect& rect, const QImage& image) {
    painter.save();
    QPainterPath path;
    path.addRoundedRect(rect, 8, 8);
    painter.setClipPath(path);
    painter.fillRect(rect, QColor(QStringLiteral("#e9edf3")));
    if (!image.isNull()) {
        const QSize drawSize = image.size().scaled(rect.size(), Qt::KeepAspectRatio);
        const QRect imageRect(QPoint(rect.center().x() - drawSize.width() / 2,
                                     rect.center().y() - drawSize.height() / 2),
                              drawSize);
        painter.drawImage(imageRect, image);
    }
    painter.restore();
}
} // namespace

TierListExporter::TierListExporter(const AssetManager* assetManager, QObject* parent)
    : QObject(parent), m_assetManager(assetManager) {}

QImage TierListExporter::renderToImage(const TierProject& project, const ExportOptions& options) const {
    const int scale = std::clamp(options.scale, 1, 4);
    const int width = kBaseWidth * scale;
    const int margin = kMargin * scale;
    const int gap = kGap * scale;
    const int labelWidth = kLabelWidth * scale;
    const int tile = kTile * scale;
    const int contentWidth = width - margin * 2 - labelWidth - gap;
    const int columns = std::max(1, (contentWidth + gap) / (tile + gap));

    QFont titleFont;
    titleFont.setPointSize(22 * scale);
    titleFont.setBold(true);
    QFont labelFont;
    labelFont.setPointSize(24 * scale);
    labelFont.setBold(true);

    int height = margin;
    if (options.includeTitle) {
        height += 48 * scale;
    }
    for (const TierRow& row : project.rows) {
        const int imageCount = static_cast<int>(row.imageIds.size());
        const int lines = std::max(1, (imageCount + columns - 1) / columns);
        height += std::max(row.height * scale, lines * tile + (lines + 1) * gap) + gap;
    }
    height += margin;

    QImage image(width, height,
                 options.transparentBackground ? QImage::Format_ARGB32_Premultiplied
                                               : QImage::Format_RGB32);
    image.setDevicePixelRatio(1.0);
    image.fill(options.transparentBackground ? QColor(Qt::transparent) : options.backgroundColor);

    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform |
                           QPainter::TextAntialiasing);
    int y = margin;
    if (options.includeTitle) {
        painter.setFont(titleFont);
        painter.setPen(QColor(QStringLiteral("#1f2328")));
        painter.drawText(QRect(margin, y, width - margin * 2, 40 * scale),
                         Qt::AlignVCenter | Qt::AlignLeft, project.name);
        y += 48 * scale;
    }

    painter.setFont(labelFont);
    for (const TierRow& row : project.rows) {
        const int imageCount = static_cast<int>(row.imageIds.size());
        const int lines = std::max(1, (imageCount + columns - 1) / columns);
        const int rowHeight = std::max(row.height * scale, lines * tile + (lines + 1) * gap);
        const QRect labelRect(margin, y, labelWidth, rowHeight);
        const QRect contentRect(margin + labelWidth + gap, y, contentWidth, rowHeight);

        painter.setPen(Qt::NoPen);
        painter.setBrush(row.color);
        painter.drawRoundedRect(labelRect, kRadius * scale, kRadius * scale);
        painter.setPen(QColor(QStringLiteral("#111111")));
        painter.drawText(labelRect, Qt::AlignCenter, row.label);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#f3f5f8")));
        painter.drawRoundedRect(contentRect, kRadius * scale, kRadius * scale);

        for (int i = 0; i < row.imageIds.size(); ++i) {
            const TierImage* tierImage = project.imageById(row.imageIds[i]);
            if (!tierImage) {
                continue;
            }
            const int column = i % columns;
            const int line = i / columns;
            const QRect tileRect(contentRect.left() + gap + column * (tile + gap),
                                 contentRect.top() + gap + line * (tile + gap), tile, tile);
            const QString path =
                m_assetManager ? m_assetManager->resolvedImagePath(project, *tierImage) : tierImage->sourcePath;
            drawRoundedImage(painter, tileRect, loadPreviewImage(path, tileRect.size()));
        }
        y += rowHeight + gap;
    }

    return image;
}

Result<void> TierListExporter::exportProject(const TierProject& project, const QString& filePath,
                                             const ExportOptions& options) const {
    ExportOptions actual = options;
    if (QFileInfo(filePath).suffix().isEmpty()) {
        return Result<void>::failure(tr("The export path needs a file extension."));
    }
    actual.format = ExportOptions::formatFromSuffix(QFileInfo(filePath).suffix());

    const QImage image = renderToImage(project, actual);
    if (image.isNull()) {
        return Result<void>::failure(tr("Could not render the project for export."));
    }

    if (actual.format == ExportFormat::Pdf) {
        QPdfWriter writer(filePath);
        writer.setResolution(144);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setPageSize(QPageSize(QSizeF(image.width(), image.height()), QPageSize::Point));
        QPainter painter(&writer);
        painter.drawImage(QRect(0, 0, image.width(), image.height()), image);
        return Result<void>::success();
    }

    const char* format = actual.format == ExportFormat::Jpeg ? "JPG" : "PNG";
    if (!image.save(filePath, format, actual.format == ExportFormat::Jpeg ? 94 : -1)) {
        return Result<void>::failure(tr("Could not write the export file."), filePath);
    }
    return Result<void>::success();
}

} // namespace tlm
