#include "site/help.hpp"

#include <QDesktopServices>
#include <QUrlQuery>

namespace bacuri::vms::client
{
QUrl help_url(const QUrl& site, QStringView language, QStringView theme, HelpPage page, QStringView cause)
{
    if (!site.isValid() || site.scheme() != "https" || site.host().isEmpty() || !site.userInfo().isEmpty() ||
        site.hasQuery() || site.hasFragment() || (!site.path().isEmpty() && site.path() != "/") ||
        (language != u"en" && language != u"ru") || (theme != u"light" && theme != u"dark" && theme != u"system"))
        return {};

    QUrl result = site.resolved(QUrl{QStringLiteral("/help")});
    QUrlQuery fragment;
    fragment.addQueryItem("lang", language.toString());
    fragment.addQueryItem("theme", theme.toString());
    if (page == HelpPage::sign_in) {
        if (cause == u"desktop_code_invalid")
            fragment.addQueryItem("code", "desktop_code_invalid");
        else if (cause == u"site_unreachable" || cause == u"certificate_untrusted")
            fragment.addQueryItem("article", "connection-lost");
        else
            fragment.addQueryItem("article", "sign-in");
    } else if (page == HelpPage::site_map)
        fragment.addQueryItem("article", "camera-on-map");
    else if (page == HelpPage::monitoring)
        fragment.addQueryItem("article", "start-monitoring");
    result.setFragment(fragment.toString());
    return result;
}

bool open_help(const QUrl& site, QStringView language, QStringView theme, HelpPage page, QStringView cause)
{
    const auto url = help_url(site, language, theme, page, cause);
    return !url.isEmpty() && QDesktopServices::openUrl(url);
}
} // namespace bacuri::vms::client
