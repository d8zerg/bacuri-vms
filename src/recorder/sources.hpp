#pragma once

#include "control/support.hpp"
#include "control.pb.h"
#include "recorder/ingest.hpp"

#include <map>

namespace bacuri::vms::ingest
{

class Sources
{
  public:
    explicit Sources(const control::CameraNetworks& networks) : networks_{networks} {}
    ~Sources();
    void apply(const v1::ApplyConfigurationRequest& assignment);

  private:
    struct Key {
        std::string device;
        std::string point;
        Profile profile;
        auto operator<=>(const Key&) const = default;
    };
    struct Entry {
        Endpoint endpoint;
        std::unique_ptr<Stream> stream;
    };

    const control::CameraNetworks& networks_;
    std::map<Key, Entry> streams_;
};

} // namespace bacuri::vms::ingest
