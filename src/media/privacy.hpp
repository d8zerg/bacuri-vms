#pragma once
#include <gst/gst.h>
#include <string_view>
#include <vector>
namespace bacuri::vms::media
{
class PrivacyMask
{
  public:
    explicit PrivacyMask(std::string_view json);
    void attach(GstElement* encoder);
    bool empty() const
    {
        return regions_.empty();
    }

  private:
    struct Region {
        double left, top, right, bottom;
    };
    std::vector<Region> regions_;
    bool cover(GstPad*, GstPadProbeInfo*) const;
};
} // namespace bacuri::vms::media
