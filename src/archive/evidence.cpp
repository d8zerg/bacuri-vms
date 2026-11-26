#include "archive/evidence.hpp"
#include <sodium.h>
#include <array>
#include <span>

namespace bacuri::vms::archive
{
namespace
{
struct SigningKey {
    std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> pub{};
    std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secret{};
    explicit SigningKey(const platform::Key& seed)
    {
        crypto_sign_seed_keypair(pub.data(), secret.data(), seed.data());
    }
    SigningKey(const SigningKey&) = delete;
    SigningKey& operator=(const SigningKey&) = delete;
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
} // namespace
Json export_public_key(const platform::Key& seed, const std::string& site)
{
    return {{"site_id", site}, {"algorithm", "Ed25519"}, {"public_key", hex(SigningKey(seed).pub)}};
}
std::string sign_description(const platform::Key& seed, const std::string& description)
{
    SigningKey key(seed);
    std::array<unsigned char, crypto_sign_BYTES> sig{};
    crypto_sign_detached(sig.data(), nullptr, reinterpret_cast<const unsigned char*>(description.data()),
                         description.size(), key.secret.data());
    return hex(sig);
}
} // namespace bacuri::vms::archive
