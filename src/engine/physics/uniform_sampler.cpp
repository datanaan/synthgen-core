#include "engine/physics/uniform_sampler.h"

namespace synthgen::engine::physics {

UniformSampler::UniformSampler(uint64_t seed) : rng_(seed) {}

double UniformSampler::sample_float(double min, double max) {
    return rng_.uniform_range(min, max);
}

int64_t UniformSampler::sample_int(int64_t min, int64_t max) {
    return rng_.uniform_int(min, max);
}

int64_t UniformSampler::sample_datetime() {
    return rng_.uniform_int(0, 31536000000000LL);
}

std::string UniformSampler::sample_string() {
    static const char alphanum[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int len = static_cast<int>(rng_.uniform_int(1, 16));
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) {
        result += alphanum[rng_.uniform_index(sizeof(alphanum) - 1)];
    }
    return result;
}

std::string UniformSampler::sample_enum(const std::vector<std::string>& values) {
    return values[rng_.uniform_index(values.size())];
}

}  // namespace synthgen::engine::physics
