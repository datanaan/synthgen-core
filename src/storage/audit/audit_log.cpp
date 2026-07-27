#include "storage/audit/audit_log.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <sstream>
#include <chrono>
#include <algorithm>

namespace synthgen::storage::audit {

AuditLog::AuditLog() = default;

std::string AuditLog::generate_id() {
    return "aud_" + std::to_string(next_id_++);
}

std::string AuditLog::compute_content_hash(const AuditRecord& record) const {
    std::ostringstream oss;
    oss << record.operation << "|"
        << record.actor_identity << "|"
        << record.timestamp << "|";
    for (const auto& [k, v] : record.metadata) {
        oss << k << "=" << v << ";";
    }
    return sha256_hex(oss.str());
}

std::string AuditLog::compute_chain_hash(
    const std::string& prev_hash, const std::string& content_hash) const {
    return sha256_hex(prev_hash + content_hash);
}

Result<void> AuditLog::create_genesis() {
    scaffold::SpanGuard span("audit", "create_genesis", "audit_gen");

    std::lock_guard<std::mutex> lock(mutex_);

    if (genesis_created_) {
        return Error(ErrorCode::kAlreadyExists,
                     "Genesis record already exists", "audit_log");
    }

    AuditRecord genesis;
    genesis.record_id = generate_id();
    genesis.operation = "genesis";
    genesis.actor_identity = "system";
    genesis.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    genesis.prev_hash = "0";
    genesis.content_hash = compute_content_hash(genesis);
    genesis.chain_hash = compute_chain_hash(genesis.prev_hash, genesis.content_hash);

    records_.push_back(genesis);
    genesis_created_ = true;

    scaffold::MetricsRegistry::instance().counter("audit_records_total").increment();

    return {};
}

Result<AuditRecord> AuditLog::append(
    const std::string& operation,
    const std::string& actor_identity,
    const std::map<std::string, std::string>& metadata) {

    scaffold::SpanGuard span("audit", "append", "audit_app");

    std::lock_guard<std::mutex> lock(mutex_);

    if (!genesis_created_) {
        return Error(ErrorCode::kInvalidState,
                     "Genesis record must be created first", "audit_log");
    }

    if (operation.empty()) {
        return Error(ErrorCode::kInvalidArgument,
                     "Operation is required", "audit_log");
    }

    AuditRecord record;
    record.record_id = generate_id();
    record.operation = operation;
    record.actor_identity = actor_identity;
    record.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    record.metadata = metadata;

    // Chain to previous record
    record.prev_hash = records_.back().chain_hash;
    record.content_hash = compute_content_hash(record);
    record.chain_hash = compute_chain_hash(record.prev_hash, record.content_hash);

    records_.push_back(record);

    scaffold::MetricsRegistry::instance().counter("audit_records_total").increment();

    return record;
}

Result<bool> AuditLog::verify_chain() {
    scaffold::SpanGuard span("audit", "verify", "audit_verify");

    std::lock_guard<std::mutex> lock(mutex_);

    if (records_.empty()) return true;

    for (size_t i = 1; i < records_.size(); ++i) {
        const auto& prev = records_[i - 1];
        const auto& curr = records_[i];

        // Verify prev_hash linkage
        if (curr.prev_hash != prev.chain_hash) {
            return false;
        }

        // Verify content hash
        auto expected_content = compute_content_hash(curr);
        if (curr.content_hash != expected_content) {
            return false;
        }

        // Verify chain hash
        auto expected_chain = compute_chain_hash(curr.prev_hash, curr.content_hash);
        if (curr.chain_hash != expected_chain) {
            return false;
        }
    }
    return true;
}

Result<ChainVerificationReport> AuditLog::daily_verification() {
    scaffold::SpanGuard span("audit", "daily_verify", "audit_daily");

    std::lock_guard<std::mutex> lock(mutex_);

    ChainVerificationReport report;
    report.total_records = static_cast<int64_t>(records_.size());
    report.is_valid = true;

    if (records_.empty()) return report;

    for (size_t i = 1; i < records_.size(); ++i) {
        const auto& prev = records_[i - 1];
        const auto& curr = records_[i];

        if (curr.prev_hash != prev.chain_hash) {
            report.broken_links.push_back(curr.record_id);
            report.is_valid = false;
        }

        auto expected_content = compute_content_hash(curr);
        if (curr.content_hash != expected_content) {
            report.broken_links.push_back(curr.record_id);
            report.is_valid = false;
        }

        auto expected_chain = compute_chain_hash(curr.prev_hash, curr.content_hash);
        if (curr.chain_hash != expected_chain) {
            report.broken_links.push_back(curr.record_id);
            report.is_valid = false;
        }

        report.verified_records++;
    }

    return report;
}

Result<std::vector<ForkDetection>> AuditLog::detect_forks() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ForkDetection> forks;

    // Group records by prev_hash
    std::map<std::string, std::vector<std::string>> prev_hash_groups;
    for (const auto& r : records_) {
        prev_hash_groups[r.prev_hash].push_back(r.record_id);
    }

    for (const auto& [prev_hash, ids] : prev_hash_groups) {
        if (ids.size() > 1) {
            ForkDetection fd;
            fd.prev_hash = prev_hash;
            fd.competing_next = ids;
            forks.push_back(fd);
        }
    }

    return forks;
}

Result<AuditRecord> AuditLog::get_latest() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (records_.empty()) {
        return Error(ErrorCode::kNotFound, "No audit records", "audit_log");
    }
    return records_.back();
}

Result<std::vector<AuditRecord>> AuditLog::scan(
    const std::optional<Timestamp>& from,
    const std::optional<Timestamp>& to,
    int64_t limit) {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<AuditRecord> result;
    for (const auto& r : records_) {
        if (from.has_value() && r.timestamp < *from) continue;
        if (to.has_value() && r.timestamp > *to) continue;
        result.push_back(r);
        if (limit > 0 && static_cast<int64_t>(result.size()) >= limit) break;
    }
    return result;
}

}  // namespace synthgen::storage::audit
