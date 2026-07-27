#include "storage/timetravel/time_travel_engine.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <algorithm>

namespace synthgen::storage::timetravel {

using scaffold::SpanGuard;

TimeTravelEngine::TimeTravelEngine(version::ModelVersionChain& chain,
                                   model::ModelStorageLayer& storage)
    : chain_(chain), storage_(storage) {}

Result<TimeTravelResult> TimeTravelEngine::query_as_of(
    const std::string& model_name,
    const std::string& version_id) {
    SpanGuard span("TimeTravelEngine", "query_as_of", "");

    if (model_name.empty()) {
        span.set_status("error");
        return Error(ErrorCode::kInvalidArgument,
                     "model_name must not be empty",
                     "TimeTravelEngine");
    }
    if (version_id.empty()) {
        span.set_status("error");
        return Error(ErrorCode::kInvalidArgument,
                     "version_id must not be empty",
                     "TimeTravelEngine");
    }

    span.set_attribute("model_name", model_name);
    span.set_attribute("requested_version", version_id);

    // Step 1: Try to look up the version in the chain
    auto version_result = chain_.get_version(version_id);
    if (version_result.ok()) {
        // Version exists in the chain; try to load its data from storage
        auto load_result = storage_.load_model(model_name, version_id);
        if (load_result.ok()) {
            // Exact hit: data is available
            TimeTravelResult result;
            result.data = load_result.value();
            result.version = *version_result.value();
            result.was_degraded = false;
            result.bias_report = std::nullopt;

            span.set_attribute("result", "exact_hit");
            scaffold::MetricsRegistry::instance()
                .counter("timetravel.query_as_of")
                .increment();
            scaffold::MetricsRegistry::instance()
                .counter("timetravel.exact_hit")
                .increment();
            return result;
        }

        // Version exists in chain but data file was compacted (deleted)
        // Fall through to degradation path
    }

    // Step 2: Degradation path — find the nearest available version
    gc::CompactionBiasReport report;
    report.requested_version = version_id;

    auto nearest_result = find_nearest_available(
        model_name, version_id, report);
    if (!nearest_result.ok()) {
        span.set_status("error");
        return Error(nearest_result.error().code,
                     nearest_result.error().message,
                     "TimeTravelEngine");
    }

    // Look up the nearest version metadata
    auto nearest_version_result = chain_.get_version(report.returned_version);
    if (!nearest_version_result.ok()) {
        span.set_status("error");
        return Error(ErrorCode::kNoAvailableVersion,
                     "nearest version metadata not found in chain",
                     "TimeTravelEngine");
    }

    TimeTravelResult result;
    result.data = nearest_result.value();
    result.version = *nearest_version_result.value();
    result.was_degraded = true;
    result.bias_report = report;

    span.set_attribute("result", "degraded");
    span.set_attribute("returned_version", report.returned_version);

    scaffold::MetricsRegistry::instance()
        .counter("timetravel.query_as_of")
        .increment();
    scaffold::MetricsRegistry::instance()
        .counter("timetravel.degraded")
        .increment();
    return result;
}

Result<std::string> TimeTravelEngine::find_nearest_available(
    const std::string& model_name,
    const std::string& requested_version,
    gc::CompactionBiasReport& report) {
    SpanGuard span("TimeTravelEngine", "find_nearest_available", "");

    // List all versions for the model from the chain (sorted by created_at desc)
    auto list_result = chain_.list_versions(model_name);
    if (!list_result.ok()) {
        span.set_status("error");
        return Error(list_result.error().code,
                     list_result.error().message,
                     "TimeTravelEngine");
    }

    const auto& all_versions = list_result.value();

    // Find versions that still exist in storage
    // Try loading each version starting from the most recent
    for (const auto& v : all_versions) {
        auto load_result = storage_.load_model(model_name, v.version_id);
        if (load_result.ok()) {
            report.returned_version = v.version_id;
            report.reason = "compacted";
            report.version_mismatch = (v.version_id != requested_version);
            report.merged_from.push_back(v.version_id);
            report.training_data_range = v.training_data_range;
            report.fidelity_score_range_min = v.fidelity_score;
            report.fidelity_score_range_max = v.fidelity_score;

            span.set_attribute("nearest_version", v.version_id);
            return load_result.value();
        }
    }

    // No available version found
    span.set_status("error");
    return Error(ErrorCode::kNoAvailableVersion,
                 "no available version found for model: " + model_name,
                 "TimeTravelEngine");
}

}  // namespace synthgen::storage::timetravel
