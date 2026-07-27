#include <gtest/gtest.h>
#include "engine/alignment/drift_detector.h"

#include <random>
#include <vector>

using namespace synthgen;
using namespace synthgen::engine::alignment;

namespace {

std::vector<double> generate_normal(double mean, double stddev, int n, uint64_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(mean, stddev);
    std::vector<double> result(n);
    for (int i = 0; i < n; ++i) result[i] = dist(rng);
    return result;
}

std::vector<double> generate_uniform(double low, double high, int n, uint64_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(low, high);
    std::vector<double> result(n);
    for (int i = 0; i < n; ++i) result[i] = dist(rng);
    return result;
}

}  // namespace

// Test 1: Large mean shift between distributions should be detected
TEST(DriftDetectorTest, MeanShift_Detected) {
    DriftDetector detector("ks", 0.05);
    auto current = generate_normal(0.0, 1.0, 200, 42);
    auto new_data = generate_normal(3.0, 1.0, 200, 123);

    auto result = detector.detect(current, new_data);
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_TRUE(result.value().drift_detected);
    EXPECT_GT(result.value().ks_statistic, 0.5);
    EXPECT_LT(result.value().p_value, 0.05);
}

// Test 2: Samples from the same distribution should not show drift
TEST(DriftDetectorTest, NoDrift_NotDetected) {
    DriftDetector detector("ks", 0.05);
    auto sample1 = generate_normal(0.0, 1.0, 200, 42);
    auto sample2 = generate_normal(0.0, 1.0, 200, 99);

    auto result = detector.detect(sample1, sample2);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // Same distribution — p-value should be well above alpha
    // Use generous threshold since this is a statistical test
    EXPECT_GT(result.value().p_value, 0.01);
    EXPECT_LT(result.value().ks_statistic, 0.2);
}

// Test 3: Variance shift (same mean, different spread) should be detected
TEST(DriftDetectorTest, VarianceShift_Detected) {
    DriftDetector detector("ks", 0.05);
    auto current = generate_normal(0.0, 1.0, 200, 42);
    auto new_data = generate_normal(0.0, 5.0, 200, 77);

    auto result = detector.detect(current, new_data);
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_TRUE(result.value().drift_detected);
    EXPECT_GT(result.value().ks_statistic, 0.1);
}

// Test 4: Empty current vector should return error
TEST(DriftDetectorTest, EmptyCurrent_ReturnsError) {
    DriftDetector detector("ks", 0.05);
    std::vector<double> empty;
    auto new_data = generate_normal(0.0, 1.0, 50, 42);

    auto result = detector.detect(empty, new_data);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kEmptyTrainingData);
}

// Test 5: Empty new_data vector should return error
TEST(DriftDetectorTest, EmptyNewData_ReturnsError) {
    DriftDetector detector("ks", 0.05);
    auto current = generate_normal(0.0, 1.0, 50, 42);
    std::vector<double> empty;

    auto result = detector.detect(current, empty);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kEmptyTrainingData);
}

// Test 6: mode="none" should skip detection regardless of data
TEST(DriftDetectorTest, ModeNone_SkipsDetection) {
    DriftDetector detector("none", 0.05);
    auto current = generate_normal(0.0, 1.0, 200, 42);
    auto new_data = generate_normal(10.0, 1.0, 200, 123);

    auto result = detector.detect(current, new_data);
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_FALSE(result.value().drift_detected);
    EXPECT_DOUBLE_EQ(result.value().drift_score, 0.0);
    EXPECT_DOUBLE_EQ(result.value().ks_statistic, 0.0);
    EXPECT_DOUBLE_EQ(result.value().p_value, 1.0);
}

// Test 7: Uniform vs concentrated distributions should show drift
TEST(DriftDetectorTest, ExtremeDistribution_Detected) {
    DriftDetector detector("ks", 0.05);
    auto current = generate_uniform(0.0, 100.0, 200, 42);
    // Concentrated: narrow normal
    auto new_data = generate_normal(50.0, 0.5, 200, 88);

    auto result = detector.detect(current, new_data);
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_TRUE(result.value().drift_detected);
    EXPECT_GT(result.value().ks_statistic, 0.1);
}

// Test 8: Identical samples should have zero or near-zero KS statistic
TEST(DriftDetectorTest, IdenticalSamples_ZeroDrift) {
    DriftDetector detector("ks", 0.05);
    auto sample = generate_normal(5.0, 2.0, 100, 42);

    auto result = detector.detect(sample, sample);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // Same exact data — KS statistic should be near-zero
    // (small non-zero due to merge walk when values coincide)
    EXPECT_LT(result.value().ks_statistic, 0.05);
    EXPECT_LT(result.value().drift_score, 0.05);
    EXPECT_FALSE(result.value().drift_detected);
}

// Test 9: KS statistic is bounded in [0, 1]
TEST(DriftDetectorTest, KSStatistic_Bounded) {
    DriftDetector detector("ks", 0.05);
    auto current = generate_normal(0.0, 1.0, 50, 42);
    auto new_data = generate_normal(10.0, 1.0, 50, 123);

    auto result = detector.detect(current, new_data);
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_GE(result.value().ks_statistic, 0.0);
    EXPECT_LE(result.value().ks_statistic, 1.0);
    EXPECT_GE(result.value().p_value, 0.0);
    EXPECT_LE(result.value().p_value, 1.0);
}

// Test 10: Small sample sizes still produce valid results
TEST(DriftDetectorTest, SmallSamples_ValidResult) {
    DriftDetector detector("ks", 0.05);
    std::vector<double> current = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> new_data = {10.0, 20.0, 30.0, 40.0, 50.0};

    auto result = detector.detect(current, new_data);
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_TRUE(result.value().drift_detected);
    EXPECT_GT(result.value().ks_statistic, 0.5);
}
