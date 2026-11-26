#include "archive/manifest_json.hpp"
#include "archive/block_verification.hpp"
#include "archive/retention.hpp"
#include "archive/replica.hpp"
#include "archive/volume_io.hpp"
#include "platform/identifiers.hpp"
#include <sys/file.h>

namespace bacuri::vms::archive
{
using storage::bind_text;
using storage::column;
using storage::next;
bool BlockVerification::scrub_copy()
{
    auto replica = replica_blocks(replica_, journal_.root_, journal_.identity_.owner);
    if (!replica)
        return false;
    fs::create_directory(*replica);
    if (!fs::is_directory(fs::symlink_status(*replica)))
        throw std::runtime_error("Invalid archive copy volume");
    std::scoped_lock disk{replica_mutex_};
    std::string id;
    std::string journaled;
    {
        std::scoped_lock guard{journal_.mutex_};
        auto expired = journal_.db_.query("SELECT block_id FROM replica_deletions LIMIT 1");
        if (next(expired.get())) {
            id = column(expired.get(), 0);
            expired.reset();
            if (!platform::canonical_id(id))
                throw std::runtime_error("Invalid expired copy");
            remove_copy(*replica / id);
            auto done = journal_.db_.query("DELETE FROM replica_deletions WHERE block_id=?");
            bind_text(done.get(), 1, id);
            next(done.get());
            return true;
        }
        auto row = journal_.db_.query(
            "SELECT b.block_id,b.manifest_json FROM committed_blocks b LEFT JOIN replica_checks c USING(block_id) "
            "WHERE c.checked_us IS NULL OR c.checked_us<? ORDER BY c.checked_us NULLS FIRST,b.committed_at_utc_us "
            "LIMIT 1");
        sqlite3_bind_int64(row.get(), 1, now_us() - 60000000);
        if (!next(row.get()))
            return false;
        id = column(row.get(), 0);
        journaled = column(row.get(), 1);
    }
    const auto manifest = parse_manifest(Json::parse(journaled));
    if (!platform::canonical_id(id) || manifest.recording.block().id.to_string() != id)
        throw std::runtime_error("Invalid archive copy identity");
    auto primary = journal_.root_ / "blocks" / id, copy = *replica / id;
    bool local = verified_copy(primary, manifest), remote = verified_copy(copy, manifest);
    try {
        if (local != remote) {
            const auto& target = local ? copy : primary;
            const auto free = fs::space(target.parent_path());
            const auto bytes = static_cast<std::uintmax_t>(manifest.size_bytes) +
                               (manifest.audio ? static_cast<std::uintmax_t>(manifest.audio->size_bytes) : 0) +
                               journaled.size();
            int percent = 0;
            {
                std::scoped_lock guard{journal_.mutex_};
                auto policy = journal_.db_.query("SELECT reserve_percent FROM retention");
                next(policy.get());
                percent = sqlite3_column_int(policy.get(), 0);
            }
            if (capacity_state(percent, free, bytes) != CapacityState::recording_stopped) {
                restore_copy(local ? primary : copy, manifest, target);
                local = remote = true;
            }
        }
    } catch (const std::exception&) {
        std::fputs("Archive copy repair deferred\n", stderr);
    }
    std::scoped_lock guard{journal_.mutex_};
    auto existing = journal_.db_.query("SELECT 1 FROM committed_blocks WHERE block_id=?");
    bind_text(existing.get(), 1, id);
    if (!next(existing.get()))
        return false;
    auto save = journal_.db_.query("INSERT OR REPLACE INTO replica_checks VALUES(?,?,?)");
    bind_text(save.get(), 1, id);
    sqlite3_bind_int64(save.get(), 2, now_us());
    sqlite3_bind_int(save.get(), 3, local && remote);
    next(save.get());
    journal_.availability("blocks/" + id, local || remote ? "" : "corrupt");
    return true;
}
std::optional<VerificationResult> BlockVerification::verify_next()
{
    std::scoped_lock disk{replica_mutex_};
    std::string relative, journaled;
    {
        std::scoped_lock guard{journal_.mutex_};
        // Blocks that may have been repaired come first, then blocks not verified since the volume
        // opened, then blocks whose last verification is older than the periodic interval.
        static constexpr const char* due[] = {
            "SELECT relative_directory,manifest_json FROM committed_blocks "
            "WHERE unavailable_reason IS NOT NULL AND verified_generation<?1 LIMIT 1",
            "SELECT relative_directory,manifest_json FROM committed_blocks "
            "WHERE verified_generation<?1 ORDER BY verified_generation LIMIT 1",
            "SELECT relative_directory,manifest_json FROM committed_blocks "
            "WHERE verified_at_utc_us<?2 ORDER BY verified_at_utc_us LIMIT 1"};
        const auto stale =
            now_us() - std::chrono::duration_cast<domain::Microseconds>(reverification_interval).count();
        for (const auto* query : due) {
            auto row = journal_.db_.query(query);
            sqlite3_bind_int64(row.get(), 1, journal_.identity_.generation);
            sqlite3_bind_int64(row.get(), 2, stale);
            if (next(row.get())) {
                relative = column(row.get(), 0);
                journaled = column(row.get(), 1);
                break;
            }
        }
    }
    if (relative.empty())
        return {};
    const auto manifest = parse_manifest(Json::parse(journaled));
    const auto id = manifest.recording.block().id.to_string();
    if (!platform::canonical_id(id) || relative != "blocks/" + id)
        throw std::runtime_error("Invalid archive journal identity");
    const auto media_bytes = static_cast<std::uintmax_t>(manifest.size_bytes) +
                             (manifest.audio ? static_cast<std::uintmax_t>(manifest.audio->size_bytes) : 0);
    const auto primary = journal_.root_ / relative;
    VerificationResult result{
        .block_id = id, .bytes_read = media_bytes, .available = verified_copy(primary, manifest)};
    if (!result.available) {
        try {
            if (const auto copies = replica_blocks(replica_, journal_.root_, journal_.identity_.owner);
                copies && verified_copy(*copies / id, manifest)) {
                restore_copy(*copies / id, manifest, primary);
                result.available = true;
                result.bytes_read += 2 * media_bytes;
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "archive copy repair deferred %s: %s\n", id.c_str(), error.what());
        }
    }
    const auto reason = result.available ? "" : fs::exists(fs::symlink_status(primary)) ? "corrupt" : "missing";
    std::scoped_lock guard{journal_.mutex_};
    auto verified = journal_.db_.query("UPDATE committed_blocks SET verified_generation=?1,verified_at_utc_us=?2 "
                                       "WHERE relative_directory=?3 AND manifest_json=?4");
    sqlite3_bind_int64(verified.get(), 1, journal_.identity_.generation);
    sqlite3_bind_int64(verified.get(), 2, now_us());
    bind_text(verified.get(), 3, relative);
    bind_text(verified.get(), 4, journaled);
    next(verified.get());
    // The row is gone when retention removed the block during verification.
    if (journal_.db_.changes() > 0 && journal_.availability(relative, reason))
        std::fprintf(stderr, "archive block %s %s\n", id.c_str(), *reason ? reason : "available");
    return result;
}

void BlockVerification::verify_all()
{
    while (verify_next()) {
    }
}

} // namespace bacuri::vms::archive
