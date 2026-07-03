#pragma once

#include <QString>

namespace tlm::platform {

/** Reveals a path in the native file manager when possible. */
bool revealInFileManager(const QString& path);

/** Opens a URL or path with the desktop shell. */
bool openUrlOrPath(const QString& path);

/** Returns a readable platform label for About and diagnostics. */
QString platformName();

} // namespace tlm::platform

