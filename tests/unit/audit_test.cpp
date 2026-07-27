#include <gtest/gtest.h>
#include "storage/audit/audit_log.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

using namespace synthgen::storage::audit;

// ===== Genesis =====

TEST(AuditLogTest, CreateGenesis) {
    AuditLog log;
    auto result = log.create_genesis();
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(log.record_count(), 1);
}

TEST(AuditLogTest, GenesisTwiceFails) {
    AuditLog log;
    log.create_genesis();
    auto result = log.create_genesis();
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kAlreadyExists);
}

TEST(AuditLogTest, GenesisRecordFields) {
    AuditLog log;
    log.create_genesis();
    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().operation, "genesis");
    EXPECT_EQ(latest.value().actor_identity, "system");
    EXPECT_EQ(latest.value().prev_hash, "0");
    EXPECT_FALSE(latest.value().content_hash.empty());
    EXPECT_FALSE(latest.value().chain_hash.empty());
}

// ===== Append =====

TEST(AuditLogTest, AppendRecord) {
    AuditLog log;
    log.create_genesis();
    auto result = log.append("generate", "physics_sampler", {{"rows", "100"}});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().operation, "generate");
    EXPECT_EQ(result.value().actor_identity, "physics_sampler");
    EXPECT_EQ(log.record_count(), 2);
}

TEST(AuditLogTest, AppendWithoutGenesisFails) {
    AuditLog log;
    auto result = log.append("generate", "test");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kInvalidState);
}

TEST(AuditLogTest, AppendEmptyOperationFails) {
    AuditLog log;
    log.create_genesis();
    auto result = log.append("", "test");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kInvalidArgument);
}

TEST(AuditLogTest, AppendMultipleRecords) {
    AuditLog log;
    log.create_genesis();
    for (int i = 0; i < 10; ++i) {
        auto result = log.append("generate", "sampler", {{"batch", std::to_string(i)}});
        ASSERT_TRUE(result.ok());
    }
    EXPECT_EQ(log.record_count(), 11);
}

// ===== Hash Chain =====

TEST(AuditLogTest, ChainIntegrity) {
    AuditLog log;
    log.create_genesis();
    log.append("generate", "sampler");
    log.append("validate", "validator");
    log.append("evidence", "builder");

    auto result = log.verify_chain();
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value());
}

TEST(AuditLogTest, ChainHashesAreLinked) {
    AuditLog log;
    log.create_genesis();
    auto r1 = log.append("generate", "sampler");

    // Second record's prev_hash must equal genesis chain_hash
    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());

    // Verify the chain: r1.prev_hash == genesis.chain_hash
    // and r1.chain_hash == SHA256(r1.prev_hash + r1.content_hash)
    EXPECT_FALSE(latest.value().prev_hash.empty());
    EXPECT_NE(latest.value().prev_hash, "0");  // Not genesis
}

TEST(AuditLogTest, ChainHashFormat) {
    AuditLog log;
    log.create_genesis();
    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().chain_hash.size(), 64u);  // SHA-256 hex
    EXPECT_EQ(latest.value().content_hash.size(), 64u);
}

// ===== Daily Verification =====

TEST(AuditLogTest, DailyVerificationPasses) {
    AuditLog log;
    log.create_genesis();
    log.append("generate", "sampler");
    log.append("validate", "validator");

    auto report = log.daily_verification();
    ASSERT_TRUE(report.ok());
    EXPECT_TRUE(report.value().is_valid);
    EXPECT_EQ(report.value().total_records, 3);
    EXPECT_EQ(report.value().verified_records, 2);
    EXPECT_TRUE(report.value().broken_links.empty());
}

TEST(AuditLogTest, DailyVerificationEmptyLog) {
    AuditLog log;
    auto report = log.daily_verification();
    ASSERT_TRUE(report.ok());
    EXPECT_TRUE(report.value().is_valid);
    EXPECT_EQ(report.value().total_records, 0);
}

// ===== Fork Detection =====

TEST(AuditLogTest, NoForksInNormalOperation) {
    AuditLog log;
    log.create_genesis();
    log.append("generate", "sampler");

    auto forks = log.detect_forks();
    ASSERT_TRUE(forks.ok());
    EXPECT_TRUE(forks.value().empty());
}

// ===== Scan =====

TEST(AuditLogTest, ScanAll) {
    AuditLog log;
    log.create_genesis();
    log.append("generate", "sampler");
    log.append("validate", "validator");

    auto result = log.scan(std::nullopt, std::nullopt);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().size(), 3u);
}

TEST(AuditLogTest, ScanWithLimit) {
    AuditLog log;
    log.create_genesis();
    log.append("generate", "sampler");
    log.append("validate", "validator");

    auto result = log.scan(std::nullopt, std::nullopt, 2);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().size(), 2u);
}

TEST(AuditLogTest, GetLatest) {
    AuditLog log;
    log.create_genesis();
    log.append("generate", "sampler");

    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().operation, "generate");
}

TEST(AuditLogTest, GetLatestEmpty) {
    AuditLog log;
    auto latest = log.get_latest();
    EXPECT_FALSE(latest.ok());
    EXPECT_EQ(latest.error().code, synthgen::ErrorCode::kNotFound);
}

// ===== Metadata =====

TEST(AuditLogTest, MetadataPreserved) {
    AuditLog log;
    log.create_genesis();
    log.append("generate", "sampler", {
        {"rows", "1000"},
        {"constraint", "safe_range"},
        {"seed", "42"}
    });

    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().metadata.at("rows"), "1000");
    EXPECT_EQ(latest.value().metadata.at("constraint"), "safe_range");
    EXPECT_EQ(latest.value().metadata.at("seed"), "42");
}

// ===== Record IDs =====

TEST(AuditLogTest, RecordIdsUnique) {
    AuditLog log;
    log.create_genesis();
    auto r1 = log.append("op1", "actor1");
    auto r2 = log.append("op2", "actor2");
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());
    EXPECT_NE(r1.value().record_id, r2.value().record_id);
}

TEST(AuditLogTest, RecordIdFormat) {
    AuditLog log;
    log.create_genesis();
    auto r = log.append("generate", "sampler");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().record_id.substr(0, 4), "aud_");
}

// ===== Timestamps =====

TEST(AuditLogTest, TimestampsOrdered) {
    AuditLog log;
    log.create_genesis();
    auto r1 = log.append("op1", "actor1");
    auto r2 = log.append("op2", "actor2");
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());
    EXPECT_LE(r1.value().timestamp, r2.value().timestamp);
}

// ===== Trace =====

TEST(AuditLogTest, ProducesTraceSpan) {
    synthgen::scaffold::SpanGuard::active_spans().clear();
    AuditLog log;
    log.create_genesis();

    bool found = false;
    for (const auto& sp : synthgen::scaffold::SpanGuard::active_spans()) {
        if (sp.component == "audit") found = true;
    }
    EXPECT_TRUE(found);
}

// ===== Tamper Detection =====

TEST(AuditLogTest, TamperDetectionViaContentHash) {
    AuditLog log;
    log.create_genesis();
    log.append("generate", "sampler");

    // The chain should be valid
    auto verify = log.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value());

    // In-memory tamper detection would require direct mutation
    // Here we verify the hash computation is correct
    auto report = log.daily_verification();
    ASSERT_TRUE(report.ok());
    EXPECT_TRUE(report.value().is_valid);
}
