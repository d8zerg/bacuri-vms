#pragma once

#include "control/support.hpp"
#include "archive/writer.hpp"
#include "control.pb.h"
#include "recorder/ingest.hpp"

#include <map>

namespace bacuri::vms::ingest
{

class Sources
{
  public:
    Sources(const control::CameraNetworks& networks, archive::Volume& volume)
        : networks_{networks}, volume_{volume}
    {
    }
    ~Sources();
    v1::CameraIssues issues(const std::string& device) const;
    void apply(const v1::ApplyConfigurationRequest& assignment);
    std::shared_ptr<FrameQueue> preview(const std::string& point);

  private:
    struct Key {
        std::string device;
        std::string point;
        Profile profile;
        std::string channel, main_profile;
        std::int64_t binding;
        auto operator<=>(const Key&) const = default;
    };
    struct Entry {
        Endpoint endpoint;
        std::unique_ptr<archive::Writer> writer;
        std::unique_ptr<Stream> stream;
    };

    const control::CameraNetworks& networks_;
    archive::Volume& volume_;
    std::map<Key, Entry> streams_;
};

} // namespace bacuri::vms::ingest
