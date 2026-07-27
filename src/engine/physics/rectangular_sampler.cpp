#include "engine/physics/rectangular_sampler.h"
#include "schema/schema.h"
#include "engine/physics/seed_controller.h"

#include <arrow/api.h>
#include <chrono>

namespace synthgen::engine::physics {

RectangularSampler::RectangularSampler(const schema::Schema& schema)
    : schema_(schema), extractor_(schema) {}

Result<void> RectangularSampler::validate_request(const GenerationRequest& request) const {
    if (schema_.columns.empty())
        return Error(ErrorCode::kInvalidArgument, "Schema has no columns");
    if (request.limit < 0)
        return Error(ErrorCode::kInvalidArgument, "LIMIT must be non-negative");
    if (request.distribution != "uniform" && request.distribution != "gaussian")
        return Error(ErrorCode::kInvalidArgument,
                     "Unsupported distribution: " + request.distribution);
    return {};
}

Result<GenerationResult> RectangularSampler::generate(const GenerationRequest& request) {
    auto validation = validate_request(request);
    if (!validation.ok()) return validation.error();

    if (request.limit == 0) {
        GenerationResult result;
        result.stats.rows_generated = 0;
        result.stats.rows_requested = 0;
        result.stats.distribution_used = request.distribution;
        return result;
    }

    auto start = std::chrono::steady_clock::now();

    auto ranges_result = extractor_.extract(request.constraints);
    if (!ranges_result.ok()) return ranges_result.error();
    auto& ranges = ranges_result.value();

    SeedController seed_ctrl(request.seed);
    uint64_t req_seed = seed_ctrl.request_seed(1);

    int64_t batch_size = request.batch_size;
    int64_t remaining = request.limit;
    int64_t batch_count = 0;
    int64_t total_generated = 0;

    // Pre-allocate column builders
    std::vector<std::unique_ptr<arrow::ArrayBuilder>> builders;
    for (const auto& range : ranges) {
        switch (range.type) {
            case DataType::kFloat:
                builders.push_back(std::make_unique<arrow::DoubleBuilder>());
                break;
            case DataType::kInt:
                builders.push_back(std::make_unique<arrow::Int64Builder>());
                break;
            case DataType::kDatetime:
                builders.push_back(std::make_unique<arrow::TimestampBuilder>(
                    arrow::timestamp(arrow::TimeUnit::MICRO), arrow::default_memory_pool()));
                break;
            case DataType::kString:
            case DataType::kEnum:
                builders.push_back(std::make_unique<arrow::StringBuilder>());
                break;
        }
    }

    while (remaining > 0) {
        int64_t this_batch = std::min(remaining, batch_size);
        uint64_t b_seed = seed_ctrl.batch_seed(req_seed, batch_count);

        for (int64_t row = 0; row < this_batch; row++) {
            uint64_t r_seed = seed_ctrl.row_seed(b_seed, row);

            for (size_t col = 0; col < ranges.size(); col++) {
                const auto& range = ranges[col];
                auto& builder = builders[col];

                if (request.distribution == "uniform") {
                    UniformSampler sampler(r_seed);
                    switch (range.type) {
                        case DataType::kFloat: {
                            auto val = sampler.sample_float(range.min_value, range.max_value);
                            static_cast<arrow::DoubleBuilder*>(builder.get())->Append(val);
                            break;
                        }
                        case DataType::kInt: {
                            auto val = sampler.sample_int(
                                static_cast<int64_t>(range.min_value),
                                static_cast<int64_t>(range.max_value));
                            static_cast<arrow::Int64Builder*>(builder.get())->Append(val);
                            break;
                        }
                        case DataType::kDatetime: {
                            auto val = sampler.sample_datetime();
                            static_cast<arrow::TimestampBuilder*>(builder.get())->Append(val);
                            break;
                        }
                        case DataType::kString: {
                            auto val = sampler.sample_string();
                            static_cast<arrow::StringBuilder*>(builder.get())->Append(val);
                            break;
                        }
                        case DataType::kEnum: {
                            auto val = sampler.sample_enum(range.enum_values);
                            static_cast<arrow::StringBuilder*>(builder.get())->Append(val);
                            break;
                        }
                    }
                } else {  // gaussian
                    GaussianSampler gsampler(r_seed);
                    UniformSampler usampler(r_seed);
                    switch (range.type) {
                        case DataType::kFloat: {
                            TruncationStats stats;
                            auto val = gsampler.sample_float(range.min_value, range.max_value, stats);
                            static_cast<arrow::DoubleBuilder*>(builder.get())->Append(val);
                            break;
                        }
                        case DataType::kInt: {
                            TruncationStats stats;
                            auto fval = gsampler.sample_float(range.min_value, range.max_value, stats);
                            static_cast<arrow::Int64Builder*>(builder.get())->Append(
                                static_cast<int64_t>(std::round(fval)));
                            break;
                        }
                        case DataType::kDatetime: {
                            auto val = usampler.sample_datetime();
                            static_cast<arrow::TimestampBuilder*>(builder.get())->Append(val);
                            break;
                        }
                        case DataType::kString: {
                            auto val = usampler.sample_string();
                            static_cast<arrow::StringBuilder*>(builder.get())->Append(val);
                            break;
                        }
                        case DataType::kEnum: {
                            auto val = usampler.sample_enum(range.enum_values);
                            static_cast<arrow::StringBuilder*>(builder.get())->Append(val);
                            break;
                        }
                    }
                }
            }
        }

        total_generated += this_batch;
        remaining -= this_batch;
        batch_count++;
    }

    // Build Arrow schema and table
    std::vector<std::shared_ptr<arrow::Field>> fields;
    for (const auto& range : ranges) {
        switch (range.type) {
            case DataType::kFloat:
                fields.push_back(arrow::field(range.column_name, arrow::float64()));
                break;
            case DataType::kInt:
                fields.push_back(arrow::field(range.column_name, arrow::int64()));
                break;
            case DataType::kDatetime:
                fields.push_back(arrow::field(range.column_name,
                    arrow::timestamp(arrow::TimeUnit::MICRO)));
                break;
            case DataType::kString:
            case DataType::kEnum:
                fields.push_back(arrow::field(range.column_name, arrow::utf8()));
                break;
        }
    }
    auto arrow_schema = std::make_shared<arrow::Schema>(fields);

    std::vector<std::shared_ptr<arrow::Array>> arrays;
    for (auto& builder : builders) {
        std::shared_ptr<arrow::Array> arr;
        auto status = builder->Finish(&arr);
        if (!status.ok())
            return Error(ErrorCode::kInternalError, "Arrow array build failed: " + status.ToString());
        arrays.push_back(arr);
    }

    auto table = arrow::Table::Make(arrow_schema, arrays);

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    GenerationResult result;
    result.data = table;
    result.stats.rows_generated = total_generated;
    result.stats.rows_requested = request.limit;
    result.stats.exclusion_rate = 0.0;  // Pure physics path
    result.stats.elapsed_ms = elapsed_ms;
    result.stats.batch_count = batch_count;
    result.stats.distribution_used = request.distribution;
    return result;
}

}  // namespace synthgen::engine::physics
