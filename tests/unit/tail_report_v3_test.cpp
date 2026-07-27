#include "gtest/gtest.h"
#include "engine/evidence/tail_report_v3.h"

using namespace synthgen::engine::evidence;

// Test 1: LowBand_0To30 — rate 0.0, 0.15, 0.299 all return "low"
TEST(TailReportV3Test, LowBand_0To30) {
    EXPECT_EQ("low", exclusion_rate_to_band(0.0));
    EXPECT_EQ("low", exclusion_rate_to_band(0.15));
    EXPECT_EQ("low", exclusion_rate_to_band(0.299));
}

// Test 2: MediumBand_30To70 — rate 0.30, 0.50, 0.699 all return "medium"
TEST(TailReportV3Test, MediumBand_30To70) {
    EXPECT_EQ("medium", exclusion_rate_to_band(0.30));
    EXPECT_EQ("medium", exclusion_rate_to_band(0.50));
    EXPECT_EQ("medium", exclusion_rate_to_band(0.699));
}

// Test 3: HighBand_70To90 — rate 0.70, 0.89 return "high"
TEST(TailReportV3Test, HighBand_70To90) {
    EXPECT_EQ("high", exclusion_rate_to_band(0.70));
    EXPECT_EQ("high", exclusion_rate_to_band(0.89));
}

// Test 4: CriticalBand_Above90 — rate 0.90, 1.0 return "critical"
TEST(TailReportV3Test, CriticalBand_Above90) {
    EXPECT_EQ("critical", exclusion_rate_to_band(0.90));
    EXPECT_EQ("critical", exclusion_rate_to_band(1.0));
}

// Test 5: DataGradeMapping — all 4 bands map correctly
TEST(TailReportV3Test, DataGradeMapping) {
    EXPECT_EQ("statistics_guaranteed", rate_band_to_data_grade("low"));
    EXPECT_EQ("limited_fidelity", rate_band_to_data_grade("medium"));
    EXPECT_EQ("limited_fidelity_conservative", rate_band_to_data_grade("high"));
    EXPECT_EQ("rejected", rate_band_to_data_grade("critical"));
}

// Test 6: Build_NoDegradation — was_degraded=false, mismatch fields empty
TEST(TailReportV3Test, Build_NoDegradation) {
    TailReportV3 report = build_tail_report_v3(
        0.25,                          // exclusion_rate
        false,                         // was_degraded
        "converged",                   // compensation_status
        1234567890LL                   // compensation_deadline
    );

    EXPECT_EQ("physical_first", report.epistemological_bias);
    EXPECT_DOUBLE_EQ(0.25, report.exclusion_rate);
    EXPECT_EQ("low", report.rate_band);
    EXPECT_EQ("statistics_guaranteed", report.data_grade);
    EXPECT_FALSE(report.fidelity_mismatch);
    EXPECT_TRUE(report.mismatch_reason.empty());
    EXPECT_EQ("converged", report.compensation_status);
    EXPECT_EQ(1234567890LL, report.compensation_deadline);
    EXPECT_TRUE(report.tail_exclusion_statement.find("25%") != std::string::npos);
}

// Test 7: Build_WithDegradation — was_degraded=true, mismatch_reason="compaction_degraded"
TEST(TailReportV3Test, Build_WithDegradation) {
    TailReportV3 report = build_tail_report_v3(
        0.75,                          // exclusion_rate
        true,                          // was_degraded
        "diverging",                   // compensation_status
        9876543210LL                   // compensation_deadline
    );

    EXPECT_EQ("physical_first", report.epistemological_bias);
    EXPECT_DOUBLE_EQ(0.75, report.exclusion_rate);
    EXPECT_EQ("high", report.rate_band);
    EXPECT_EQ("limited_fidelity_conservative", report.data_grade);
    EXPECT_TRUE(report.fidelity_mismatch);
    EXPECT_EQ("compaction_degraded", report.mismatch_reason);
    EXPECT_EQ("diverging", report.compensation_status);
    EXPECT_EQ(9876543210LL, report.compensation_deadline);
    EXPECT_TRUE(report.tail_exclusion_statement.find("75%") != std::string::npos);
}

// Test 8: CompensationStatuses — all 4 statuses work
TEST(TailReportV3Test, CompensationStatuses) {
    const std::vector<std::string> statuses = {
        "converging",
        "converged",
        "diverging",
        "timeout_degraded"
    };

    for (const auto& status : statuses) {
        TailReportV3 report = build_tail_report_v3(
            0.50,
            false,
            status,
            1111111111LL
        );
        EXPECT_EQ(status, report.compensation_status);
    }
}
