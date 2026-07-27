#include "engine/alignment/continuous_alignment_engine.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <chrono>
#include <sstream>

namespace synthgen::engine::alignment {

namespace {

int64_t now_us() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch())
        .count();
}

}  // namespace

ContinuousAlignmentEngine::ContinuousAlignmentEngine(
    storage::version::ModelVersionChain& chain,
    storage::model::ModelStorageLayer& storage,
    const std::string& drift_mode)
    : chain_(chain), storage_(storage), detector_(drift_mode) {}

Result<AlignmentResult> ContinuousAlignmentEngine::update_model(
    const AlignmentRequest& request) {
    scaffold::SpanGuard span("alignment", "update_model", "alignment_update");

    // 1. Validate inputs
    if (request.model_name.empty()) {
        span.set_status("error");
        scaffold::MetricsRegistry::instance()
            .counter("alignment.update.error")
            .increment();
        return Error(ErrorCode::kInvalidArgument,
                     "model_name must not be empty", "alignment");
    }

    if (request.new_data.empty()) {
        span.set_status("error");
        scaffold::MetricsRegistry::instance()
            .counter("alignment.update.error")
            .increment();
        return Error(ErrorCode::kEmptyTrainingData,
                     "new_data must not be empty", "alignment");
    }

    // 2. Resolve drift_check mode
    std::string effective_mode = request.drift_check;
    if (effective_mode == "auto") {
        effective_mode = "ks";
    }

    // 3. Run drift detection
    bool drift_detected = false;
    double drift_score = 0.0;

    if (effective_mode != "none" && !request.current_data.empty()) {
        // Reconfigure detector if needed
        DriftDetector local_detector(effective_mode);
        auto drift_result = local_detector.detect(
            request.current_data, request.new_data);

        if (drift_result.ok()) {
            drift_detected = drift_result.value().drift_detected;
            drift_score = drift_result.value().drift_score;
        }
        // If drift detection fails (shouldn't with non-empty data),
        // leave drift_detected=false, drift_score=0.0
    }
    // If current_data empty (first alignment) or mode is "none":
    // drift_detected stays false, drift_score stays 0.0

    // 4. Create new version via chain
    storage::version::ModelVersion version_meta;
    version_meta.created_by = "alignment_engine";
    version_meta.custom_metadata["drift_score"] = std::to_string(drift_score);
    version_meta.custom_metadata["drift_detected"] = drift_detected ? "true" : "false";
    version_meta.custom_metadata["data_size"] = std::to_string(request.new_data.size());

    // Compute simple data stats for metadata
    double data_min = request.new_data[0];
    double data_max = request.new_data[0];
    double data_sum = 0.0;
    for (const auto& v : request.new_data) {
        data_min = std::min(data_min, v);
        data_max = std::max(data_max, v);
        data_sum += v;
    }
    double data_mean = data_sum / static_cast<double>(request.new_data.size());
    version_meta.custom_metadata["data_min"] = std::to_string(data_min);
    version_meta.custom_metadata["data_max"] = std::to_string(data_max);
    version_meta.custom_metadata["data_mean"] = std::to_string(data_mean);

    auto version_result = chain_.create_version(
        request.model_name, request.current_version_id, version_meta);

    if (!version_result.ok()) {
        span.set_status("error");
        scaffold::MetricsRegistry::instance()
            .counter("alignment.update.version_error")
            .increment();
        return Error(version_result.error().code,
                     version_result.error().message,
                     "alignment");
    }

    auto new_version = version_result.value();

    // 5. Save checkpoint
    {
        std::ostringstream oss;
        oss << "drift_score=" << drift_score
            << ";drift_detected=" << (drift_detected ? 1 : 0)
            << ";data_size=" << request.new_data.size()
            << ";data_mean=" << data_mean
            << ";data_min=" << data_min
            << ";data_max=" << data_max;
        auto save_result = storage_.save_checkpoint(
            request.model_name, new_version.version_id, oss.str());
        if (!save_result.ok()) {
            span.set_status("error");
            scaffold::MetricsRegistry::instance()
                .counter("alignment.update.checkpoint_error")
                .increment();
            // Continue — checkpoint failure is not fatal for alignment
        }
    }

    // 6. Compute compensation status
    std::string compensation_status = compute_compensation_status(
        request.model_name, drift_score);

    // 7. Build result
    AlignmentResult result;
    result.new_version = new_version;
    result.drift_detected = drift_detected;
    result.drift_score = drift_score;
    result.compensation_status = compensation_status;

    // Set deadline in result
    auto deadline_it = deadlines_.find(request.model_name);
    if (deadline_it != deadlines_.end()) {
        result.compensation_deadline = deadline_it->second;
    } else {
        result.compensation_deadline = now_us() + default_deadline_us_;
    }

    span.set_attribute("model_name", request.model_name);
    span.set_attribute("version_id", new_version.version_id);
    span.set_attribute("drift_score", std::to_string(drift_score));
    span.set_attribute("compensation_status", compensation_status);

    scaffold::MetricsRegistry::instance()
        .counter("alignment.update.success")
        .increment();
    scaffold::MetricsRegistry::instance()
        .histogram("alignment.drift_score")
        .observe(drift_score);

    return result;
}

std::string ContinuousAlignmentEngine::compute_compensation_status(
    const std::string& model_name, double drift_score) {
    // Track recent drift scores per model
    auto& scores = recent_drift_scores_[model_name];
    scores.push_back(drift_score);

    // Keep only the last 10 scores
    if (scores.size() > 10) {
        scores.erase(scores.begin(), scores.begin() + (scores.size() - 10));
    }

    // Check convergence: last convergence_window_ scores all below threshold
    bool converged = false;
    if (static_cast<int>(scores.size()) >= convergence_window_) {
        converged = true;
        int start = static_cast<int>(scores.size()) - convergence_window_;
        for (int i = start; i < static_cast<int>(scores.size()); ++i) {
            if (scores[i] >= convergence_threshold_) {
                converged = false;
                break;
            }
        }
    }

    // Check divergence: last divergence_window_ scores all above threshold
    bool diverging = false;
    if (static_cast<int>(scores.size()) >= divergence_window_) {
        diverging = true;
        int start = static_cast<int>(scores.size()) - divergence_window_;
        for (int i = start; i < static_cast<int>(scores.size()); ++i) {
            if (scores[i] <= divergence_threshold_) {
                diverging = false;
                break;
            }
        }
    }

    // Check deadline
    auto deadline_it = deadlines_.find(model_name);
    if (deadline_it != deadlines_.end()) {
        int64_t now = now_us();
        if (now > deadline_it->second && !converged) {
            return "timeout_degraded";
        }
    }

    if (converged) {
        return "converged";
    }
    if (diverging) {
        return "diverging";
    }
    return "converging";
}

void ContinuousAlignmentEngine::set_compensation_deadline(
    const std::string& model_name, int64_t deadline) {
    deadlines_[model_name] = deadline;
}

}  // namespace synthgen::engine::alignment
