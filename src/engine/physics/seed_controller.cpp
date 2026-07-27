#include "engine/physics/seed_controller.h"

namespace synthgen::engine::physics {

uint64_t SeedController::hash_combine(uint64_t seed, uint64_t value) {
    // FNV-1a inspired combine
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 12) + (seed >> 4);
    return seed;
}

SeedController::SeedController(uint64_t global_seed) : global_seed_(global_seed) {}

uint64_t SeedController::global_seed() const { return global_seed_; }

uint64_t SeedController::request_seed(uint64_t request_id) const {
    return hash_combine(global_seed_, request_id);
}

uint64_t SeedController::batch_seed(uint64_t req_seed, int64_t batch_index) const {
    return hash_combine(req_seed, static_cast<uint64_t>(batch_index));
}

uint64_t SeedController::row_seed(uint64_t b_seed, int64_t row_index) const {
    return hash_combine(b_seed, static_cast<uint64_t>(row_index));
}

}  // namespace synthgen::engine::physics
