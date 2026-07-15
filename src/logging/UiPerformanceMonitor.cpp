#include "logging/UiPerformanceMonitor.h"

#include "logging/Logger.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QHash>
#include <QPaintEvent>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <array>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#elif defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

namespace tlm {

namespace {

enum class Surface {
    Window,
    TierBoard,
    EditorToolbar,
    Sidebar,
    Projects,
    Preferences,
    Other,
    Count,
};

constexpr auto surfaceCount = static_cast<std::size_t>(Surface::Count);
constexpr auto counterCount = static_cast<std::size_t>(UiPerformanceMonitor::Counter::Count);

std::size_t indexOf(Surface surface) {
    return static_cast<std::size_t>(surface);
}

std::size_t indexOf(UiPerformanceMonitor::Counter counter) {
    return static_cast<std::size_t>(counter);
}

QString surfaceName(Surface surface) {
    switch (surface) {
    case Surface::Window:
        return QStringLiteral("window");
    case Surface::TierBoard:
        return QStringLiteral("tier");
    case Surface::EditorToolbar:
        return QStringLiteral("toolbar");
    case Surface::Sidebar:
        return QStringLiteral("sidebar");
    case Surface::Projects:
        return QStringLiteral("projects");
    case Surface::Preferences:
        return QStringLiteral("preferences");
    case Surface::Other:
        return QStringLiteral("other");
    case Surface::Count:
        break;
    }
    return QStringLiteral("unknown");
}

#if defined(Q_OS_WIN)
quint64 fileTimeValue(const FILETIME& time) {
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

quint64 processCpuTime() {
    FILETIME creation;
    FILETIME exit;
    FILETIME kernel;
    FILETIME user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        return 0;
    }
    return fileTimeValue(kernel) + fileTimeValue(user);
}
#elif defined(Q_OS_UNIX)
quint64 processCpuTime() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
    const auto microseconds = [](const timeval& value) {
        return static_cast<quint64>(value.tv_sec) * 1'000'000ULL +
               static_cast<quint64>(value.tv_usec);
    };
    // Match Windows FILETIME's 100 ns unit so the sampling formula stays platform neutral.
    return (microseconds(usage.ru_utime) + microseconds(usage.ru_stime)) * 10ULL;
}
#endif

} // namespace

class UiPerformanceMonitor::Private {
public:
    struct SurfaceStats {
        quint64 paints{0};
        quint64 paintedPixels{0};
        quint64 updateRequests{0};
        quint64 layoutRequests{0};
        quint64 styleAnimationUpdates{0};
        quint64 hoverEvents{0};
        quint64 mouseMoves{0};
        quint64 resizes{0};
    };

    struct TargetStats {
        QString label;
        quint64 paints{0};
        quint64 paintedPixels{0};
        quint64 paintRegionRects{0};
        quint64 largestPaintPixels{0};
        quint64 widgetPixels{0};
        quint64 updateRequests{0};
    };

    bool enabled{false};
    bool tierFocusMode{false};
    QApplication* application{nullptr};
    QTimer timer;
    QElapsedTimer elapsed;
    quint64 previousProcessCpuTime{0};
    quint64 timerEvents{0};
    std::array<SurfaceStats, surfaceCount> surfaces{};
    std::array<quint64, counterCount> counters{};
    QHash<const QObject*, Surface> surfaceCache;
    QHash<const QObject*, TargetStats> targets;

    TargetStats& targetFor(const QObject* object) {
        auto [it, inserted] = targets.tryEmplace(object);
        if (inserted && object) {
            const QString objectName = object->objectName();
            it->label =
                objectName.isEmpty()
                    ? QString::fromLatin1(object->metaObject()->className())
                    : QStringLiteral("%1#%2").arg(
                          QString::fromLatin1(object->metaObject()->className()), objectName);
            if (const auto* widget = qobject_cast<const QWidget*>(object)) {
                it->widgetPixels = static_cast<quint64>(qMax(0, widget->width())) *
                                   static_cast<quint64>(qMax(0, widget->height()));
            }
        }
        return it.value();
    }

