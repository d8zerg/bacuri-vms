#include "media/graphics_probe.hpp"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

namespace bacuri::vms::client
{
Graphics probe_graphics()
{
    Graphics graphics{QStringLiteral("none"), {}, true};
    QOpenGLContext context;
    if (!context.create())
        return graphics;
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!context.makeCurrent(&surface))
        return graphics;
    auto* functions = context.functions();
    graphics.renderer = QString::fromLatin1(reinterpret_cast<const char*>(functions->glGetString(GL_RENDERER)));
    graphics.version = QString::fromLatin1(reinterpret_cast<const char*>(functions->glGetString(GL_VERSION)));
    context.doneCurrent();
    const auto renderer = graphics.renderer.toLower();
    graphics.software =
        renderer.contains(QLatin1String("llvmpipe")) || renderer.contains(QLatin1String("softpipe")) ||
        renderer.contains(QLatin1String("swrast")) || renderer.contains(QLatin1String("software rasterizer")) ||
        renderer.contains(QLatin1String("basic render driver"));
    return graphics;
}
} // namespace bacuri::vms::client
