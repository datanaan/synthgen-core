#pragma once
#include <cstdint>
#include <functional>

namespace synthgen::engine::physics {

class SeedController {
public:
    explicit SeedController(uint64_t global_seed);
    uint64_t global_seed() const;
    uint64_t request_seed(uint64_t request_id) const;
    uint64_t batch_seed(uint64_t req_seed, int64_t batch_index) const;
    uint64_t row_seed(uint64_t b_seed, int64_t row_index) const;
private:
    uint64_t global_seed_;
    static uint64_t hash_combine(uint64_t seed, uint64_t value);
};

}  // namespace synthgen::engine::physics
