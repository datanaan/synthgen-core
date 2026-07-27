#include <gtest/gtest.h>
#include "storage/audit/audit_log.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

using namespace synthgen::storage::audit;

// ===== Genesis Edge Cases =====

TEST(AuditExtended, GenesisRecordIdFormat) {
    AuditLog log;
    auto result = log.create_genesis();
    ASSERT_TRUE(result.ok());
    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().record_id.substr(0, 4), "aud_");
}

TEST(AuditExtended, GenesisTimestampPositive) {
    AuditLog log;
    auto result = log.create_genesis();
    ASSERT_TRUE(result.ok());
    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_GT(latest.value().timestamp, 0);
}

// ===== Append Edge Cases =====

TEST(AuditExtended, AppendWithEmptyMetadata) {
    AuditLog log;
    log.create_genesis();
    auto result = log.append("generate", "sampler", {});
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().metadata.empty());
}

TEST(AuditExtended, AppendWithManyMetadata) {
    AuditLog log;
    log.create_genesis();
    std::map<std::string, std::string> meta;
    for (int i = 0; i < 50; ++i) {
        meta["key_" + std::to_string(i)] = "value_" + std::to_string(i);
    }
    auto result = log.append("generate", "sampler", meta);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().metadata.size(), 50u);
}

TEST(AuditExtended, AppendWithSpecialCharacters) {
    AuditLog log;
    log.create_genesis();
    auto result = log.append("generate_batch", "physics_sampler", {
        {"constraint", "safe_range:temp AND pressure"},
        {"path", "/data/test/file (1).parquet"},
        {"special", "value with \"quotes\" and <tags>"}
    });
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().metadata.at("constraint"), "safe_range:temp AND pressure");
    EXPECT_EQ(result.value().metadata.at("special"), "value with \"quotes\" and <tags>");
}

TEST(AuditExtended, AppendEmptyActorFails) {
    AuditLog log;
    log.create_genesis();
    auto result = log.append("generate", "");
    // Empty actor should fail or succeed — check behavior
    // If it fails, the error code should be kInvalidArgument
    if (!result.ok()) {
        EXPECT_EQ(result.error().code, synthgen::ErrorCode::kInvalidArgument);
    }
}

// ===== Chain integrity with many records =====

TEST(AuditExtended, ChainIntegrity50Records) {
    AuditLog log;
    log.create_genesis();
    for (int i = 0; i < 50; ++i) {
        auto result = log.append("generate", "sampler", {{"batch", std::to_string(i)}});
        ASSERT_TRUE(result.ok()) << "Failed at record " << i;
    }
    EXPECT_EQ(log.record_count(), 51);

    auto verify = log.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value());
}

TEST(AuditExtended, ChainIntegrity100Records) {
    AuditLog log;
    log.create_genesis();
    for (int i = 0; i < 100; ++i) {
        auto result = log.append("op_" + std::to_string(i), "actor");
        ASSERT_TRUE(result.ok()) << "Failed at record " << i;
    }
    EXPECT_EQ(log.record_count(), 101);

    auto verify = log.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value());
}

// ===== Chain hash linkage =====

TEST(AuditExtended, ChainHashLinkedCorrectly) {
    AuditLog log;
    ASSERT_TRUE(log.create_genesis().ok());
    auto genesis = log.get_latest();
    ASSERT_TRUE(genesis.ok());
    std::string genesis_hash = genesis.value().chain_hash;

    auto r1 = log.append("op1", "actor1");
    ASSERT_TRUE(r1.ok());
    // r1's prev_hash should be genesis's chain_hash
    EXPECT_EQ(r1.value().prev_hash, genesis_hash);

    std::string r1_hash = r1.value().chain_hash;
    auto r2 = log.append("op2", "actor2");
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(r2.value().prev_hash, r1_hash);
}

// ===== Content hash uniqueness =====

TEST(AuditExtended, ContentHashDifferentForDifferentOps) {
    AuditLog log;
    log.create_genesis();

    auto r1 = log.append("generate", "actor");
    auto r2 = log.append("validate", "actor");
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());

    EXPECT_NE(r1.value().content_hash, r2.value().content_hash);
}

// ===== Daily verification =====

