#include "app/Application.h"

#include "assets/AssetManager.h"
#include "assets/ThumbnailCache.h"
#include "i18n/LanguageManager.h"
#include "logging/Logger.h"
#include "persistence/ProjectRepository.h"
#include "persistence/RecentProjectsStore.h"
#include "settings/AppSettings.h"
#include "theme/ThemeManager.h"
#include "update/AppUpdater.h"
#include "window/MainWindow.h"

#include <QDateTime>
#include <QFont>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QToolTip>
#include <QWidget>

#include <cmath>

namespace tlm {

namespace {
qreal relativeLuminance(const QColor& color) {
    const auto channel = [](qreal value) {
        value /= 255.0;
        return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF() * 255.0) +
           0.7152 * channel(color.greenF() * 255.0) +
           0.0722 * channel(color.blueF() * 255.0);
}

class TextOnlyToolTipLabel final : public QLabel {
public:
    explicit TextOnlyToolTipLabel(QWidget* parent = nullptr)
        : QLabel(parent, Qt::Tool | Qt::FramelessWindowHint |
                            Qt::WindowDoesNotAcceptFocus |
                            Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setObjectName(QStringLiteral("TextOnlyToolTip"));
        setMargin(0);
        setIndent(0);
    }

    void setReferencePalette(const QPalette& palette, QPalette::ColorRole role) {
        QColor background = palette.color(role);
        if (!background.isValid()) {
            background = palette.color(QPalette::Window);
        }
        const bool darkBackground = relativeLuminance(background) < 0.46;
        m_textColor = darkBackground ? QColor(255, 255, 255) : QColor(18, 22, 30);
        m_outlineColor = darkBackground ? QColor(0, 0, 0, 210) : QColor(255, 255, 255, 220);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        QFont tipFont = font();
        tipFont.setBold(true);
        painter.setFont(tipFont);

        const QFontMetrics metrics(tipFont);
        const QStringList lines = text().split(QLatin1Char('\n'));
        int y = 3 + metrics.ascent();
        for (const QString& line : lines) {
            QPainterPath glyphs;
            glyphs.addText(2, y, tipFont, line);
            painter.setPen(QPen(m_outlineColor, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(glyphs);
            painter.fillPath(glyphs, m_textColor);
            y += metrics.lineSpacing();
        }
    }

    QSize sizeHint() const override {
        QFont tipFont = font();
        tipFont.setBold(true);
        const QFontMetrics metrics(tipFont);
        const QStringList lines = text().split(QLatin1Char('\n'));
        int width = 0;
        for (const QString& line : lines) {
            width = qMax(width, metrics.horizontalAdvance(line));
        }
        const int lineCount = qMax(1, static_cast<int>(lines.size()));
        return QSize(width + 6, metrics.lineSpacing() * lineCount + 6);
    }

private:
    QColor m_textColor{Qt::white};
    QColor m_outlineColor{0, 0, 0, 210};
};

class TextOnlyToolTipFilter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        QWidget* widget = qobject_cast<QWidget*>(watched);
        if (!widget) {
            return QObject::eventFilter(watched, event);
        }

        if (event->type() == QEvent::ToolTip) {
            auto* helpEvent = static_cast<QHelpEvent*>(event);
            const QString text = widget->toolTip().trimmed();
            if (text.isEmpty()) {
                hideTip();
                event->ignore();
                return true;
            }

            showTip(text, helpEvent->globalPos(), widget);
            event->accept();
            return true;
        }

        if (widget == m_owner &&
            (event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress ||
             event->type() == QEvent::KeyPress || event->type() == QEvent::Hide ||
             event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Destroy)) {
            hideTip();
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void showTip(const QString& text, const QPoint& globalPos, QWidget* owner) {
        if (!m_tip) {
            m_tip = new TextOnlyToolTipLabel();
            m_tip->setFont(QToolTip::font());
        }

        m_owner = owner;
        const QPalette ownerPalette = owner ? owner->palette() : qApp->palette();
        m_tip->setPalette(ownerPalette);
        m_tip->setReferencePalette(ownerPalette, owner ? owner->backgroundRole() : QPalette::Window);
        m_tip->setText(text);
        m_tip->adjustSize();

        QPoint pos = globalPos + QPoint(0, 18);
        if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
            const QRect available = screen->availableGeometry().adjusted(8, 8, -8, -8);
            pos.setX(qBound(available.left(), pos.x(), available.right() - m_tip->width()));
            pos.setY(qBound(available.top(), pos.y(), available.bottom() - m_tip->height()));
        }
        m_tip->move(pos);
        m_tip->show();
        m_tip->raise();
    }

    void hideTip() {
        if (m_tip) {
            m_tip->hide();
        }
        m_owner = nullptr;
    }

    QPointer<TextOnlyToolTipLabel> m_tip;
    QPointer<QWidget> m_owner;
};
} // namespace

Application::Application(int& argc, char** argv) : QApplication(argc, argv) {
    configureApplication();
    configureFont();

    m_logger = std::make_unique<Logger>();
    m_logger->installMessageHandler();
    m_settings = std::make_unique<AppSettings>();
    m_languageManager = std::make_unique<LanguageManager>(this);
    m_languageManager->setLanguage(m_settings->language());
    m_themeManager = std::make_unique<ThemeManager>(m_settings.get());
    m_themeManager->applyTo(*this);
    m_repository = std::make_unique<ProjectRepository>();
    m_recentProjects = std::make_unique<RecentProjectsStore>();
    m_assetManager = std::make_unique<AssetManager>();
    m_thumbnailCache = std::make_unique<ThumbnailCache>();
    m_updater = std::make_unique<AppUpdater>();
    connect(m_updater.get(), &AppUpdater::checkingStarted, this, [this](const QUrl&) {
        if (m_settings) {
            m_settings->setLastUpdateCheckAt(QDateTime::currentDateTimeUtc());
        }
    });
}

Application::~Application() = default;

int Application::run() {
    MainWindow window(m_repository.get(), m_recentProjects.get(), m_assetManager.get(),
                      m_thumbnailCache.get(), m_settings.get(), m_languageManager.get(),
                      m_updater.get());
    window.show();
    scheduleAutoUpdateCheck();
    return exec();
}

void Application::configureApplication() {
    setOrganizationName(QStringLiteral("TierListMaker"));
    setOrganizationDomain(QStringLiteral("tierlistmaker.local"));
    setApplicationName(QStringLiteral("TierListMaker"));
    setApplicationDisplayName(QStringLiteral("TierListMaker"));
    setApplicationVersion(QStringLiteral(TLM_APP_VERSION));
    setWindowIcon(QIcon(QStringLiteral(":/images/app-icon.png")));
    setQuitOnLastWindowClosed(true);
    setStyleSheet(QStringLiteral(
        "QToolTip{background:transparent;border:0px;padding:0px;color:palette(window-text);font-weight:700;}"));
    installEventFilter(new TextOnlyToolTipFilter(this));
}

void Application::configureFont() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    setFont(QFont(QStringLiteral(".AppleSystemUIFont")));
#elif defined(Q_OS_WIN)
    setFont(QFont(QStringLiteral("Segoe UI")));
#else
    QFont font;
    font.setStyleHint(QFont::SansSerif);
    setFont(font);
#endif
}

void Application::scheduleAutoUpdateCheck() {
    if (!m_settings || !m_updater || !m_settings->shouldRunAutoUpdateCheck()) {
        return;
    }
    // Delay the network request until the first window has settled, matching the
    // manual updater path while avoiding startup UI contention.
    QTimer::singleShot(1500, m_updater.get(), [updater = m_updater.get()]() {
        if (updater) {
            updater->checkForUpdates();
        }
    });
}

} // namespace tlm
