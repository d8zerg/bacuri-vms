#pragma once

#include <QUrl>

namespace bacuri::vms::client
{
enum class HelpPage { home, sign_in, monitoring, site_map };

// Build a public help link from the configured site and the display preferences of the workstation.
QUrl help_url(const QUrl& site, QStringView language, QStringView theme, HelpPage page, QStringView cause = {});
bool open_help(const QUrl& site, QStringView language, QStringView theme, HelpPage page, QStringView cause = {});
} // namespace bacuri::vms::client
