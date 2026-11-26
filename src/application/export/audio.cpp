#include "application/export/package.hpp"
#include "archive/evidence.hpp"
#include "media/lease.hpp"

namespace bacuri::vms::control
{
using namespace std::chrono_literals;
Json export_audio(const archive::BlockManifest& source, std::int64_t begin, std::int64_t end, const fs::path& dir,
                  const std::string& file)
{
    if (!source.audio)
        return nullptr;
    const auto& a = *source.audio;
    const auto start = a.start_us, size = a.size_bytes;
    using Pcm = domain::ArchiveSound;
    const auto samples = size / Pcm::bytes_per_sample;
    const auto first =
        std::clamp<std::int64_t>((begin - start + Pcm::sample_period_us - 1) / Pcm::sample_period_us, 0, samples);
    const auto last = std::clamp<std::int64_t>((end - start) / Pcm::sample_period_us, 0, samples);
    if (last <= first)
        return nullptr;
    std::string pcm;
    auto stub = v1::RecorderControl::NewStub(media::local_channel("BACURI_SOCKET"));
    for (auto offset = first * Pcm::bytes_per_sample; offset < last * Pcm::bytes_per_sample;) {
        const auto length = std::min(Pcm::max_chunk_bytes, last * Pcm::bytes_per_sample - offset);
        v1::AudioRequest q;
        q.set_mode(v1::AudioRequest::ARCHIVE);
        q.set_site_id(source.recording.block().site.to_string());
        q.set_point_id(source.recording.binding().point.to_string());
        q.set_block_id(source.recording.block().id.to_string());
        q.set_sha256(archive::digest_hex(a.sha256));
        q.set_at_us(offset);
        q.set_length(static_cast<std::uint32_t>(length));
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + 2s);
        v1::AudioChunk chunk;
        if (!stub->ExchangeAudio(&ctx, q, &chunk).ok() || chunk.pcm().size() != static_cast<std::size_t>(length))
            throw Failure{ErrorCode::media_service_unavailable};
        pcm += chunk.pcm();
        offset += length;
    }
    std::string wav = "RIFF";
    // A WAV file stores the sizes in 32 bits.
    if (pcm.size() > std::numeric_limits<std::uint32_t>::max() - 36)
        throw Failure{ErrorCode::export_too_large};
    const auto data_size = static_cast<std::uint32_t>(pcm.size());
    auto le = [&](std::uint32_t value, int count) {
        while (count--) {
            wav.push_back(static_cast<char>(value & 255));
            value >>= 8;
        }
    };
    le(36 + data_size, 4);
    wav += "WAVEfmt ";
    le(16, 4);
    le(1, 2);
    le(1, 2);
    le(Pcm::sample_rate, 4);
    le(Pcm::sample_rate * Pcm::bytes_per_sample, 4);
    le(Pcm::bytes_per_sample, 2);
    le(16, 2);
    wav += "data";
    le(data_size, 4);
    wav += pcm;
    write_text(dir / file, wav);
    const auto digest = archive::file_digest(dir / file);
    return {{"file", file},
            {"codec", "pcm_s16le"},
            {"sample_rate", Pcm::sample_rate},
            {"channels", 1},
            {"sha256", digest.sha256},
            {"size_bytes", digest.bytes},
            {"begin_utc_us", start + first * Pcm::sample_period_us},
            {"end_utc_us", start + last * Pcm::sample_period_us}};
}
} // namespace bacuri::vms::control
