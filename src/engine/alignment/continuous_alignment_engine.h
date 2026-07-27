#pragma once

#include "engine/alignment/drift_detector.h"
#include "storage/version/model_version_chain.h"
#include "storage/model/model_storage_layer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace synthgen::engine::alignment {

struct AlignmentRequest {
    std::string model_name;
    std::string current_version_id;
    std::vector<double> current_data;   // current distribution sample
    std::vector<double> new_data;       // new distribution sample
    std::string drift_check = "auto";   // "auto"/"ks"/"none"
};

struct AlignmentResult {
    storage::version::ModelVersion new_version;
    bool drift_detected = false;
    double drift_score = 0.0;
    std::string compensation_status;  // converging/converged/diverging/timeout_degraded
    int64_t compensation_deadline = 0;
};

class ContinuousAlignmentEngine {
public:
    ContinuousAlignmentEngine(
        storage::version::ModelVersionChain& chain,
        storage::model::ModelStorageLayer& storage,
        const std::string& drift_mode = "ks");

    Result<AlignmentResult> update_model(const AlignmentRequest& request);

    void set_compensation_deadline(const std::string& model_name, int64_t deadline);

private:
    storage::version::ModelVersionChain& chain_;
    storage::model::ModelStorageLayer& storage_;
    DriftDetector detector_;

    // Convergence tracking per model
    std::unordered_map<std::string, std::vector<double>> recent_drift_scores_;
    int convergence_window_ = 3;
    double convergence_threshold_ = 0.1;
    double divergence_threshold_ = 0.5;
    int divergence_window_ = 5;
    int64_t default_deadline_us_ = 24 * 3600 * 1000000LL;  // 24 hours

    std::unordered_map<std::string, int64_t> deadlines_;

    std::string compute_compensation_status(
        const std::string& model_name, double drift_score);
};

}  // namespace synthgen::engine::alignment
