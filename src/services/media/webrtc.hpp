#pragma once
#include "media/privacy.hpp"
#include <gst/app/gstappsrc.h>
#include <gst/webrtc/webrtc.h>
#include <chrono>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>

namespace bacuri::vms::media
{
template <class T, auto Release> struct GstDeleter {
    void operator()(T* object) const
    {
        Release(object);
    }
};
template <class T, auto Release> using GstHandle = std::unique_ptr<T, GstDeleter<T, Release>>;

struct LiveFormat {
    std::string_view source, output;
};
// Wakes the session thread when the connection may have changed instead of polling it.
struct PeerEvents;
class WebRtcPeer
{
  public:
    WebRtcPeer(const std::string& offer, LiveFormat format, std::string_view regions);
    ~WebRtcPeer();
    WebRtcPeer(const WebRtcPeer&) = delete;
    WebRtcPeer& operator=(const WebRtcPeer&) = delete;
    void stop() noexcept;
    std::string answer(std::chrono::steady_clock::time_point deadline);
    GstWebRTCPeerConnectionState state() const;
    void push(std::string_view access_unit, bool keyframe);
    // Waits until the connection or ICE gathering state changes, the pipeline reports an error, notify() is
    // called, the deadline passes or a stop is requested.
    void wait(std::stop_token stop, std::chrono::steady_clock::time_point deadline);
    // Wakes wait(), for example when the frame source of the connection ends.
    void notify() noexcept;

  private:
    std::unique_ptr<PeerEvents> events_;
    PrivacyMask mask_;
    GstHandle<GstWebRTCSessionDescription, gst_webrtc_session_description_free> offer_;
    GstHandle<GstElement, gst_object_unref> pipeline_, rtc_, input_;
    bool need_keyframe_{true};
};
} // namespace bacuri::vms::media