    Surface surfaceFor(const QObject* object) {
        if (!object) {
            return Surface::Other;
        }
        if (const auto found = surfaceCache.constFind(object); found != surfaceCache.cend()) {
            return found.value();
        }

        Surface result = Surface::Other;
        for (const QObject* current = object; current; current = current->parent()) {
            const QString name = current->objectName();
            if (name == QStringLiteral("ToolbarButtonGroup") ||
                current->inherits("tlm::AppTitleBar")) {
                result = Surface::EditorToolbar;
                break;
            }
            if (current->inherits("tlm::TierListView") ||
                current->inherits("tlm::TierBoardWidget")) {
                result = Surface::TierBoard;
                break;
            }
            if (current->inherits("tlm::SidebarView") || name == QStringLiteral("Sidebar") ||
                name == QStringLiteral("SidebarShell")) {
                result = Surface::Sidebar;
                break;
            }
            if (current->inherits("tlm::ProjectsPage")) {
                result = Surface::Projects;
                break;
            }
            if (current->inherits("tlm::PreferencesPage") ||
                name == QStringLiteral("PreferencesDialog")) {
                result = Surface::Preferences;
                break;
            }
            if (current->inherits("tlm::MainWindow") ||
                (current->isWidgetType() && !current->parent())) {
                result = Surface::Window;
            }
        }
        surfaceCache.insert(object, result);
        return result;
    }

    void resetSample() {
        surfaces = {};
        counters = {};
        timerEvents = 0;
        targets.clear();
    }

