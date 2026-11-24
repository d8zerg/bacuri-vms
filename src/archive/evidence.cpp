#include "archive/evidence.hpp"
#include "archive/mp4.hpp"
#include <sodium.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <set>

namespace bacuri::vms::archive
{
namespace
{
void require(bool ok)
{
    if (!ok)
        throw std::invalid_argument("Invalid evidence package");
}
struct SigningKey {
    std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> pub;
    std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secret;
    explicit SigningKey(const control::Key& seed)
    {
        crypto_sign_seed_keypair(pub.data(), secret.data(), seed.data());
    }
    ~SigningKey()
    {
        sodium_memzero(secret.data(), secret.size());
    }
};
std::string hex(std::span<const unsigned char> bytes)
{
    std::string result(bytes.size() * 2 + 1, '\0');
    sodium_bin2hex(result.data(), result.size(), bytes.data(), bytes.size());
    result.pop_back();
    return result;
}
template <std::size_t N> auto unhex(const std::string& s)
{
    std::array<unsigned char, N> bytes{};
    require(s.size() == 2 * N &&
            sodium_hex2bin(bytes.data(), N, s.data(), s.size(), nullptr, nullptr, nullptr) == 0);
    return bytes;
}
std::string read_text(const std::filesystem::path& p, std::size_t limit)
{
    require(is_regular_file(symlink_status(p)) && file_size(p) <= limit);
    std::ifstream f{p, std::ios::binary};
    f.exceptions(std::ios::badbit);
    require(f.is_open());
    return {std::istreambuf_iterator<char>{f}, {}};
}
} // namespace
MediaDigest file_digest(const std::filesystem::path& path)
{
    const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("Cannot open evidence media");
    struct Close {
        int fd;
        ~Close()
        {
            close(fd);
        }
    } handle{fd};
    struct stat s{};
    require(fstat(fd, &s) == 0 && S_ISREG(s.st_mode) && s.st_size > 0 && s.st_size <= evidence_limit);
    return digest_media(fd);
}
Json export_public_key(const control::Key& seed, const std::string& site)
{
    return {{"site_id", site}, {"algorithm", "Ed25519"}, {"public_key", hex(SigningKey(seed).pub)}};
}
std::string sign_description(const control::Key& seed, const std::string& description)
{
    SigningKey key(seed);
    std::array<unsigned char, crypto_sign_BYTES> sig;
    crypto_sign_detached(sig.data(), nullptr, reinterpret_cast<const unsigned char*>(description.data()),
                         description.size(), key.secret.data());
    return hex(sig);
}
static Json verify_directory(const std::filesystem::path& dir, const Json& trusted, bool allow_case)
{
    const auto description = read_text(dir / "manifest.json", 4uz * 1024 * 1024);
    const auto sig = unhex<crypto_sign_BYTES>(read_text(dir / "manifest.sig", 2uz * crypto_sign_BYTES));
    require(trusted.at("algorithm") == "Ed25519");
    const auto pub = unhex<crypto_sign_PUBLICKEYBYTES>(trusted.at("public_key"));
    require(crypto_sign_verify_detached(sig.data(), reinterpret_cast<const unsigned char*>(description.data()),
                                        description.size(), pub.data()) == 0);
    const auto m = Json::parse(description);
    if (allow_case && m.at("format") == "bacuri.vms.case.v1") {
        const auto& c = m.at("case");
        const auto& parts = m.at("parts");
        require(m.at("site_id") == trusted.at("site_id") &&
                control::canonical_id(c.at("id").get_ref<const std::string&>()) && !m.at("reason").empty() &&
                parts.is_array() && !parts.empty() && parts.size() <= 32 &&
                parts.size() == c.at("materials").size());
        std::set<std::string> expected{"manifest.json", "manifest.sig"};
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto name = "material-" + std::to_string(i + 1);
            const auto& part = parts.at(i);
            const auto& item = c.at("materials").at(i);
            require(part.at("directory") == name && part.at("point_id") == item.at("point_id") &&
                    std::filesystem::is_directory(std::filesystem::symlink_status(dir / name)) &&
                    part.at("manifest_sha256") == file_digest(dir / name / "manifest.json").sha256);
            const auto inner = verify_directory(dir / name, trusted, false);
            require(inner.at("mode") == m.at("mode") && inner.at("point_id") == part.at("point_id") &&
                    inner.at("begin_utc_us") == part.at("begin_utc_us") &&
                    inner.at("end_utc_us") == part.at("end_utc_us"));
            expected.insert(name);
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir))
            require(expected.erase(entry.path().filename().string()) == 1);
        require(expected.empty());
        return m;
    }
    const bool v2 = m.at("format") == "bacuri.vms.export.v2";
    require((m.at("format") == "bacuri.vms.export.v1" || v2) && m.at("site_id") == trusted.at("site_id") &&
            control::canonical_id(m.at("point_id").get_ref<const std::string&>()) &&
            control::canonical_id(m.at("export_id").get_ref<const std::string&>()));
    std::set<std::string> files{"manifest.json", "manifest.sig"};
    auto cursor = m.at("begin_utc_us").get<std::int64_t>();
    const auto end = m.at("end_utc_us").get<std::int64_t>();
    require(cursor < end && m.at("periods").is_array());
    int clips = 0;
    for (const auto& span : m.at("periods")) {
        const auto a = span.at("begin_utc_us").get<std::int64_t>(), b = span.at("end_utc_us").get<std::int64_t>();
        require(a == cursor && a < b && b <= end);
        cursor = b;
        if (span.at("state") == "gap") {
            require(!span.at("reason").get_ref<const std::string&>().empty());
            continue;
        }
        const auto file = "clip-" + std::to_string(++clips) + ".mp4";
        require(span.at("state") == "recorded" && span.at("file") == file);
        const auto& source = span.at("source");
        validate_manifest(source);
        require(source.at("point_id") == m.at("point_id") && source.at("site_id") == m.at("site_id") &&
                source.at("begin_utc_us") <= a && b <= source.at("end_utc_us"));
        const auto digest = file_digest(dir / file);
        require(span.at("sha256") == digest.sha256 && digest.bytes == span.at("size_bytes"));
        if (span.at("transform") == "original") {
            require(source.at("media").at("sha256") == digest.sha256 && source.at("begin_utc_us") == a &&
                    source.at("end_utc_us") == b);
        } else if (v2 && span.at("transform") == "original_with_decode_context") {
            require(m.at("mode") == "original" &&
                    digest.sha256 == source.at("media").at("sha256").get<std::string>());
        } else
            require((span.at("transform") == "h264_frame_selection" ||
                     (v2 && span.at("transform") == "h264_masked_selection")) &&
                    span.at("frame_count") > 0);
        if (v2) {
            const bool original =
                span.at("transform") == "original" || span.at("transform") == "original_with_decode_context";
            require(span.at("codec") ==
                    (original ? source.at("media").at("tracks").at(0).at("codec") : Json("h264")));
            require(span.at("masks").is_array() &&
                    (span.at("transform") == "h264_masked_selection") == !span.at("masks").empty());
            if (!span.at("audio").is_null()) {
                const auto& audio = span.at("audio");
                const auto name = "audio-" + std::to_string(clips) + ".wav";
                require(m.at("audio_requested") == true && audio.at("file") == name &&
                        audio.at("codec") == "pcm_s16le" && audio.at("channels") == 1 &&
                        audio.at("sample_rate") == 8000);
                const auto ab = audio.at("begin_utc_us").get<std::int64_t>(),
                           ae = audio.at("end_utc_us").get<std::int64_t>();
                require(a <= ab && ab < ae && ae <= b && (ae - ab) % 125 == 0);
                const auto d = file_digest(dir / name);
                require(audio.at("sha256") == d.sha256 && audio.at("size_bytes") == d.bytes &&
                        d.bytes == 44 + 2 * ((ae - ab) / 125));
                files.insert(name);
            }
        }
        files.insert(file);
    }
    require(clips > 0 && cursor == end);
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        require(files.erase(entry.path().filename().string()) == 1);
    require(files.empty());
    return m;
}
Json verify_evidence(const std::filesystem::path& dir, const Json& trusted)
{
    return verify_directory(dir, trusted, true);
}
} // namespace bacuri::vms::archive
