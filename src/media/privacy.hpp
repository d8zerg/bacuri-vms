#pragma once
#include "domain/image_region.hpp"
#include <gst/gst.h>
#include <string_view>
#include <vector>
namespace bacuri::vms::media
{
class PrivacyMask
{
  public:
    explicit PrivacyMask(std::string_view json);
    explicit PrivacyMask(domain::ImageRegions regions) : regions_(std::move(regions)) {}
    void attach(GstElement* encoder);
    bool empty() const
    {
        return regions_.empty();
    }

  private:
    domain::ImageRegions regions_;
    bool cover(GstPad*, GstPadProbeInfo*) const;
};
} // namespace bacuri::vms::media
