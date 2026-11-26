#include "platform/secrets.hpp"

#include <sodium.h>

#include <algorithm>
#include <fstream>
#include <span>
#include <stdexcept>
#include <vector>

namespace bacuri::vms::platform
{
namespace
{

std::string hex(std::span<const unsigned char> bytes)
{
    std::string result(bytes.size() * 2 + 1, '\0');
    sodium_bin2hex(result.data(), result.size(), bytes.data(), bytes.size());
    result.pop_back();
    return result;
}

std::vector<unsigned char> decode(const std::string& value)
{
    std::vector<unsigned char> bytes(value.size() / 2);
    if (value.empty() || value.size() % 2 != 0 ||
        sodium_hex2bin(bytes.data(), bytes.size(), value.data(), value.size(), nullptr, nullptr, nullptr) != 0) {
        throw std::runtime_error("Invalid encoded key or ciphertext");
    }
    return bytes;
}

auto bytes(std::string_view text)
{
    return reinterpret_cast<const unsigned char*>(text.data());
}

} // namespace

Key load_key(const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::string line;
    std::getline(input, line);
    const auto decoded = decode(line);
    Key key{};
    if (decoded.size() != key.size()) {
        throw std::runtime_error("Expected 32-byte encryption key");
    }
    std::ranges::copy(decoded, key.begin());
    return key;
}

std::string token()
{
    Key random{};
    randombytes_buf(random.data(), random.size());
    return hex(random);
}

std::string fingerprint(const Key& key, std::string_view purpose, std::string_view value)
{
    crypto_generichash_state state{};
    Key digest{};
    crypto_generichash_init(&state, key.data(), key.size(), digest.size());
    crypto_generichash_update(&state, bytes(purpose), purpose.size());
    const unsigned char separator = 0;
    crypto_generichash_update(&state, &separator, 1);
    crypto_generichash_update(&state, bytes(value), value.size());
    crypto_generichash_final(&state, digest.data(), digest.size());
    return hex(digest);
}

std::string seal(const Key& key, std::string_view context, std::string_view plaintext)
{
    constexpr auto nonce_size = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    std::vector<unsigned char> result(nonce_size + plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    randombytes_buf(result.data(), nonce_size);
    crypto_aead_xchacha20poly1305_ietf_encrypt(result.data() + nonce_size, nullptr, bytes(plaintext),
                                               plaintext.size(), bytes(context), context.size(), nullptr,
                                               result.data(), key.data());
    return "k1:" + hex(result);
}

std::string unseal(std::string_view ciphertext, const Key& key, std::string_view context)
{
    constexpr auto nonce_size = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    constexpr auto overhead = nonce_size + crypto_aead_xchacha20poly1305_ietf_ABYTES;
    if (!ciphertext.starts_with("k1:")) {
        throw std::runtime_error("Unknown configuration key version");
    }
    const auto input = decode(std::string{ciphertext.substr(3)});
    if (input.size() < overhead) {
        throw std::runtime_error("Incomplete ciphertext");
    }
    std::string result(input.size() - overhead, '\0');
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            reinterpret_cast<unsigned char*>(result.data()), nullptr, nullptr, input.data() + nonce_size,
            input.size() - nonce_size, bytes(context), context.size(), input.data(), key.data()) != 0) {
        throw std::runtime_error("Configuration authentication failed");
    }
    return result;
}

std::string password_hash(std::string_view password)
{
    std::array<char, crypto_pwhash_STRBYTES> hash{};
    if (crypto_pwhash_str_alg(hash.data(), password.data(), password.size(), crypto_pwhash_OPSLIMIT_INTERACTIVE,
                              crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_ARGON2ID13) != 0) {
        throw std::runtime_error("Password hashing failed");
    }
    return hash.data();
}

bool password_matches(const std::string& hash, std::string_view password)
{
    return crypto_pwhash_str_verify(hash.c_str(), password.data(), password.size()) == 0;
}

} // namespace bacuri::vms::platform
