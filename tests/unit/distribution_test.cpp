#include <gtest/gtest.h>
#include "engine/physics/uniform_sampler.h"
#include "engine/physics/gaussian_sampler.h"
#include "engine/physics/random.h"
#include <cmath>
#include <set>

using namespace synthgen;
using namespace synthgen::engine::physics;

TEST(DistributionTest, UniformFloatRange) {
    UniformSampler s(42);
    for (int i = 0; i < 1000; i++) {
        auto val = s.sample_float(-10.0, 10.0);
        EXPECT_GE(val, -10.0);
        EXPECT_LE(val, 10.0);
    }
}

TEST(DistributionTest, UniformIntRange) {
    UniformSampler s(42);
    for (int i = 0; i < 1000; i++) {
        auto val = s.sample_int(-100, 100);
        EXPECT_GE(val, -100);
        EXPECT_LE(val, 100);
    }
}

TEST(DistributionTest, UniformDatetime) {
    UniformSampler s(42);
    for (int i = 0; i < 100; i++) {
        auto val = s.sample_datetime();
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 31536000000000LL);
    }
}

TEST(DistributionTest, UniformStringLength) {
    UniformSampler s(42);
    for (int i = 0; i < 100; i++) {
        auto val = s.sample_string();
        EXPECT_GE(val.size(), 1u);
        EXPECT_LE(val.size(), 16u);
    }
}

TEST(DistributionTest, UniformEnumValues) {
    std::vector<std::string> values = {"a", "b", "c"};
    UniformSampler s(42);
    std::set<std::string> seen;
    for (int i = 0; i < 100; i++) {
        auto val = s.sample_enum(values);
        EXPECT_NE(std::find(values.begin(), values.end(), val), values.end());
        seen.insert(val);
    }
    EXPECT_EQ(seen.size(), 3u);
}

TEST(DistributionTest, GaussianFloatRange) {
    GaussianSampler s(42);
    TruncationStats stats;
    for (int i = 0; i < 1000; i++) {
        auto val = s.sample_float(-10.0, 10.0, stats);
        EXPECT_GE(val, -10.0);
        EXPECT_LE(val, 10.0);
    }
}

TEST(DistributionTest, GaussianTruncation) {
    GaussianSampler s(42);
    TruncationStats stats;
    for (int i = 0; i < 10000; i++) {
        s.sample_float(0.0, 1.0, stats);
    }
    // With stddev=1/6, ~0.3% should truncate
    EXPECT_GT(stats.truncated_low + stats.truncated_high, 0);
}

TEST(DistributionTest, RandomEngineDeterministic) {
    RandomEngine r1(42);
    RandomEngine r2(42);
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(r1.uniform_int(0, 1000000), r2.uniform_int(0, 1000000));
    }
}

TEST(DistributionTest, RandomEngineDifferentSeed) {
    RandomEngine r1(42);
    RandomEngine r2(43);
    bool different = false;
    for (int i = 0; i < 10; i++) {
        if (r1.uniform_int(0, 1000000) != r2.uniform_int(0, 1000000)) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different);
}

TEST(DistributionTest, BoundaryZeroWidthRange) {
    UniformSampler s(42);
    auto val = s.sample_float(5.0, 5.0);
    EXPECT_DOUBLE_EQ(val, 5.0);
}

TEST(DistributionTest, BoundaryNegativeRange) {
    UniformSampler s(42);
    for (int i = 0; i < 100; i++) {
        auto val = s.sample_float(-100.0, -1.0);
        EXPECT_GE(val, -100.0);
        EXPECT_LE(val, -1.0);
    }
}

TEST(DistributionTest, SingleEnumValue) {
    std::vector<std::string> values = {"only"};
    UniformSampler s(42);
    for (int i = 0; i < 50; i++) {
        EXPECT_EQ(s.sample_enum(values), "only");
    }
}

TEST(DistributionTest, UniformFloatLargeRange) {
    UniformSampler s(42);
    auto val = s.sample_float(-1e18, 1e18);
    EXPECT_GE(val, -1e18);
    EXPECT_LE(val, 1e18);
}

TEST(DistributionTest, GaussianWideRangeNoTruncation) {
    GaussianSampler s(42);
    TruncationStats stats;
    for (int i = 0; i < 1000; i++) {
        s.sample_float(-1000.0, 1000.0, stats);
    }
    // With range 2000, stddev=333, truncation should be rare
    EXPECT_LT(stats.truncated_low + stats.truncated_high, 50);
}

TEST(DistributionTest, UniformIntMinValue) {
    UniformSampler s(42);
    auto val = s.sample_int(0, 0);
    EXPECT_EQ(val, 0);
}
