#pragma once

#include "export/export_jobs.hpp"
#include "live/live_preview.hpp"

namespace bacuri::vms::client
{
struct LivePreviewType {
    Q_GADGET
    QML_FOREIGN(LivePreview)
    QML_NAMED_ELEMENT(LivePreview)
};
struct ExportJobsType {
    Q_GADGET
    QML_FOREIGN(ExportJobs)
    QML_ANONYMOUS
};
} // namespace bacuri::vms::client
