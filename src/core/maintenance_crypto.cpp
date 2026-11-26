#include "maintenance.hpp"
#include <sodium.h>
#include <array>
#include <fstream>
namespace bacuri::vms::maintenance
{
namespace
{
using Cipher = crypto_secretstream_xchacha20poly1305_state;
constexpr auto overhead = crypto_secretstream_xchacha20poly1305_ABYTES;
constexpr auto final_tag = crypto_secretstream_xchacha20poly1305_TAG_FINAL;
struct Secret {
    std::array<unsigned char, crypto_secretstream_xchacha20poly1305_KEYBYTES> bytes{};
    Secret() = default;
    Secret(const Secret&) = delete;
    Secret& operator=(const Secret&) = delete;
    ~Secret()
    {
        sodium_memzero(bytes.data(), bytes.size());
    }
};
} // namespace
void snapshot_cipher(const fs::path& key, const Io& io, bool encrypt)
{
    Secret secret;
    std::ifstream k(key, std::ios::binary);
    k.read(reinterpret_cast<char*>(secret.bytes.data()), secret.bytes.size());
    ensure(k && k.peek() == EOF, "Expected a separate 32-byte recovery key");
    std::ifstream in(io.input, std::ios::binary);
    ensure(in.is_open(), "Cannot read snapshot");
    std::ofstream out;
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out.open(io.output, std::ios::binary);
    Cipher state{};
    std::array<unsigned char, crypto_secretstream_xchacha20poly1305_HEADERBYTES> header{};
    if (encrypt) {
        ensure(crypto_secretstream_xchacha20poly1305_init_push(&state, header.data(), secret.bytes.data()) == 0,
               "Cannot initialize snapshot encryption");
        out.write(reinterpret_cast<char*>(header.data()), header.size());
    } else {
        in.read(reinterpret_cast<char*>(header.data()), header.size());
        ensure(in && crypto_secretstream_xchacha20poly1305_init_pull(&state, header.data(), secret.bytes.data()) ==
                         0,
               "Invalid snapshot header");
    }
    constexpr std::size_t chunk = 65536;
    std::array<unsigned char, chunk + overhead> buffer{}, result{};
    for (;;) {
        in.read(reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(encrypt ? chunk : buffer.size()));
        auto count = static_cast<unsigned long long>(in.gcount());
        ensure(!in.bad(), "Snapshot read failed");
        const bool last = in.peek() == EOF;
        unsigned long long size{};
        if (encrypt) {
            ensure(crypto_secretstream_xchacha20poly1305_push(&state, result.data(), &size, buffer.data(), count,
                                                              nullptr, 0, last ? final_tag : 0) == 0,
                   "Cannot encrypt snapshot");
        } else {
            unsigned char tag{};
            ensure(crypto_secretstream_xchacha20poly1305_pull(&state, result.data(), &size, &tag, buffer.data(),
                                                              count, nullptr, 0) == 0 &&
                       tag == (last ? final_tag : 0),
                   "Snapshot authentication failed");
        }
        out.write(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size));
        if (last)
            break;
    }
    out.close();
    sodium_memzero(&state, sizeof(state));
}
} // namespace bacuri::vms::maintenance
