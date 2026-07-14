#pragma once

#include <QObject>

class QApplication;
class QEvent;

namespace tlm {

/** Low-overhead Windows UI counters used while diagnosing visible-window CPU usage. */
class UiPerformanceMonitor final : public QObject {
public:
    enum class Counter {
        ThumbnailCoverageCheck,
        ThumbnailCoverageHit,
        ThumbnailCoverageMiss,
        ThumbnailPixmapLookup,
        ThumbnailRequest,
        ThumbnailRequestStarted,
        ThumbnailRequestCached,
        ThumbnailRequestPending,
        ThumbnailReady,
        ThumbnailFailed,
        ThumbnailEvicted,
        Count,
    };

    explicit UiPerformanceMonitor(QApplication* application, QObject* parent = nullptr);
    ~UiPerformanceMonitor() override;

    static void increment(Counter counter, quint64 amount = 1);
    static void setTierFocusMode(bool enabled);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    class Private;
    Private* d{nullptr};
};

} // namespace tlm
