#include <gtest/gtest.h>
#include "storage/gc/compaction_bias_report.h"

using namespace synthgen::storage::gc;

// Test 1: DefaultConstruction_Empty — fresh report is empty, version_mismatch is false
TEST(CompactionBiasReportTest, DefaultConstruction_Empty) {
    CompactionBiasReport report;

    EXPECT_TRUE(report.empty());
    EXPECT_FALSE(report.version_mismatch);
    EXPECT_EQ(report.requested_version, "");
    EXPECT_EQ(report.returned_version, "");
    EXPECT_EQ(report.reason, "");
    EXPECT_TRUE(report.merged_from.empty());
    EXPECT_EQ(report.training_data_range, "");
    EXPECT_DOUBLE_EQ(report.fidelity_score_range_min, 0.0);
    EXPECT_DOUBLE_EQ(report.fidelity_score_range_max, 0.0);
}

// Test 2: FilledFields_NotEmpty — set fields, verify not empty
TEST(CompactionBiasReportTest, FilledFields_NotEmpty) {
    CompactionBiasReport report;

    report.requested_version = "v1.0.0";
    report.returned_version = "v1.0.0";
    report.reason = "compacted";
    report.training_data_range = "2024-01-01 to 2024-12-31";

    EXPECT_FALSE(report.empty());
    EXPECT_EQ(report.requested_version, "v1.0.0");
    EXPECT_EQ(report.returned_version, "v1.0.0");
    EXPECT_EQ(report.reason, "compacted");
    EXPECT_EQ(report.training_data_range, "2024-01-01 to 2024-12-31");
}

// Test 3: FidelityRange_Correct — set min/max doubles, verify
TEST(CompactionBiasReportTest, FidelityRange_Correct) {
    CompactionBiasReport report;

    report.fidelity_score_range_min = 0.85;
    report.fidelity_score_range_max = 0.95;

    EXPECT_DOUBLE_EQ(report.fidelity_score_range_min, 0.85);
    EXPECT_DOUBLE_EQ(report.fidelity_score_range_max, 0.95);
}

// Test 4: ProvenanceChain_Reconstructable — merged_from has entries, version_mismatch true when requested != returned
TEST(CompactionBiasReportTest, ProvenanceChain_Reconstructable) {
    CompactionBiasReport report;

    report.requested_version = "v1.5.0";
    report.returned_version = "v1.4.2";
    report.reason = "compacted";
    report.version_mismatch = true;  // explicitly set since versions differ
    report.merged_from = {"v1.0.0", "v1.1.0", "v1.2.0", "v1.3.0"};

    EXPECT_FALSE(report.empty());
    EXPECT_TRUE(report.version_mismatch);
    EXPECT_EQ(report.requested_version, "v1.5.0");
    EXPECT_EQ(report.returned_version, "v1.4.2");
    EXPECT_EQ(report.reason, "compacted");
    EXPECT_EQ(report.merged_from.size(), 4);
    EXPECT_EQ(report.merged_from[0], "v1.0.0");
    EXPECT_EQ(report.merged_from[3], "v1.3.0");
}

// Test 5: NoMismatch_SameVersions — version_mismatch false when same
TEST(CompactionBiasReportTest, NoMismatch_SameVersions) {
    CompactionBiasReport report;

    report.requested_version = "v1.0.0";
    report.returned_version = "v1.0.0";
    report.reason = "anchored";

    EXPECT_FALSE(report.empty());
    EXPECT_FALSE(report.version_mismatch);
    EXPECT_EQ(report.requested_version, "v1.0.0");
    EXPECT_EQ(report.returned_version, "v1.0.0");
    EXPECT_EQ(report.reason, "anchored");
}
