#include "settings/preferences.hpp"

#include <QGuiApplication>
#include <QLocale>
#include <QStyleHints>
#include <QPalette>
#include <QFont>
#include <QEvent>
#include <QDBusConnection>
#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

#include <algorithm>

namespace bacuri::vms::client
{
Preferences* Preferences::create(QQmlEngine* engine, QJSEngine*)
{
    return new Preferences(engine);
}

Preferences::Preferences(QQmlEngine* engine) : engine_{engine}
{
    auto bus = QDBusConnection::sessionBus();
    const QString service = "org.freedesktop.portal.Desktop";
    bus.connect(service, "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", "SettingChanged",
                this, SLOT(settingChanged(QString, QString, QDBusVariant)));
    auto* watcher = new QDBusServiceWatcher(service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] { readPortal(); });
    readPortal();
    qGuiApp->installEventFilter(this);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            &Preferences::appearanceChanged);
    theme_ = settings_.value("appearance/theme", "system").toString();
    if (theme_ != "light" && theme_ != "dark")
        theme_ = "system";
    text_scale_ = std::clamp(settings_.value("appearance/textScale", 1.).toDouble(), 1., 2.);
    applyLanguage(settings_.value("appearance/language") == "ru" ? "ru" : "en");
}

Preferences::~Preferences()
{
    QCoreApplication::removeTranslator(&translator_);
}

bool Preferences::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qGuiApp && event->type() == QEvent::ApplicationFontChange)
        emit appearanceChanged();
    return QObject::eventFilter(watched, event);
}
QString Preferences::language() const
{
    return language_;
}
QString Preferences::theme() const
{
    return theme_;
}
double Preferences::textScale() const
{
    return text_scale_;
}

bool Preferences::dark() const
{
    if (theme_ != "system")
        return theme_ == "dark";
    const auto scheme = portal_.value("color-scheme").toUInt();
    if (scheme == 1 || scheme == 2)
        return scheme == 1;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

bool Preferences::motion() const
{
    return portal_.value("reduced-motion").toUInt() != 1;
}

double Preferences::systemTextScale() const
{
    // On xcb Qt already incorporates Xft.dpi into the window device pixel ratio.
    if (QGuiApplication::platformName() != "xcb" && portal_.contains("text-scaling-factor"))
        return std::clamp(portal_.value("text-scaling-factor").toDouble(), .5, 4.);
    const auto pt = QGuiApplication::font().pointSizeF();
    return pt > 0 ? std::clamp(pt / 9., .5, 4.) : 1.;
}

bool Preferences::highContrast() const
{
    return portal_.value("contrast").toUInt() == 1;
}
QColor Preferences::accent() const
{
    return portal_.value("accent-color", QGuiApplication::palette().accent().color()).value<QColor>();
}
void Preferences::readPortal()
{
    ++generation_;
    portal_.clear();
    revisions_.clear();
    emit appearanceChanged();
    auto bus = QDBusConnection::sessionBus();
    for (const QString key :
         {"color-scheme", "contrast", "accent-color", "reduced-motion", "text-scaling-factor"}) {
        const QString group =
            key == "text-scaling-factor" ? "org.gnome.desktop.interface" : "org.freedesktop.appearance";
        auto call =
            QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                           "org.freedesktop.portal.Settings", "Read");
        call << group << key;
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, watcher, group, key, generation = generation_, revision = revisions_.value(key)] {
                    QDBusPendingReply<QDBusVariant> reply = *watcher;
                    watcher->deleteLater();
                    if (!reply.isError() && generation_ == generation && revisions_.value(key) == revision)
                        settingChanged(group, key, reply.value());
                });
    }
}
void Preferences::settingChanged(const QString& group, const QString& key, const QDBusVariant& variant)
{
    if (group != "org.freedesktop.appearance" &&
        !(group == "org.gnome.desktop.interface" && key == "text-scaling-factor"))
        return;
    QVariant value = variant.variant();
    while (value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    if (key == "accent-color") {
        if (value.metaType() != QMetaType::fromType<QDBusArgument>())
            return;
        const auto arg = value.value<QDBusArgument>();
        if (arg.currentSignature() != "(ddd)")
            return;
        double r, g, b;
        arg.beginStructure();
        arg >> r >> g >> b;
        arg.endStructure();
        if (r < 0 || r > 1 || g < 0 || g > 1 || b < 0 || b > 1)
            return;
        value = QColor::fromRgbF(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
    }
    ++revisions_[key];
    if (portal_.value(key) != value) {
        portal_[key] = value;
        emit appearanceChanged();
    }
}
void Preferences::setLanguage(const QString& value)
{
    if (value != "en" && value != "ru")
        return;
    settings_.setValue("appearance/language", value);
    applyLanguage(value);
}
void Preferences::applyLanguage(const QString& value)
{
    if ((value != "en" && value != "ru") || value == language_)
        return;
    QCoreApplication::removeTranslator(&translator_);
    if (!translator_.load(":/i18n/" + value + ".qm"))
        qFatal("Cannot load the workstation translation catalog");
    QCoreApplication::installTranslator(&translator_);
    language_ = value;
    QLocale::setDefault(QLocale(value));
    engine_->setUiLanguage(value);
    engine_->retranslate();
    emit languageChanged();
}

void Preferences::setTheme(const QString& value)
{
    if ((value != "system" && value != "light" && value != "dark") || value == theme_)
        return;
    theme_ = value;
    settings_.setValue("appearance/theme", value);
    emit themeChanged();
    emit appearanceChanged();
}

void Preferences::setTextScale(double value)
{
    if (!(value >= 1. && value <= 2.) || value == text_scale_)
        return;
    text_scale_ = value;
    settings_.setValue("appearance/textScale", value);
    emit appearanceChanged();
}
} // namespace bacuri::vms::client
