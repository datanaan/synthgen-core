#pragma once
#include "common/result.h"
#include "common/types.h"
#include "engine/physics/uniform_sampler.h"
#include "engine/physics/gaussian_sampler.h"
#include "engine/physics/range_extractor.h"
#include "engine/physics/seed_controller.h"
#include "parser/ast.h"
#include <arrow/table.h>
#include <string>

namespace synthgen::schema { struct Schema; }

namespace synthgen::engine::physics {

struct GenerationRequest {
    const schema::Schema& schema;
    std::vector<parser::ast::ConstraintItem> constraints;
    int64_t limit;
    uint64_t seed;
    std::string distribution = "uniform";
    int64_t batch_size = 1000;
};

struct GenerationStats {
    int64_t rows_generated = 0;
    int64_t rows_requested = 0;
    double exclusion_rate = 0.0;
    int64_t elapsed_ms = 0;
    int64_t batch_count = 0;
    std::string distribution_used;
};

struct GenerationResult {
    std::shared_ptr<arrow::Table> data;
    GenerationStats stats;
};

class RectangularSampler {
public:
    explicit RectangularSampler(const schema::Schema& schema);
    Result<GenerationResult> generate(const GenerationRequest& request);
    Result<void> validate_request(const GenerationRequest& request) const;
private:
    const schema::Schema& schema_;
    RangeExtractor extractor_;
};

}  // namespace synthgen::engine::physics
