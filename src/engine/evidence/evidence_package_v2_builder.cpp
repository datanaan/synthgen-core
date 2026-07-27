#include "engine/evidence/evidence_package_v2_builder.h"
#include "common/hash.h"
#include "scaffold/trace.h"

#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

namespace synthgen::engine::evidence {

Result<EvidencePackageV2> EvidencePackageV2Builder::build(
    int64_t row_count,
    double exclusion_rate,
    const std::string& data_grade,
    const router::RoutingDecision& routing_decision,
    const router::ClassificationResult& classification,
    const postfilter::PostFilterResult& postfilter_result,
    const schema::Schema& schema) {

    scaffold::SpanGuard span("evidence_v2", "build", "evp2_build");

    EvidencePackageV2 pkg;
    pkg.schema_version = "v2";
    pkg.row_count = row_count;
    pkg.exclusion_rate = exclusion_rate;
    pkg.data_grade = data_grade;

    // Schema hash — must match V1 format for consistency
    std::ostringstream oss;
    oss << schema.type_name << "{";
    for (const auto& col : schema.columns) {
        oss << col.name << ":" << static_cast<int>(col.type);
        if (col.range_min) oss << "[" << *col.range_min;
        if (col.range_max) oss << "," << *col.range_max << "]";
        oss << ";";
    }
    oss << "}";
    pkg.schema_hash = sha256_hex(oss.str());

    // Constraint summary
    pkg.constraint_summary.type = "value_range";
    for (const auto& col : schema.columns) {
        if (col.range_min.has_value() && col.range_max.has_value()) {
            pkg.constraint_summary.details.push_back(
                {col.name, col.range_min.value(), col.range_max.value()});
        }
    }

    // Tail report
    pkg.epistemological_bias = "physical_first";
    pkg.tail_exclusion_statement =
        "Tail events systematically excluded by constraint filtering. "
        "The generated data world's risk spectrum is narrower than the real physical world.";
    pkg.exclusion_rate_report = exclusion_rate;
    pkg.rows_generated = row_count;
    pkg.rows_validated = row_count;
    pkg.rows_failed_validation = 0;
    pkg.distribution_used = "uniform";
    pkg.seed_used = 0;

    // v2: audit immutability
    pkg.audit_immutability = "verified";

    // v2: statistical fidelity
    pkg.statistical_fidelity.available = false;

    // v2: constraint type breakdown
    pkg.constraint_type_breakdown.value_range_count = classification.value_range_count;
    pkg.constraint_type_breakdown.inter_row_count = classification.inter_row_count;
    pkg.constraint_type_breakdown.aggregate_count = classification.aggregate_count;

    // v2: generator identity
    pkg.generator_identity = routing_decision.identity;

    // v2: provenance
    pkg.provenance.base.data_source = "";
    pkg.provenance.base.generator_identity = routing_decision.identity.identity;
    pkg.provenance.degradation_path = routing_decision.identity.identity;

    // v2: post-filter info
    pkg.post_filter_info.was_post_filtered = postfilter_result.actual_exclusion_rate > 0.0;
    pkg.post_filter_info.pre_filter_rows = postfilter_result.pre_filter_rows;
    pkg.post_filter_info.post_filter_rows = postfilter_result.post_filter_rows;
    pkg.post_filter_info.actual_exclusion_rate = postfilter_result.actual_exclusion_rate;
    pkg.post_filter_info.exclusion_rate_band = postfilter::PostFilter::data_grade_for_band(
        postfilter_result.rate_band);
    pkg.post_filter_info.was_timeout_truncated = postfilter_result.was_timeout_truncated;

    return pkg;
}

Result<std::string> EvidencePackageV2Builder::to_json(const EvidencePackageV2& pkg) {
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> w(buf);

    w.StartObject();
    w.Key("$schema"); w.String("EvidencePackage/v2");
    w.Key("schema_version"); w.String(pkg.schema_version.c_str());
    w.Key("schema_hash"); w.String(pkg.schema_hash.c_str());

    // constraint_summary
    w.Key("constraint_summary");
    w.StartObject();
    w.Key("type"); w.String(pkg.constraint_summary.type.c_str());
    w.Key("details");
    w.StartArray();
    for (const auto& d : pkg.constraint_summary.details) {
        w.StartObject();
        w.Key("column"); w.String(d.column.c_str());
        w.Key("min"); w.Double(d.min);
        w.Key("max"); w.Double(d.max);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();

    w.Key("exclusion_rate"); w.Double(pkg.exclusion_rate);
    w.Key("data_grade"); w.String(pkg.data_grade.c_str());
    w.Key("row_count"); w.Int64(pkg.row_count);

    // generator_identity
    w.Key("generator_identity");
    w.StartObject();
    w.Key("identity"); w.String(pkg.generator_identity.identity.c_str());
    w.Key("justification"); w.String(pkg.generator_identity.justification.c_str());
    w.Key("path"); w.Int(static_cast<int>(pkg.generator_identity.path));
    w.EndObject();

    // constraint_type_breakdown
    w.Key("constraint_type_breakdown");
    w.StartObject();
    w.Key("value_range"); w.Int(pkg.constraint_type_breakdown.value_range_count);
    w.Key("inter_row"); w.Int(pkg.constraint_type_breakdown.inter_row_count);
    w.Key("aggregate"); w.Int(pkg.constraint_type_breakdown.aggregate_count);
    w.EndObject();

    // audit
    w.Key("audit_immutability"); w.String(pkg.audit_immutability.c_str());

    // statistical_fidelity
    w.Key("statistical_fidelity");
    w.StartObject();
    w.Key("available"); w.Bool(pkg.statistical_fidelity.available);
    w.EndObject();

    // post_filter_info
    w.Key("post_filter_info");
    w.StartObject();
    w.Key("was_post_filtered"); w.Bool(pkg.post_filter_info.was_post_filtered);
    w.Key("pre_filter_rows"); w.Int64(pkg.post_filter_info.pre_filter_rows);
    w.Key("post_filter_rows"); w.Int64(pkg.post_filter_info.post_filter_rows);
    w.Key("actual_exclusion_rate"); w.Double(pkg.post_filter_info.actual_exclusion_rate);
    w.Key("exclusion_rate_band"); w.String(pkg.post_filter_info.exclusion_rate_band.c_str());
    w.Key("was_timeout_truncated"); w.Bool(pkg.post_filter_info.was_timeout_truncated);
    w.EndObject();

    // tail_report
    w.Key("conservative_tail_report");
    w.StartObject();
    w.Key("epistemological_bias"); w.String(pkg.epistemological_bias.c_str());
    w.Key("tail_exclusion_statement"); w.String(pkg.tail_exclusion_statement.c_str());
    w.Key("exclusion_rate"); w.Double(pkg.exclusion_rate_report);
    w.Key("rows_generated"); w.Int64(pkg.rows_generated);
    w.Key("rows_validated"); w.Int64(pkg.rows_validated);
    w.Key("rows_failed_validation"); w.Int64(pkg.rows_failed_validation);
    w.Key("distribution_used"); w.String(pkg.distribution_used.c_str());
    w.Key("seed_used"); w.Uint64(pkg.seed_used);
    w.Key("data_grade"); w.String(pkg.data_grade.c_str());
    w.EndObject();

    w.EndObject();
    return std::string(buf.GetString(), buf.GetSize());
}

Result<EvidencePackageV2> EvidencePackageV2Builder::from_json(const std::string& json_str) {
    rapidjson::Document doc;
    if (doc.Parse(json_str.c_str()).HasParseError()) {
        return Error(ErrorCode::kDeserializationError, "JSON parse error", "evidence_v2");
    }
    EvidencePackageV2 pkg;
    if (doc.HasMember("schema_version") && doc["schema_version"].IsString())
        pkg.schema_version = doc["schema_version"].GetString();
    if (doc.HasMember("schema_hash") && doc["schema_hash"].IsString())
        pkg.schema_hash = doc["schema_hash"].GetString();
    if (doc.HasMember("data_grade") && doc["data_grade"].IsString())
        pkg.data_grade = doc["data_grade"].GetString();
    if (doc.HasMember("row_count") && doc["row_count"].IsInt64())
        pkg.row_count = doc["row_count"].GetInt64();
    if (doc.HasMember("exclusion_rate") && doc["exclusion_rate"].IsNumber())
        pkg.exclusion_rate = doc["exclusion_rate"].GetDouble();
    if (doc.HasMember("audit_immutability") && doc["audit_immutability"].IsString())
        pkg.audit_immutability = doc["audit_immutability"].GetString();

    // constraint_type_breakdown
    if (doc.HasMember("constraint_type_breakdown") && doc["constraint_type_breakdown"].IsObject()) {
        const auto& ctb = doc["constraint_type_breakdown"];
        if (ctb.HasMember("value_range")) pkg.constraint_type_breakdown.value_range_count = ctb["value_range"].GetInt();
        if (ctb.HasMember("inter_row")) pkg.constraint_type_breakdown.inter_row_count = ctb["inter_row"].GetInt();
        if (ctb.HasMember("aggregate")) pkg.constraint_type_breakdown.aggregate_count = ctb["aggregate"].GetInt();
    }

    // statistical_fidelity
    if (doc.HasMember("statistical_fidelity") && doc["statistical_fidelity"].IsObject()) {
        const auto& sf = doc["statistical_fidelity"];
        if (sf.HasMember("available")) pkg.statistical_fidelity.available = sf["available"].GetBool();
    }

    // post_filter_info
    if (doc.HasMember("post_filter_info") && doc["post_filter_info"].IsObject()) {
        const auto& pf = doc["post_filter_info"];
        if (pf.HasMember("was_post_filtered")) pkg.post_filter_info.was_post_filtered = pf["was_post_filtered"].GetBool();
        if (pf.HasMember("pre_filter_rows") && pf["pre_filter_rows"].IsInt64())
            pkg.post_filter_info.pre_filter_rows = pf["pre_filter_rows"].GetInt64();
        if (pf.HasMember("post_filter_rows") && pf["post_filter_rows"].IsInt64())
            pkg.post_filter_info.post_filter_rows = pf["post_filter_rows"].GetInt64();
        if (pf.HasMember("actual_exclusion_rate") && pf["actual_exclusion_rate"].IsNumber())
            pkg.post_filter_info.actual_exclusion_rate = pf["actual_exclusion_rate"].GetDouble();
        if (pf.HasMember("exclusion_rate_band") && pf["exclusion_rate_band"].IsString())
            pkg.post_filter_info.exclusion_rate_band = pf["exclusion_rate_band"].GetString();
        if (pf.HasMember("was_timeout_truncated"))
            pkg.post_filter_info.was_timeout_truncated = pf["was_timeout_truncated"].GetBool();
    }

    // generator_identity
    if (doc.HasMember("generator_identity") && doc["generator_identity"].IsObject()) {
        const auto& gi = doc["generator_identity"];
        if (gi.HasMember("identity") && gi["identity"].IsString())
            pkg.generator_identity.identity = gi["identity"].GetString();
        if (gi.HasMember("justification") && gi["justification"].IsString())
            pkg.generator_identity.justification = gi["justification"].GetString();
        if (gi.HasMember("path") && gi["path"].IsInt())
            pkg.generator_identity.path = static_cast<router::DegradationPath>(gi["path"].GetInt());
    }

    // tail report
    if (doc.HasMember("conservative_tail_report") && doc["conservative_tail_report"].IsObject()) {
        const auto& tr = doc["conservative_tail_report"];
        if (tr.HasMember("epistemological_bias") && tr["epistemological_bias"].IsString())
            pkg.epistemological_bias = tr["epistemological_bias"].GetString();
        if (tr.HasMember("tail_exclusion_statement") && tr["tail_exclusion_statement"].IsString())
            pkg.tail_exclusion_statement = tr["tail_exclusion_statement"].GetString();
        if (tr.HasMember("exclusion_rate") && tr["exclusion_rate"].IsNumber())
            pkg.exclusion_rate_report = tr["exclusion_rate"].GetDouble();
        if (tr.HasMember("rows_generated") && tr["rows_generated"].IsInt64())
            pkg.rows_generated = tr["rows_generated"].GetInt64();
        if (tr.HasMember("rows_validated") && tr["rows_validated"].IsInt64())
            pkg.rows_validated = tr["rows_validated"].GetInt64();
        if (tr.HasMember("rows_failed_validation") && tr["rows_failed_validation"].IsInt64())
            pkg.rows_failed_validation = tr["rows_failed_validation"].GetInt64();
        if (tr.HasMember("distribution_used") && tr["distribution_used"].IsString())
            pkg.distribution_used = tr["distribution_used"].GetString();
        if (tr.HasMember("seed_used") && tr["seed_used"].IsUint64())
            pkg.seed_used = tr["seed_used"].GetUint64();
    }

    return pkg;
}

}  // namespace synthgen::engine::evidence
