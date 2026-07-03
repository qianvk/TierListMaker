#pragma once

#include <QStringView>

namespace tlm::settings_keys {

inline constexpr QStringView language = u"general/language";
inline constexpr QStringView appearance = u"general/appearance";
inline constexpr QStringView importBehavior = u"import/behavior";
inline constexpr QStringView autosaveEnabled = u"save/autosaveEnabled";
inline constexpr QStringView autosaveIntervalMinutes = u"save/autosaveIntervalMinutes";
inline constexpr QStringView autoUpdateEnabled = u"update/autoUpdateEnabled";
inline constexpr QStringView lastUpdateCheckAt = u"update/lastCheckAt";
inline constexpr QStringView exportFormat = u"export/format";
inline constexpr QStringView exportScale = u"export/scale";
inline constexpr QStringView animationsEnabled = u"animation/enabled";
inline constexpr QStringView reducedMotion = u"animation/reducedMotion";
inline constexpr QStringView localOnlyMode = u"privacy/localOnlyMode";

} // namespace tlm::settings_keys
