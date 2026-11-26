#pragma once

#include "platform/camera_networks.hpp"
#include "archive/writer.hpp"
#include "services/recorder/detector.hpp"
#include "control.pb.h"
#include "ingest/ingest.hpp"

#include <map>

namespace bacuri::vms::ingest
{

class Sources
{
  public:
    Sources(const platform::CameraNetworks& networks, archive::Volume& volume,
            const recording::RecordingPlan& plan, const DetectorPoints& detectors)
        : networks_{networks}, volume_{volume}, plan_{plan}, detectors_{detectors}
    {
    }
    Sources(const Sources&) = delete;
    Sources& operator=(const Sources&) = delete;
    ~Sources();
    v1::CameraIssues issues(const std::string& device) const;
    std::shared_ptr<CameraAudio> audio(const std::string& point, const std::string& device);
    void apply(const v1::ApplyConfigurationRequest& assignment);
    std::shared_ptr<FrameQueue> preview(const std::string& point, Profile profile);
    // Stream reception and policy selection of every point assigned to this recorder.
    v1::PointStates point_states();
    std::map<std::string, std::string> recording_states();
    v1::VideoProfiles video_profiles(const std::string& point) const;

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
        std::shared_ptr<CameraAudio> audio;
        std::unique_ptr<archive::Writer> writer;
        std::unique_ptr<Stream> stream;
        std::jthread detector;
    };

    const platform::CameraNetworks& networks_;
    archive::Volume& volume_;
    const recording::RecordingPlan& plan_;
    const DetectorPoints& detectors_;
    std::map<Key, Entry> streams_;
};

} // namespace bacuri::vms::ingest
