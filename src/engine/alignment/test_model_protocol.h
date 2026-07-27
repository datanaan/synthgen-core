#pragma once

#include "common/result.h"
#include <string>
#include <vector>

namespace synthgen::engine::alignment {

struct TestModelProtocol {
    virtual ~TestModelProtocol() = default;
    virtual std::string model_id() const = 0;
    virtual std::string model_type() const = 0;
    virtual Result<double> query_density(const std::vector<double>& point) const = 0;
    virtual Result<std::vector<double>> query_boundary(const std::string& constraint) const = 0;
};

}  // namespace synthgen::engine::alignment
