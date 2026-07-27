#include <gtest/gtest.h>
#include "engine/physics/seed_controller.h"

using namespace synthgen;
using namespace synthgen::engine::physics;

TEST(SeedControllerTest, SameSeedSameOutput) {
    SeedController sc1(42);
    SeedController sc2(42);
    EXPECT_EQ(sc1.request_seed(1), sc2.request_seed(1));
    EXPECT_EQ(sc1.batch_seed(100, 0), sc2.batch_seed(100, 0));
}

TEST(SeedControllerTest, DifferentSeedDifferentOutput) {
    SeedController sc1(42);
    SeedController sc2(43);
    EXPECT_NE(sc1.request_seed(1), sc2.request_seed(1));
}

TEST(SeedControllerTest, DerivationChain) {
    SeedController sc(42);
    auto rs = sc.request_seed(1);
    auto bs = sc.batch_seed(rs, 0);
    auto ws = sc.row_seed(bs, 0);
    EXPECT_NE(rs, sc.global_seed());
    EXPECT_NE(bs, rs);
    EXPECT_NE(ws, bs);
}

TEST(SeedControllerTest, DifferentRequestDifferentSeed) {
    SeedController sc(42);
    EXPECT_NE(sc.request_seed(1), sc.request_seed(2));
}

TEST(SeedControllerTest, DifferentBatchDifferentSeed) {
    SeedController sc(42);
    auto rs = sc.request_seed(1);
    EXPECT_NE(sc.batch_seed(rs, 0), sc.batch_seed(rs, 1));
}

TEST(SeedControllerTest, SeedZero) {
    SeedController sc(0);
    auto rs = sc.request_seed(0);
    EXPECT_NE(rs, 0u);  // should produce non-zero
}

TEST(SeedControllerTest, SeedOne) {
    SeedController sc(1);
    auto rs = sc.request_seed(1);
    SeedController sc2(1);
    EXPECT_EQ(rs, sc2.request_seed(1));
}

TEST(SeedControllerTest, MaxSeed) {
    SeedController sc(UINT64_MAX);
    auto rs = sc.request_seed(UINT64_MAX);
    // Just verify no crash
    EXPECT_NE(rs, 0u);
}

TEST(SeedControllerTest, Determinism100Times) {
    SeedController sc(12345);
    auto expected = sc.request_seed(99);
    for (int i = 0; i < 100; i++) {
        SeedController sc2(12345);
        EXPECT_EQ(sc2.request_seed(99), expected);
    }
}

TEST(SeedControllerTest, BatchIndexMax) {
    SeedController sc(42);
    auto rs = sc.request_seed(1);
    auto bs = sc.batch_seed(rs, INT64_MAX);
    EXPECT_NE(bs, 0u);
}