    void writeSample() {
        if (!enabled || !application) {
            return;
        }

        const qint64 elapsedMs = qMax<qint64>(1, elapsed.restart());
        qreal corePercent = 0.0;
#if defined(Q_OS_WIN) || defined(Q_OS_UNIX)
        const quint64 currentCpuTime = processCpuTime();
        if (currentCpuTime >= previousProcessCpuTime && previousProcessCpuTime != 0) {
            const quint64 delta100ns = currentCpuTime - previousProcessCpuTime;
            corePercent =
                static_cast<qreal>(delta100ns) / (static_cast<qreal>(elapsedMs) * 10'000.0) * 100.0;
        }
        previousProcessCpuTime = currentCpuTime;
#endif

        QWidget* window = application->activeWindow();
        if (!window) {
            for (QWidget* candidate : application->topLevelWidgets()) {
                if (candidate && candidate->isVisible() && candidate->isWindow()) {
                    window = candidate;
                    break;
                }
            }
        }

        quint64 totalPaints = 0;
        quint64 totalUpdates = 0;
        quint64 totalLayouts = 0;
        quint64 totalStyleAnimations = 0;
        quint64 totalHover = 0;
        quint64 totalMouseMoves = 0;
        QStringList surfaceParts;
        surfaceParts.reserve(static_cast<qsizetype>(surfaceCount));
        for (std::size_t i = 0; i < surfaceCount; ++i) {
            const SurfaceStats& stats = surfaces[i];
            totalPaints += stats.paints;
            totalUpdates += stats.updateRequests;
            totalLayouts += stats.layoutRequests;
            totalStyleAnimations += stats.styleAnimationUpdates;
            totalHover += stats.hoverEvents;
            totalMouseMoves += stats.mouseMoves;
            if (stats.paints || stats.updateRequests || stats.layoutRequests ||
                stats.styleAnimationUpdates || stats.hoverEvents || stats.mouseMoves) {
                surfaceParts.append(QStringLiteral("%1:p%2/px%3/u%4/l%5/a%6/h%7/m%8")
                                        .arg(surfaceName(static_cast<Surface>(i)))
                                        .arg(stats.paints)
                                        .arg(stats.paintedPixels)
                                        .arg(stats.updateRequests)
                                        .arg(stats.layoutRequests)
                                        .arg(stats.styleAnimationUpdates)
                                        .arg(stats.hoverEvents)
                                        .arg(stats.mouseMoves));
            }
        }

        QVector<TargetStats> busiestTargets;
        busiestTargets.reserve(targets.size());
        for (const TargetStats& stats : std::as_const(targets)) {
            if (stats.paints || stats.updateRequests) {
                busiestTargets.append(stats);
            }
        }
        std::ranges::sort(busiestTargets, [](const TargetStats& left, const TargetStats& right) {
            return left.paints + left.updateRequests > right.paints + right.updateRequests;
        });
        QStringList targetParts;
        const qsizetype targetLimit = qMin<qsizetype>(6, busiestTargets.size());
        targetParts.reserve(targetLimit);
        for (qsizetype i = 0; i < targetLimit; ++i) {
            const TargetStats& stats = busiestTargets.at(i);
            targetParts.append(QStringLiteral("%1:p%2/px%3/max%4/of%5/r%6/u%7")
                                   .arg(stats.label)
                                   .arg(stats.paints)
                                   .arg(stats.paintedPixels)
                                   .arg(stats.largestPaintPixels)
                                   .arg(stats.widgetPixels)
                                   .arg(stats.paintRegionRects)
                                   .arg(stats.updateRequests));
        }

        Logger::debug(
            QStringLiteral("ui.perf.sample intervalMs=%1 cpuCorePct=%2 visible=%3 minimized=%4 "
                           "active=%5 focus=%6 paint=%7 update=%8 layout=%9 styleAnimation=%10 "
                           "timer=%11 hover=%12 mouseMove=%13 surfaces={%14} targets={%15}")
                .arg(elapsedMs)
                .arg(corePercent, 0, 'f', 1)
                .arg(window && window->isVisible())
                .arg(window && window->isMinimized())
                .arg(window && window->isActiveWindow())
                .arg(tierFocusMode)
                .arg(totalPaints)
                .arg(totalUpdates)
                .arg(totalLayouts)
                .arg(totalStyleAnimations)
                .arg(timerEvents)
                .arg(totalHover)
                .arg(totalMouseMoves)
                .arg(surfaceParts.join(QLatin1Char(',')))
                .arg(targetParts.join(QLatin1Char(','))));

        Logger::debug(
            QStringLiteral("thumbnail.perf coverage=%1 hit=%2 miss=%3 lookup=%4 request=%5 "
                           "started=%6 cached=%7 pending=%8 ready=%9 failed=%10 evicted=%11")
                .arg(counters[indexOf(Counter::ThumbnailCoverageCheck)])
                .arg(counters[indexOf(Counter::ThumbnailCoverageHit)])
                .arg(counters[indexOf(Counter::ThumbnailCoverageMiss)])
                .arg(counters[indexOf(Counter::ThumbnailPixmapLookup)])
                .arg(counters[indexOf(Counter::ThumbnailRequest)])
                .arg(counters[indexOf(Counter::ThumbnailRequestStarted)])
                .arg(counters[indexOf(Counter::ThumbnailRequestCached)])
                .arg(counters[indexOf(Counter::ThumbnailRequestPending)])
                .arg(counters[indexOf(Counter::ThumbnailReady)])
                .arg(counters[indexOf(Counter::ThumbnailFailed)])
                .arg(counters[indexOf(Counter::ThumbnailEvicted)]));
        resetSample();
    }
};

namespace {
UiPerformanceMonitor* g_monitor = nullptr;
}

UiPerformanceMonitor::UiPerformanceMonitor(QApplication* application, QObject* parent)
    : QObject(parent), d(new Private) {
    const QByteArray requested = qgetenv("TLM_PERF_DIAGNOSTICS").trimmed();
    const bool explicitlyEnabled =
        requested == QByteArrayLiteral("1") ||
        requested.compare(QByteArrayLiteral("true"), Qt::CaseInsensitive) == 0;
#if defined(NDEBUG)
    d->enabled = explicitlyEnabled;
#else
    const bool explicitlyDisabled =
        requested == QByteArrayLiteral("0") ||
        requested.compare(QByteArrayLiteral("false"), Qt::CaseInsensitive) == 0;
#if defined(Q_OS_WIN)
    d->enabled = !explicitlyDisabled;
#else
    // Keep the observer out of normal macOS/Linux debug sessions. It remains available on demand
    // for identical cross-platform paint/update/CPU diagnostics.
    d->enabled = explicitlyEnabled && !explicitlyDisabled;
#endif
#endif
    if (!d->enabled || !application) {
        return;
    }

    g_monitor = this;
    d->application = application;
#if defined(Q_OS_WIN) || defined(Q_OS_UNIX)
    d->previousProcessCpuTime = processCpuTime();
#endif
    d->elapsed.start();
    application->installEventFilter(this);
    d->timer.setInterval(2000);
    d->timer.setTimerType(Qt::CoarseTimer);
    connect(&d->timer, &QTimer::timeout, this, [this]() { d->writeSample(); });
    d->timer.start();
    Logger::info(QStringLiteral("ui.perf.monitor enabled intervalMs=2000 enableWith="
                                "TLM_PERF_DIAGNOSTICS=1 disableWith=TLM_PERF_DIAGNOSTICS=0"));
}

UiPerformanceMonitor::~UiPerformanceMonitor() {
    if (g_monitor == this) {
        g_monitor = nullptr;
    }
    if (d && d->application) {
        d->application->removeEventFilter(this);
    }
    delete d;
}

void UiPerformanceMonitor::increment(Counter counter, quint64 amount) {
    if (!g_monitor || !g_monitor->d || !g_monitor->d->enabled) {
        return;
    }
    g_monitor->d->counters[indexOf(counter)] += amount;
}

void UiPerformanceMonitor::setTierFocusMode(bool enabled) {
    if (!g_monitor || !g_monitor->d || !g_monitor->d->enabled) {
        return;
    }
    g_monitor->d->tierFocusMode = enabled;
    Logger::debug(QStringLiteral("ui.perf.state focus=%1").arg(enabled));
}

bool UiPerformanceMonitor::eventFilter(QObject* watched, QEvent* event) {
    if (!d || !d->enabled || !event) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::Destroy) {
        d->surfaceCache.remove(watched);
        d->targets.remove(watched);
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::Timer) {
        ++d->timerEvents;
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::Paint:
    case QEvent::UpdateRequest:
    case QEvent::LayoutRequest:
    case QEvent::StyleAnimationUpdate:
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::HoverEnter:
    case QEvent::HoverLeave:
    case QEvent::HoverMove:
    case QEvent::MouseMove:
    case QEvent::Resize:
        break;
    default:
        return QObject::eventFilter(watched, event);
    }

