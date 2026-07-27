#pragma once
#include "engine/physics/random.h"
#include <cstdint>

namespace synthgen::engine::physics {

struct TruncationStats {
    int64_t truncated_low = 0;
    int64_t truncated_high = 0;
};

class GaussianSampler {
public:
    explicit GaussianSampler(uint64_t seed);
    double sample_float(double min, double max, TruncationStats& stats);
private:
    RandomEngine rng_;
};

}  // namespace synthgen::engine::physics
