#include "platform/Platform.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

namespace tlm::platform {

bool openUrlOrPath(const QString& path) {
    const QUrl url = path.startsWith(QStringLiteral("http"))
        ? QUrl(path)
        : QUrl::fromLocalFile(path);
    return QDesktopServices::openUrl(url);
}

bool revealInFileManager(const QString& path) {
    const QFileInfo info(path);
    const QString target = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    return QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

QString platformName() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QStringLiteral("macOS");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("Linux");
#else
    return QStringLiteral("Unknown");
#endif
}

} // namespace tlm::platform