    const auto surface = d->surfaceFor(watched);
    Private::SurfaceStats& stats = d->surfaces[indexOf(surface)];
    switch (event->type()) {
    case QEvent::Paint: {
        ++stats.paints;
        Private::TargetStats& target = d->targetFor(watched);
        ++target.paints;
        const QRegion region = static_cast<QPaintEvent*>(event)->region();
        quint64 paintPixels = 0;
        for (const QRect& rect : region) {
            paintPixels += static_cast<quint64>(qMax(0, rect.width())) *
                           static_cast<quint64>(qMax(0, rect.height()));
        }
        stats.paintedPixels += paintPixels;
        target.paintedPixels += paintPixels;
        target.paintRegionRects += static_cast<quint64>(region.rectCount());
        target.largestPaintPixels = qMax(target.largestPaintPixels, paintPixels);
        break;
    }
    case QEvent::UpdateRequest:
        ++stats.updateRequests;
        ++d->targetFor(watched).updateRequests;
        break;
    case QEvent::LayoutRequest:
        ++stats.layoutRequests;
        break;
    case QEvent::StyleAnimationUpdate:
        ++stats.styleAnimationUpdates;
        break;
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::HoverEnter:
    case QEvent::HoverLeave:
    case QEvent::HoverMove:
        ++stats.hoverEvents;
        break;
    case QEvent::MouseMove:
        ++stats.mouseMoves;
        break;
    case QEvent::Resize:
        ++stats.resizes;
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace tlm
