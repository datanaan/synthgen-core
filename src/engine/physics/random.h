#pragma once
#include <cstdint>
#include <random>

namespace synthgen::engine::physics {

class RandomEngine {
public:
    explicit RandomEngine(uint64_t seed);
    double uniform_01();
    double uniform_range(double min, double max);
    double gaussian(double mean, double stddev);
    int64_t uniform_int(int64_t min, int64_t max);
    int64_t uniform_index(size_t size);
private:
    std::mt19937_64 rng_;
};

}  // namespace synthgen::engine::physics
