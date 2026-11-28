#include "app/window_group.hpp"
#include "app/navigation.hpp"

#include "settings/preferences.hpp"

#include <QFontDatabase>
#include <QGuiApplication>

namespace bacuri::vms::client
{
WindowGroup::WindowGroup(SignInOptions options, bool catalog)
    : workspace_{std::move(options)}, shell_{&engine_}, show_catalog_{catalog}
{
    static const int latin = QFontDatabase::addApplicationFont(":/design/assets/inter-latin.woff2");
    static const int cyrillic = QFontDatabase::addApplicationFont(":/design/assets/inter-cyrillic.woff2");
    if (latin >= 0 && cyrillic >= 0) {
        QFont font = QGuiApplication::font();
        font.setFamily("Inter");
        QGuiApplication::setFont(font);
    }
    auto* preferences = engine_.singletonInstance<Preferences*>("Bacuri.Vms.Client.Settings", "Preferences");
    connect(preferences, &Preferences::languageChanged, &workspace_, &Workspace::messageChanged);
    if (show_catalog_)
        shell_.loadFromModule("Bacuri.Vms.Client.Catalog", "Catalog");
    else
        shell_.loadFromModule("Bacuri.Vms.Client.App", "Shell");
}

void WindowGroup::finish()
{
    if (!workspace_.signedIn()) {
        workspace_.cancel();
        QCoreApplication::quit();
        return;
    }
    connect(&workspace_, &Workspace::changed, this, [this] {
        if (workspace_.state() != Workspace::SigningOut)
            QCoreApplication::quit();
    });
    workspace_.signOut();
}

QQuickWindow* WindowGroup::open(const QVariantList& intents)
{
    auto* window = qobject_cast<QQuickWindow*>(
        show_catalog_ ? shell_.create()
                      : shell_.createWithInitialProperties({{"workspace", QVariant::fromValue(&workspace_)}}));
    if (!window) {
        qCritical().noquote() << shell_.errorString();
        return nullptr;
    }
    if (show_catalog_) {
        auto* source = window->property("workspace").value<Workspace*>();
        auto* preferences = engine_.singletonInstance<Preferences*>("Bacuri.Vms.Client.Settings", "Preferences");
        connect(preferences, &Preferences::languageChanged, source, &Workspace::messageChanged);
    }
    if (!intents.isEmpty()) {
        auto* navigation = window->property("navigation").value<Navigation*>();
        if (!navigation || !navigation->restore(intents)) {
            delete window;
            return nullptr;
        }
        QMetaObject::invokeMethod(window, "restoreFocus");
    }
    window->QObject::setParent(&shell_);
    connect(window, SIGNAL(newWindow()), this, SLOT(openWindow()));
    connect(window, SIGNAL(closing(QQuickCloseEvent*)), window, SLOT(deleteLater()));
    return window;
}
} // namespace bacuri::vms::client
