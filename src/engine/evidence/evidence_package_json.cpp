#include "engine/evidence/evidence_package_json.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>
#include <cmath>

namespace synthgen::engine::evidence {
namespace {

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

void write_constraint_summary(Writer& w, const ConstraintSummary& cs) {
    w.Key("constraint_summary");
    w.StartObject();
    w.Key("type");
    w.String(cs.type.c_str());
    w.Key("details");
    w.StartArray();
    for (const auto& d : cs.details) {
        w.StartObject();
        w.Key("column");
        w.String(d.column.c_str());
        w.Key("min");
        w.Double(d.min);
        w.Key("max");
        w.Double(d.max);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
}

void write_generation_params(Writer& w, const GenerationParams& gp) {
    w.StartObject();
    w.Key("seed");
    w.Uint64(gp.seed);
    w.Key("distribution");
    w.String(gp.distribution.c_str());
    w.Key("limit");
    w.Int64(gp.limit);
    w.Key("batch_size");
    w.Int64(gp.batch_size);
    w.EndObject();
}

void write_trace_span(Writer& w, const TraceSpanEntry& s) {
    w.StartObject();
    w.Key("trace_id");
    w.String(s.trace_id.c_str());
    w.Key("span_id");
    w.String(s.span_id.c_str());
    w.Key("component");
    w.String(s.component.c_str());
    w.Key("operation");
    w.String(s.operation.c_str());
    w.Key("status");
    w.String(s.status.c_str());
    w.EndObject();
}

void write_provenance(Writer& w, const ProvenanceV1& p) {
    w.Key("provenance");
    w.StartObject();
    w.Key("data_source");
    w.String(p.data_source.c_str());
    w.Key("constraints");
    w.StartArray();
    for (const auto& c : p.constraints) {
        w.String(c.c_str());
    }
    w.EndArray();
    w.Key("generation_params");
    write_generation_params(w, p.generation_params);
    w.Key("trace_spans");
    w.StartArray();
    for (const auto& s : p.trace_spans) {
        write_trace_span(w, s);
    }
    w.EndArray();
    w.Key("generator_identity");
    w.String(p.generator_identity.c_str());
    w.EndObject();
}

}  // namespace

Result<std::string> to_json(const EvidencePackageV1& pkg) {
    rapidjson::StringBuffer buf;
    Writer w(buf);

    w.StartObject();

    w.Key("$schema");
    w.String("EvidencePackage/v1");
    w.Key("schema_version");
    w.String(pkg.schema_version.c_str());
    w.Key("schema_hash");
    w.String(pkg.schema_hash.c_str());

    write_constraint_summary(w, pkg.constraint_summary);

    w.Key("exclusion_rate");
    w.Double(pkg.exclusion_rate);
    w.Key("data_grade");
    w.String(pkg.data_grade.c_str());
    w.Key("row_count");
    w.Int64(pkg.row_count);

    write_provenance(w, pkg.provenance);

    // conservative_tail_report
    w.Key("conservative_tail_report");
    w.StartObject();
    w.Key("epistemological_bias");
    w.String(pkg.epistemological_bias.c_str());
    w.Key("tail_exclusion_statement");
    w.String(pkg.tail_exclusion_statement.c_str());
    w.Key("exclusion_rate");
    w.Double(pkg.exclusion_rate_report);
    w.Key("data_grade");
    w.String(pkg.data_grade.c_str());
    w.Key("rows_generated");
    w.Int64(pkg.rows_generated);
    w.Key("rows_validated");
    w.Int64(pkg.rows_validated);
    w.Key("rows_failed_validation");
    w.Int64(pkg.rows_failed_validation);
    w.Key("distribution_used");
    w.String(pkg.distribution_used.c_str());
    w.Key("seed_used");
    w.Uint64(pkg.seed_used);
    w.EndObject();

    // Applicability fields
    w.Key("audit_immutability");
    w.String(pkg.audit_immutability.c_str());
    w.Key("statistical_fidelity");
    w.String(pkg.statistical_fidelity.c_str());
    w.Key("drift_detection");
    w.String(pkg.drift_detection.c_str());
    w.Key("constraint_type_breakdown");
    w.String(pkg.constraint_type_breakdown.c_str());

    w.EndObject();
    return std::string(buf.GetString(), buf.GetSize());
}

namespace {

std::string get_string_or(const rapidjson::Value& v, const char* key, const char* def = "") {
    if (v.HasMember(key) && v[key].IsString()) return v[key].GetString();
    return def;
}

ConstraintDetail parse_constraint_detail(const rapidjson::Value& v) {
    ConstraintDetail d;
    d.column = get_string_or(v, "column");
    if (v.HasMember("min") && v["min"].IsNumber()) d.min = v["min"].GetDouble();
    if (v.HasMember("max") && v["max"].IsNumber()) d.max = v["max"].GetDouble();
    return d;
}

GenerationParams parse_generation_params(const rapidjson::Value& v) {
    GenerationParams gp;
    if (v.HasMember("seed") && v["seed"].IsUint64()) gp.seed = v["seed"].GetUint64();
    gp.distribution = get_string_or(v, "distribution", "uniform");
    if (v.HasMember("limit") && v["limit"].IsInt64()) gp.limit = v["limit"].GetInt64();
    if (v.HasMember("batch_size") && v["batch_size"].IsInt64()) gp.batch_size = v["batch_size"].GetInt64();
    return gp;
}

TraceSpanEntry parse_trace_span(const rapidjson::Value& v) {
    TraceSpanEntry s;
    s.trace_id = get_string_or(v, "trace_id");
    s.span_id = get_string_or(v, "span_id");
    s.component = get_string_or(v, "component");
    s.operation = get_string_or(v, "operation");
    s.status = get_string_or(v, "status", "ok");
    return s;
}

}  // namespace

Result<EvidencePackageV1> from_json(const std::string& json_str) {
    rapidjson::Document doc;
    if (doc.Parse(json_str.c_str()).HasParseError()) {
        return Error(ErrorCode::kDeserializationError,
                     std::string("JSON parse error: ") +
                     rapidjson::GetParseError_En(doc.GetParseError()),
                     "evidence_package_json");
    }
    if (!doc.IsObject()) {
        return Error(ErrorCode::kDeserializationError,
                     "Expected JSON object", "evidence_package_json");
    }

    EvidencePackageV1 pkg;
    pkg.schema_version = get_string_or(doc, "schema_version", "v1");
    pkg.schema_hash = get_string_or(doc, "schema_hash");

    // constraint_summary
    if (doc.HasMember("constraint_summary") && doc["constraint_summary"].IsObject()) {
        const auto& cs = doc["constraint_summary"];
        pkg.constraint_summary.type = get_string_or(cs, "type", "value_range");
        if (cs.HasMember("details") && cs["details"].IsArray()) {
            for (const auto& d : cs["details"].GetArray()) {
                pkg.constraint_summary.details.push_back(parse_constraint_detail(d));
            }
        }
    }

    if (doc.HasMember("exclusion_rate") && doc["exclusion_rate"].IsNumber())
        pkg.exclusion_rate = doc["exclusion_rate"].GetDouble();
    pkg.data_grade = get_string_or(doc, "data_grade", "physics_guaranteed");
    if (doc.HasMember("row_count") && doc["row_count"].IsInt64())
        pkg.row_count = doc["row_count"].GetInt64();

    // provenance
    if (doc.HasMember("provenance") && doc["provenance"].IsObject()) {
        const auto& prov = doc["provenance"];
        pkg.provenance.data_source = get_string_or(prov, "data_source");
        if (prov.HasMember("constraints") && prov["constraints"].IsArray()) {
            for (const auto& c : prov["constraints"].GetArray()) {
                if (c.IsString()) pkg.provenance.constraints.push_back(c.GetString());
            }
        }
        if (prov.HasMember("generation_params") && prov["generation_params"].IsObject()) {
            pkg.provenance.generation_params = parse_generation_params(prov["generation_params"]);
        }
        if (prov.HasMember("trace_spans") && prov["trace_spans"].IsArray()) {
            for (const auto& s : prov["trace_spans"].GetArray()) {
                pkg.provenance.trace_spans.push_back(parse_trace_span(s));
            }
        }
        pkg.provenance.generator_identity = get_string_or(prov, "generator_identity");
    }

    // conservative_tail_report
    if (doc.HasMember("conservative_tail_report") && doc["conservative_tail_report"].IsObject()) {
        const auto& tr = doc["conservative_tail_report"];
        pkg.epistemological_bias = get_string_or(tr, "epistemological_bias", "physical_first");
        pkg.tail_exclusion_statement = get_string_or(tr, "tail_exclusion_statement");
        if (tr.HasMember("exclusion_rate") && tr["exclusion_rate"].IsNumber())
            pkg.exclusion_rate_report = tr["exclusion_rate"].GetDouble();
        if (tr.HasMember("rows_generated") && tr["rows_generated"].IsInt64())
            pkg.rows_generated = tr["rows_generated"].GetInt64();
        if (tr.HasMember("rows_validated") && tr["rows_validated"].IsInt64())
            pkg.rows_validated = tr["rows_validated"].GetInt64();
        if (tr.HasMember("rows_failed_validation") && tr["rows_failed_validation"].IsInt64())
            pkg.rows_failed_validation = tr["rows_failed_validation"].GetInt64();
        pkg.distribution_used = get_string_or(tr, "distribution_used");
        if (tr.HasMember("seed_used") && tr["seed_used"].IsUint64())
            pkg.seed_used = tr["seed_used"].GetUint64();
    }

    // Applicability fields
    pkg.audit_immutability = get_string_or(doc, "audit_immutability", "not_applicable");
    pkg.statistical_fidelity = get_string_or(doc, "statistical_fidelity", "not_applicable");
    pkg.drift_detection = get_string_or(doc, "drift_detection", "not_applicable");
    pkg.constraint_type_breakdown = get_string_or(doc, "constraint_type_breakdown", "not_applicable");

    return pkg;
}

}  // namespace synthgen::engine::evidence
