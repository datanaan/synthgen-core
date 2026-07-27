#include "engine/physics/random.h"

namespace synthgen::engine::physics {

RandomEngine::RandomEngine(uint64_t seed) : rng_(seed) {}

double RandomEngine::uniform_01() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng_);
}

double RandomEngine::uniform_range(double min, double max) {
    return min + uniform_01() * (max - min);
}

double RandomEngine::gaussian(double mean, double stddev) {
    std::normal_distribution<double> dist(mean, stddev);
    return dist(rng_);
}

int64_t RandomEngine::uniform_int(int64_t min, int64_t max) {
    std::uniform_int_distribution<int64_t> dist(min, max);
    return dist(rng_);
}

int64_t RandomEngine::uniform_index(size_t size) {
    std::uniform_int_distribution<int64_t> dist(0, static_cast<int64_t>(size - 1));
    return dist(rng_);
}

}  // namespace synthgen::engine::physics
