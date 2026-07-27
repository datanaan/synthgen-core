#include "api/service.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <chrono>

using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::evidence;

namespace synthgen::api {

SynthGenService::SynthGenService() = default;

Result<SchemaRef> SynthGenService::define_type(const DefineTypeRequest& req) {
    scaffold::SpanGuard span("service", "define_type", "svc_dt");

    if (req.type_name.empty()) {
        return Error(ErrorCode::kInvalidArgument, "type_name is required", "service");
    }
    if (req.columns.empty()) {
        return Error(ErrorCode::kInvalidArgument, "columns is required", "service");
    }

    schema::Schema schema;
    schema.type_name = req.type_name;
    for (const auto& col : req.columns) {
        ColumnDef cd;
        cd.name = col.name;
        // Map string type to DataType
        if (col.type == "FLOAT") cd.type = DataType::kFloat;
        else if (col.type == "INT") cd.type = DataType::kInt;
        else if (col.type == "DATETIME") cd.type = DataType::kDatetime;
        else if (col.type == "STRING") cd.type = DataType::kString;
        else if (col.type == "ENUM") cd.type = DataType::kEnum;
        else cd.type = DataType::kFloat;

        cd.not_null = col.not_null;
        cd.is_order = col.is_order;
        cd.range_min = col.range_min;
        cd.range_max = col.range_max;
        cd.enum_values = col.enum_values;
        schema.columns.push_back(cd);
    }

    auto vr = schema.validate();
    if (!vr.ok()) return vr.error();

    schemas_[req.type_name] = schema;
    scaffold::MetricsRegistry::instance().counter("define_type_total").increment();

    SchemaRef ref;
    ref.type_name = req.type_name;
    ref.column_count = static_cast<int>(schema.columns.size());
    return ref;
}

Result<ImportResult> SynthGenService::load_data(const LoadDataRequest& req) {
    scaffold::SpanGuard span("service", "load_data", "svc_ld");

    if (req.type_name.empty()) {
        return Error(ErrorCode::kInvalidArgument, "type_name is required", "service");
    }
    auto it = schemas_.find(req.type_name);
    if (it == schemas_.end()) {
        return Error(ErrorCode::kNotFound, "Type not found: " + req.type_name, "service");
    }
    // In v1, load_data is a stub that validates the type exists
    scaffold::MetricsRegistry::instance().counter("load_data_total").increment();

    ImportResult result;
    result.type_name = req.type_name;
    result.rows_imported = 0;  // Stub
    result.status = "success";
    return result;
}

Result<ConstraintRef> SynthGenService::define_constraint(const DefineConstraintRequest& req) {
    scaffold::SpanGuard span("service", "define_constraint", "svc_dc");

    if (req.constraint_name.empty()) {
        return Error(ErrorCode::kInvalidArgument, "constraint_name is required", "service");
    }
    auto it = schemas_.find(req.type_name);
    if (it == schemas_.end()) {
        return Error(ErrorCode::kNotFound, "Type not found: " + req.type_name, "service");
    }
    if (req.checks.empty()) {
        return Error(ErrorCode::kInvalidArgument, "checks is required", "service");
    }

    // Convert RangeChecks to ConstraintItems
    StoredConstraint sc;
    sc.type_name = req.type_name;
    for (const auto& check : req.checks) {
        parser::ast::ConstraintItem item;
        item.column_name = check.column;
        if (check.min_val.has_value()) item.value_min = *check.min_val;
        if (check.max_val.has_value()) item.value_max = *check.max_val;
        sc.items.push_back(item);
    }

    constraints_[req.constraint_name] = sc;
    scaffold::MetricsRegistry::instance().counter("define_constraint_total").increment();

    ConstraintRef ref;
    ref.constraint_name = req.constraint_name;
    ref.type_name = req.type_name;
    ref.check_count = static_cast<int>(req.checks.size());
    return ref;
}

Result<ExplainResult> SynthGenService::explain(const ExplainRequest& req) {
    scaffold::SpanGuard span("service", "explain", "svc_ex");

    if (req.type_name.empty()) {
        return Error(ErrorCode::kInvalidArgument, "type_name is required", "service");
    }
    auto it = schemas_.find(req.type_name);
    if (it == schemas_.end()) {
        return Error(ErrorCode::kNotFound, "Type not found: " + req.type_name, "service");
    }

    ExplainResult result;
    result.execution_mode = "row_by_row";
    result.path = "physics_sampling";
    result.constraint_classification["value_range"] = static_cast<int>(req.constraints.size());
    result.constraint_classification["inter_row"] = 0;
    result.constraint_classification["aggregate"] = 0;
    return result;
}

Result<GenerateResult> SynthGenService::generate(const GenerateRequest& req) {
    scaffold::SpanGuard span("service", "generate", "svc_gen");

    if (req.type_name.empty()) {
        return Error(ErrorCode::kInvalidArgument, "type_name is required", "service");
    }
    if (req.limit <= 0) {
        return Error(ErrorCode::kInvalidArgument, "limit must be > 0", "service");
    }

    auto schema_it = schemas_.find(req.type_name);
    if (schema_it == schemas_.end()) {
        return Error(ErrorCode::kNotFound, "Type not found: " + req.type_name, "service");
    }
    const auto& schema = schema_it->second;

    // Collect constraint items from all named constraints
    std::vector<parser::ast::ConstraintItem> all_constraints;
    for (const auto& cname : req.constraints) {
        auto cit = constraints_.find(cname);
        if (cit == constraints_.end()) {
            return Error(ErrorCode::kNotFound,
                         "Constraint not found: " + cname, "service");
        }
        for (const auto& item : cit->second.items) {
            all_constraints.push_back(item);
        }
    }

    auto start = std::chrono::steady_clock::now();

    // Generate using physics engine
    synthgen::engine::physics::GenerationRequest gen_req{schema, all_constraints, req.limit,
                                       req.seed.value_or(42), req.distribution};
    synthgen::engine::physics::RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    if (!gen_result.ok()) return gen_result.error();

    // Validate
    synthgen::engine::constraint::ValueRangeValidator validator(schema, all_constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    if (!val_result.ok()) return val_result.error();

    // Build tail report
    synthgen::engine::evidence::TailReportBuilder tail_builder;
    auto tail = tail_builder.build(gen_result.value(), val_result.value(),
                                    gen_req, all_constraints);
    if (!tail.ok()) return tail.error();

    // Build evidence package
    synthgen::engine::evidence::ProvenanceV1 prov;
    prov.data_source = "";  // No data source in generate-from-scratch
    for (const auto& cname : req.constraints) prov.constraints.push_back(cname);
    prov.generation_params = {gen_req.seed, gen_req.distribution, gen_req.limit, gen_req.batch_size};
    prov.generator_identity = "physics_sampler";

    synthgen::engine::evidence::EvidencePackageBuilder evp_builder;
    auto evp = evp_builder.build(gen_result.value(),
                                  val_result.value(),
                                  tail.value(),
                                  prov, schema);
    if (!evp.ok()) return evp.error();

    // Serialize evidence to JSON
    auto json_result = evp_builder.to_json(evp.value());
    if (!json_result.ok()) return json_result.error();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    scaffold::MetricsRegistry::instance().counter("generate_total").increment();

    GenerateResult result;
    result.data_format = "parquet";
    result.evidence_json = json_result.value();
    result.stats.rows_generated = gen_result.value().stats.rows_generated;
    result.stats.elapsed_ms = elapsed;
    result.stats.distribution_used = gen_result.value().stats.distribution_used;
    return result;
}

HealthResponse SynthGenService::health() const {
    HealthResponse resp;
    resp.components["parser"] = "ok";
    resp.components["storage"] = "ok";
    resp.components["physics_engine"] = "ok";
    return resp;
}

}  // namespace synthgen::api
