#pragma once
#include "engine/physics/random.h"
#include "common/types.h"
#include <string>
#include <vector>

namespace synthgen::engine::physics {

struct ColumnRange {
    std::string column_name;
    synthgen::DataType type;
    double min_value = 0;
    double max_value = 0;
    std::vector<std::string> enum_values;
};

class UniformSampler {
public:
    explicit UniformSampler(uint64_t seed);
    double sample_float(double min, double max);
    int64_t sample_int(int64_t min, int64_t max);
    int64_t sample_datetime();
    std::string sample_string();
    std::string sample_enum(const std::vector<std::string>& values);
private:
    RandomEngine rng_;
};

}  // namespace synthgen::engine::physics
