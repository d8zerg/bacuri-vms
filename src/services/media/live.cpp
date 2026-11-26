#include "services/media/live.hpp"
#include "platform/rpc_status.hpp"
#include "media/lease.hpp"
#include "services/media/webrtc.hpp"
#include "platform/identifiers.hpp"
#include "observability/metrics.hpp"
#include "platform/scope_exit.hpp"
#include <cstdio>
#include <future>
#include <utility>

namespace bacuri::vms::media
{
namespace
{
using namespace std::chrono_literals;
using Result = std::pair<grpc::Status, std::string>;
// Time from the start of negotiation to the first frame handed to the WebRTC connection.
void first_frame(Clock::duration elapsed)
{
    observability::Registry::process()
        .histogram({.name = "bacuri_live_first_frame_seconds",
                    .help = "Time from a live offer to the first frame sent to the viewer."},
                   observability::interaction_buckets)
        .observe(elapsed);
}

void run(const std::stop_token& stop, const v1::LiveOffer& offer, std::promise<Result> result)
{
    const auto negotiated = Clock::now();
    bool answered = false;
    try {
        Lease lease{offer.scope()};
        lease.deadline();
        const auto& video = offer.scope().live();
        const std::string source_codec = video.source_codec().empty() ? "h264" : video.source_codec();
        WebRtcPeer peer{
            offer.sdp(),
            {.source = source_codec, .output = video.output_codec().empty() ? "h264" : video.output_codec()},
            offer.scope().privacy_regions()};
        const auto setup = Clock::now() + 5s;
        auto sdp = peer.answer(setup);
        lease.deadline();
        result.set_value({grpc::Status::OK, std::move(sdp)});
        answered = true;
        grpc::ClientContext source_context;
        source_context.set_deadline(std::chrono::system_clock::now() + std::chrono::minutes{5});
        std::atomic_bool ended{};
        std::atomic<Clock::time_point> authorized_until{};
        std::jthread source;
        platform::ScopeExit shutdown{[&] {
            peer.stop();
            source_context.TryCancel();
            lease.close_live();
        }};

        bool connected = false;
        while (!stop.stop_requested() && !ended) {
            auto valid = lease.deadline();
            authorized_until = valid;
            if (!connected && Clock::now() >= setup) {
                break;
            }
            const auto state = peer.state();
            if (state == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED && !connected) {
                connected = true;
                source = std::jthread{[&] {
                    try {
                        auto stub = v1::PreviewSource::NewStub(dedicated_channel("BACURI_PREVIEW_SOCKET"));
                        v1::PreviewRequest request;
                        *request.mutable_point() = offer.scope().point();
                        request.set_main(video.main());
                        auto reader = stub->SubscribeProfile(&source_context, request);
                        v1::PreviewFrame frame;
                        bool first = true;
                        while (reader->Read(&frame) && Clock::now() < authorized_until.load()) {
                            if (frame.codec() != source_codec)
                                break;
                            peer.push(frame.access_unit(), frame.keyframe());
                            if (std::exchange(first, false))
                                first_frame(Clock::now() - negotiated);
                        }
                        source_context.TryCancel();
                        reader->Finish();
                    } catch (const std::exception&) {
                        std::fputs("Preview stream interrupted\n", stderr);
                    }
                    ended = true;
                    peer.notify();
                }};
            }
            if (state == GST_WEBRTC_PEER_CONNECTION_STATE_FAILED ||
                state == GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED ||
                state == GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED) {
                break;
            }
            // Wakes on a connection change, the end of the source, the lease deadline or the setup deadline.
            peer.wait(stop, connected ? valid : std::min(valid, setup));
        }
    } catch (const std::invalid_argument&) {
        if (!answered) {
            result.set_value({platform::rpc_status(ErrorCode::invalid_request), {}});
        }
    } catch (const std::exception&) {
        if (!answered) {
            result.set_value({platform::rpc_status(ErrorCode::media_service_unavailable), {}});
        }
    }
}
} // namespace
grpc::Status LiveDelivery::Negotiate(grpc::ServerContext* context, const v1::LiveOffer* offer,
                                     v1::LiveAnswer* answer)
{
    if (!offer->scope().has_live() || !platform::canonical_id(offer->scope().media_session_id())) {
        return platform::rpc_status(ErrorCode::invalid_request);
    }
    std::promise<Result> ready;
    auto future = ready.get_future();
    {
        std::scoped_lock guard{mutex_};
        std::erase_if(peers_, [](const auto& entry) { return entry.second->done.load(); });
        auto id = offer->scope().media_session_id();
        if (peers_.contains(id)) {
            return platform::rpc_status(ErrorCode::idempotency_conflict);
        }
        if (peers_.size() >= 16) {
            return platform::rpc_status(ErrorCode::video_session_limit);
        }
        auto peer = std::make_unique<Peer>();
        peer->worker = std::jthread{[entry = peer.get(), request = *offer,
                                     result = std::move(ready)](const std::stop_token& stop) mutable {
            run(stop, request, std::move(result));
            entry->done = true;
        }};
        peers_.emplace(std::move(id), std::move(peer));
    }
    if (future.wait_until(context->deadline()) != std::future_status::ready) {
        return platform::rpc_status(ErrorCode::live_setup_timeout);
    }
    auto [status, sdp] = future.get();
    answer->set_sdp(std::move(sdp));
    return status;
}
} // namespace bacuri::vms::media
