#pragma once

#include <QString>

namespace bacuri::vms::client
{
// The OpenGL renderer of the station, read before the first window is created. A software renderer
// means a station without a usable GPU: the client then decodes in software and limits the picture
// and the frame rate of each cell to keep the interface responsive.
struct Graphics {
    QString renderer;
    QString version;
    bool software{};
};

// Needs a QGuiApplication.
Graphics probe_graphics();
} // namespace bacuri::vms::client
