#include "app/window_group.hpp"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>

using namespace bacuri::vms::client;

int main(int argc, char* argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    app.setOrganizationName("bacuri.vms");
    app.setApplicationName("bacuri-vms-client");
    app.setApplicationDisplayName("Bacuri VMS");
    app.setWindowIcon(QIcon(":/design/brand/mark.svg"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {"site", "HTTPS origin of the site.", "url"},
        {"ca", "Certificates trusted for the site.", "file"},
        {"browser", "Command that opens the site confirmation URL.", "command"},
    });
    parser.process(app);
#ifdef BACURI_COMPONENT_CATALOG
    constexpr bool catalog = true;
#else
    constexpr bool catalog = false;
#endif
    WindowGroup windows({QUrl(parser.value("site")), parser.value("ca"), parser.value("browser")}, catalog);
    app.setQuitOnLastWindowClosed(false);
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &windows, &WindowGroup::finish);
    if (!windows.open())
        return 1;
    return app.exec();
}
