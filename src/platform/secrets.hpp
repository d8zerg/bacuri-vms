#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace bacuri::vms::platform
{
using Key = std::array<unsigned char, 32>;

// Reads a 32-byte key stored as one line of hexadecimal text.
Key load_key(const std::filesystem::path& path);
// 32 random bytes in hexadecimal text.
std::string token();
// Keyed BLAKE2b digest of a value for one purpose.
std::string fingerprint(const Key& key, std::string_view purpose, std::string_view value);
// XChaCha20-Poly1305 encryption bound to a context. The context is required to decrypt.
std::string seal(const Key& key, std::string_view context, std::string_view plaintext);
std::string unseal(std::string_view ciphertext, const Key& key, std::string_view context);
// Argon2id password hash in the libsodium string format.
std::string password_hash(std::string_view password);
bool password_matches(const std::string& hash, std::string_view password);
} // namespace bacuri::vms::platform