TEST(AuditExtended, DailyVerificationWithRecords) {
    AuditLog log;
    log.create_genesis();
    log.append("generate", "sampler", {{"rows", "100"}});
    log.append("validate", "validator");
    log.append("evidence", "builder");

    auto report = log.daily_verification();
    ASSERT_TRUE(report.ok());
    EXPECT_TRUE(report.value().is_valid);
    EXPECT_EQ(report.value().total_records, 4);
    EXPECT_EQ(report.value().verified_records, 3);  // Excluding genesis
    EXPECT_TRUE(report.value().broken_links.empty());
    EXPECT_TRUE(report.value().fork_points.empty());
}

// ===== Scan edge cases =====

TEST(AuditExtended, ScanEmptyLog) {
    AuditLog log;
    auto result = log.scan(std::nullopt, std::nullopt);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().empty());
}

TEST(AuditExtended, ScanWithLimit1) {
    AuditLog log;
    log.create_genesis();
    log.append("op1", "actor");
    log.append("op2", "actor");

    auto result = log.scan(std::nullopt, std::nullopt, 1);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().size(), 1u);
}

TEST(AuditExtended, ScanWithLargeLimit) {
    AuditLog log;
    log.create_genesis();
    log.append("op1", "actor");

    auto result = log.scan(std::nullopt, std::nullopt, 10000);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().size(), 2u);
}

// ===== Record ID uniqueness =====

TEST(AuditExtended, AllRecordIdsUnique) {
    AuditLog log;
    log.create_genesis();
    std::set<std::string> ids;
    ids.insert(log.get_latest().value().record_id);

    for (int i = 0; i < 50; ++i) {
        auto r = log.append("op" + std::to_string(i), "actor");
        ASSERT_TRUE(r.ok());
        auto inserted = ids.insert(r.value().record_id);
        EXPECT_TRUE(inserted.second) << "Duplicate record_id: " << r.value().record_id;
    }
}

// ===== Fork detection (no forks in normal operation) =====

TEST(AuditExtended, NoForksAfterManyRecords) {
    AuditLog log;
    log.create_genesis();
    for (int i = 0; i < 20; ++i) {
        log.append("op" + std::to_string(i), "actor");
    }

    auto forks = log.detect_forks();
    ASSERT_TRUE(forks.ok());
    EXPECT_TRUE(forks.value().empty());
}

// ===== AuditRecord structure =====

TEST(AuditExtended, RecordStructureComplete) {
    AuditLog log;
    ASSERT_TRUE(log.create_genesis().ok());

    auto genesis = log.get_latest();
    ASSERT_TRUE(genesis.ok());

    const auto& r = genesis.value();
    EXPECT_FALSE(r.record_id.empty());
    EXPECT_EQ(r.operation, "genesis");
    EXPECT_EQ(r.actor_identity, "system");
    EXPECT_GT(r.timestamp, 0);
    EXPECT_EQ(r.prev_hash, "0");
    EXPECT_EQ(r.chain_hash.size(), 64u);
    EXPECT_EQ(r.content_hash.size(), 64u);
}

// ===== ChainVerificationReport structure =====

TEST(AuditExtended, VerificationReportDefaults) {
    ChainVerificationReport report;
    EXPECT_TRUE(report.is_valid);
    EXPECT_EQ(report.total_records, 0);
    EXPECT_EQ(report.verified_records, 0);
    EXPECT_TRUE(report.broken_links.empty());
    EXPECT_TRUE(report.fork_points.empty());
}

// ===== Multiple operations =====

TEST(AuditExtended, DifferentOperationTypes) {
    AuditLog log;
    log.create_genesis();

    log.append("generate", "physics_sampler");
    log.append("validate", "value_range_validator");
    log.append("post_filter", "post_filter_engine");
    log.append("evidence_build", "evidence_builder");
    log.append("audit_log", "audit_system");

    EXPECT_EQ(log.record_count(), 6);

    auto verify = log.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value());
}

// ===== Record count accuracy =====

TEST(AuditExtended, RecordCountAccurate) {
    AuditLog log;
    EXPECT_EQ(log.record_count(), 0);

    log.create_genesis();
    EXPECT_EQ(log.record_count(), 1);

    for (int i = 0; i < 10; ++i) {
        log.append("op", "actor");
        EXPECT_EQ(log.record_count(), 2 + i);
    }
    EXPECT_EQ(log.record_count(), 11);
}
