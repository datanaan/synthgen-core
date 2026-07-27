#pragma once

#include "common/result.h"
#include <string>
#include <vector>

namespace synthgen::engine::alignment {

struct DriftResult {
    bool drift_detected = false;
    double drift_score = 0.0;  // max KS statistic, normalized 0-1
    double ks_statistic = 0.0;
    double p_value = 0.0;
};

class DriftDetector {
public:
    explicit DriftDetector(const std::string& mode = "ks",
                            double significance_level = 0.05);

    Result<DriftResult> detect(
        const std::vector<double>& current,
        const std::vector<double>& new_data);

private:
    std::string mode_;
    double alpha_;

    double ks_statistic(const std::vector<double>& sample1,
                         const std::vector<double>& sample2) const;
    double ks_critical_value(int n1, int n2) const;
};

}  // namespace synthgen::engine::alignment
