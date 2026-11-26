#pragma once
#include "archive/block_journal.hpp"

namespace bacuri::vms::archive
{
// Committed blocks are verified again after this interval even when the volume stays open.
inline constexpr std::chrono::days reverification_interval{30};
struct VerificationResult {
    std::string block_id;
    // Media and sound bytes read from the primary volume and, during repair, from the copy.
    std::uintmax_t bytes_read{};
    bool available{};
};
class BlockVerification
{
  public:
    ~BlockVerification() = default;
    BlockVerification(const BlockVerification&) = delete;
    BlockVerification& operator=(const BlockVerification&) = delete;
    // Verifies the checksums of one due block and repairs it from the archive copy when possible.
    // Returns nothing when no block is due.
    std::optional<VerificationResult> verify_next();
    // Verifies every block that is due, for recovery tools that must not report unverified media.
    void verify_all();
    bool scrub_copy();

  private:
    friend class Volume;
    BlockVerification(BlockJournal& journal, std::mutex& replica_mutex, const std::optional<fs::path>& replica)
        : journal_{journal}, replica_mutex_{replica_mutex}, replica_{replica}
    {
    }
    BlockJournal& journal_;
    std::mutex& replica_mutex_;
    const std::optional<fs::path>& replica_;
};
} // namespace bacuri::vms::archive
