#include "engine/physics/gaussian_sampler.h"

namespace synthgen::engine::physics {

GaussianSampler::GaussianSampler(uint64_t seed) : rng_(seed) {}

double GaussianSampler::sample_float(double min, double max, TruncationStats& stats) {
    double mean = (min + max) / 2.0;
    double stddev = (max - min) / 6.0;
    double value = rng_.gaussian(mean, stddev);
    if (value < min) { value = min; stats.truncated_low++; }
    if (value > max) { value = max; stats.truncated_high++; }
    return value;
}

}  // namespace synthgen::engine::physics
