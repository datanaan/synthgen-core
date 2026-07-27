#pragma once
#include "common/result.h"
#include "common/types.h"
#include "engine/physics/uniform_sampler.h"
#include "parser/ast.h"
#include <vector>

namespace synthgen::schema { struct Schema; }

namespace synthgen::engine::physics {

class RangeExtractor {
public:
    explicit RangeExtractor(const schema::Schema& schema);
    Result<std::vector<ColumnRange>> extract(
        const std::vector<parser::ast::ConstraintItem>& constraints) const;
private:
    const schema::Schema& schema_;
};

}  // namespace synthgen::engine::physics
