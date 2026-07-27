#pragma once

#include "common/result.h"
#include "common/hash.h"
#include "common/types.h"

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>
#include <deque>
#include <mutex>

namespace synthgen::storage::audit {

struct AuditRecord {
    std::string record_id;
    std::string operation;
    std::string actor_identity;
    Timestamp timestamp = 0;
    std::string prev_hash;
    std::string content_hash;
    std::string chain_hash;
    std::map<std::string, std::string> metadata;
};

struct ChainVerificationReport {
    bool is_valid = true;
    int64_t total_records = 0;
    int64_t verified_records = 0;
    std::vector<std::string> broken_links;
    std::vector<std::string> fork_points;
};

struct ForkDetection {
    std::string record_id;
    std::string prev_hash;
    std::vector<std::string> competing_next;
};

class AuditLog {
public:
    AuditLog();

    Result<void> create_genesis();

    Result<AuditRecord> append(
        const std::string& operation,
        const std::string& actor_identity,
        const std::map<std::string, std::string>& metadata = {});

    Result<bool> verify_chain();

    Result<ChainVerificationReport> daily_verification();

    Result<std::vector<ForkDetection>> detect_forks();

    Result<AuditRecord> get_latest();

    Result<std::vector<AuditRecord>> scan(
        const std::optional<Timestamp>& from,
        const std::optional<Timestamp>& to,
        int64_t limit = 1000);

    int64_t record_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int64_t>(records_.size());
    }

private:
    mutable std::mutex mutex_;
    std::deque<AuditRecord> records_;
    bool genesis_created_ = false;
    int64_t next_id_ = 1;

    std::string generate_id();
    std::string compute_content_hash(const AuditRecord& record) const;
    std::string compute_chain_hash(const std::string& prev_hash,
                                    const std::string& content_hash) const;
};

}  // namespace synthgen::storage::audit
