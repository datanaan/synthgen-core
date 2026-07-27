#include "engine/alignment/drift_detector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace synthgen::engine::alignment {

DriftDetector::DriftDetector(const std::string& mode, double significance_level)
    : mode_(mode), alpha_(significance_level) {}

Result<DriftResult> DriftDetector::detect(
    const std::vector<double>& current,
    const std::vector<double>& new_data) {
    // Mode "none" skips detection entirely
    if (mode_ == "none") {
        DriftResult result;
        result.drift_detected = false;
        result.drift_score = 0.0;
        result.ks_statistic = 0.0;
        result.p_value = 1.0;
        return result;
    }

    // Empty samples are an error
    if (current.empty()) {
        return Error(ErrorCode::kEmptyTrainingData,
                     "Current (training) sample is empty",
                     "alignment::DriftDetector");
    }
    if (new_data.empty()) {
        return Error(ErrorCode::kEmptyTrainingData,
                     "New data sample is empty",
                     "alignment::DriftDetector");
    }

    double d = ks_statistic(current, new_data);

    int n1 = static_cast<int>(current.size());
    int n2 = static_cast<int>(new_data.size());

    // p-value approximation using Kolmogorov distribution
    // p ~ 2 * exp(-2 * D^2 * n1 * n2 / (n1 + n2))
    double lambda = 2.0 * d * d * static_cast<double>(n1) * static_cast<double>(n2)
                    / static_cast<double>(n1 + n2);
    double p_value = 2.0 * std::exp(-lambda);

    // Clamp p_value to [0, 1]
    if (p_value > 1.0) p_value = 1.0;
    if (p_value < 0.0) p_value = 0.0;

    DriftResult result;
    result.ks_statistic = d;
    result.drift_score = d;  // KS statistic is already in [0, 1]
    result.p_value = p_value;
    result.drift_detected = (p_value < alpha_);

    return result;
}

double DriftDetector::ks_statistic(
    const std::vector<double>& sample1,
    const std::vector<double>& sample2) const {
    // Sort both samples
    std::vector<double> s1 = sample1;
    std::vector<double> s2 = sample2;
    std::sort(s1.begin(), s1.end());
    std::sort(s2.begin(), s2.end());

    int n1 = static_cast<int>(s1.size());
    int n2 = static_cast<int>(s2.size());
    int i = 0;
    int j = 0;
    double max_diff = 0.0;

    // Walk through the merged sorted order, tracking empirical CDFs.
    // When s1[i] == s2[j], advance both — the CDFs step in sync at
    // shared values. This correctly implements the sup-norm distance
    // between the two empirical CDF functions (which are right-continuous).
    while (i < n1 && j < n2) {
        // Check CDF diff BEFORE advancing (at the current positions)
        double cdf1_before = static_cast<double>(i) / static_cast<double>(n1);
        double cdf2_before = static_cast<double>(j) / static_cast<double>(n2);
        double diff_before = std::fabs(cdf1_before - cdf2_before);
        if (diff_before > max_diff) {
            max_diff = diff_before;
        }

        if (s1[i] < s2[j]) {
            ++i;
        } else if (s2[j] < s1[i]) {
            ++j;
        } else {
            // Equal values: advance both so CDFs stay in sync
            ++i;
            ++j;
        }
    }

    // Drain remaining elements from sample1
    while (i < n1) {
        double cdf1 = static_cast<double>(i) / static_cast<double>(n1);
        double cdf2 = 1.0;  // sample2 is exhausted
        double diff = std::fabs(cdf1 - cdf2);
        if (diff > max_diff) {
            max_diff = diff;
        }
        ++i;
    }

    // Drain remaining elements from sample2
    while (j < n2) {
        double cdf1 = 1.0;  // sample1 is exhausted
        double cdf2 = static_cast<double>(j) / static_cast<double>(n2);
        double diff = std::fabs(cdf1 - cdf2);
        if (diff > max_diff) {
            max_diff = diff;
        }
        ++j;
    }

    return max_diff;
}

double DriftDetector::ks_critical_value(int n1, int n2) const {
    // Approximate critical value at alpha_ significance level
    // Using the Kolmogorov-Smirnov formula: c(alpha) * sqrt((n1+n2)/(n1*n2))
    // For alpha=0.05, c=1.36; alpha=0.01, c=1.63; alpha=0.10, c=1.22
    double c = 0.0;
    if (alpha_ <= 0.01) {
        c = 1.63;
    } else if (alpha_ <= 0.05) {
        c = 1.36;
    } else if (alpha_ <= 0.10) {
        c = 1.22;
    } else {
        c = 1.07;  // alpha ~ 0.20
    }
    return c * std::sqrt(static_cast<double>(n1 + n2)
                         / (static_cast<double>(n1) * static_cast<double>(n2)));
}

}  // namespace synthgen::engine::alignment
