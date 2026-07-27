#include "api/server.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <httplib.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <iostream>
#include <sstream>

namespace synthgen::api {

namespace {

std::string error_to_json(ErrorCode code, const std::string& message, const std::string& component) {
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> w(buf);
    w.StartObject();
    w.Key("error");
    w.StartObject();
    w.Key("code");
    w.String("synthgen_error");
    w.Key("message");
    w.String(message.c_str());
    w.Key("component");
    w.String(component.c_str());
    w.EndObject();
    w.EndObject();
    return std::string(buf.GetString(), buf.GetSize());
}

int error_to_http_status(ErrorCode code) {
    switch (code) {
        case ErrorCode::kInvalidArgument:
        case ErrorCode::kInvalidRange:
        case ErrorCode::kInvalidSchema:
        case ErrorCode::kSyntaxError:
            return 400;
        case ErrorCode::kNotFound:
        case ErrorCode::kTableNotFound:
            return 404;
        case ErrorCode::kInternalError:
            return 500;
        default:
            return 400;
    }
}

// Parse JSON body from request
bool parse_json_body(const httplib::Request& req, rapidjson::Document& doc) {
    if (req.body.empty()) return false;
    return !doc.Parse(req.body.c_str()).HasParseError();
}

std::string get_string(const rapidjson::Value& v, const char* key, const char* def = "") {
    if (v.HasMember(key) && v[key].IsString()) return v[key].GetString();
    return def;
}

}  // namespace

SynthGenServer::SynthGenServer(int port)
    : port_(port), server_(std::make_unique<httplib::Server>()) {
    register_routes();
}

SynthGenServer::~SynthGenServer() {
    stop();
}

void SynthGenServer::register_routes() {
    // POST /v1/types
    server_->Post("/v1/types", [this](const httplib::Request& req, httplib::Response& res) {
        scaffold::SpanGuard span("api", "define_type", "api_dt");
        rapidjson::Document doc;
        if (!parse_json_body(req, doc) || !doc.IsObject()) {
            res.status = 400;
            res.set_content(error_to_json(ErrorCode::kInvalidArgument,
                "Invalid JSON body", "api"), "application/json");
            return;
        }

        DefineTypeRequest dt_req;
        dt_req.type_name = get_string(doc, "type_name");
        if (dt_req.type_name.empty()) {
            res.status = 400;
            res.set_content(error_to_json(ErrorCode::kInvalidArgument,
                "type_name is required", "api"), "application/json");
            return;
        }

        if (doc.HasMember("columns") && doc["columns"].IsArray()) {
            for (const auto& col : doc["columns"].GetArray()) {
                DefineTypeRequest::ColumnDef cd;
                cd.name = get_string(col, "name");
                cd.type = get_string(col, "type", "FLOAT");
                if (col.HasMember("not_null") && col["not_null"].IsBool())
                    cd.not_null = col["not_null"].GetBool();
                if (col.HasMember("is_order") && col["is_order"].IsBool())
                    cd.is_order = col["is_order"].GetBool();
                if (col.HasMember("range_min") && col["range_min"].IsNumber())
                    cd.range_min = col["range_min"].GetDouble();
                if (col.HasMember("range_max") && col["range_max"].IsNumber())
                    cd.range_max = col["range_max"].GetDouble();
                if (col.HasMember("enum_values") && col["enum_values"].IsArray()) {
                    for (const auto& v : col["enum_values"].GetArray()) {
                        if (v.IsString()) cd.enum_values.push_back(v.GetString());
                    }
                }
                dt_req.columns.push_back(cd);
            }
        }

        auto result = service_.define_type(dt_req);
        if (!result.ok()) {
            res.status = error_to_http_status(result.error().code);
            res.set_content(error_to_json(result.error().code,
                result.error().message, result.error().component), "application/json");
            return;
        }

        rapidjson::StringBuffer buf;
        rapidjson::Writer<rapidjson::StringBuffer> w(buf);
        w.StartObject();
        w.Key("type_name"); w.String(result.value().type_name.c_str());
        w.Key("column_count"); w.Int(result.value().column_count);
        w.EndObject();
        res.set_content(std::string(buf.GetString(), buf.GetSize()), "application/json");
    });

    // POST /v1/types/{name}/data
    server_->Post("/v1/types/(.+)/data", [this](const httplib::Request& req, httplib::Response& res) {
        scaffold::SpanGuard span("api", "load_data", "api_ld");
        rapidjson::Document doc;
        LoadDataRequest ld_req;
        ld_req.type_name = req.matches[1];
        if (parse_json_body(req, doc) && doc.IsObject()) {
            ld_req.path = get_string(doc, "path");
            ld_req.mode = get_string(doc, "mode", "strict");
        }

        auto result = service_.load_data(ld_req);
        if (!result.ok()) {
            res.status = error_to_http_status(result.error().code);
            res.set_content(error_to_json(result.error().code,
                result.error().message, result.error().component), "application/json");
            return;
        }

        rapidjson::StringBuffer buf;
        rapidjson::Writer<rapidjson::StringBuffer> w(buf);
        w.StartObject();
        w.Key("type_name"); w.String(result.value().type_name.c_str());
        w.Key("rows_imported"); w.Int64(result.value().rows_imported);
        w.Key("status"); w.String(result.value().status.c_str());
        w.EndObject();
        res.set_content(std::string(buf.GetString(), buf.GetSize()), "application/json");
    });

    // POST /v1/constraints
    server_->Post("/v1/constraints", [this](const httplib::Request& req, httplib::Response& res) {
        scaffold::SpanGuard span("api", "define_constraint", "api_dc");
        rapidjson::Document doc;
        if (!parse_json_body(req, doc) || !doc.IsObject()) {
            res.status = 400;
            res.set_content(error_to_json(ErrorCode::kInvalidArgument,
                "Invalid JSON body", "api"), "application/json");
            return;
        }

        DefineConstraintRequest dc_req;
        dc_req.constraint_name = get_string(doc, "constraint_name");
        dc_req.type_name = get_string(doc, "type_name");

        if (doc.HasMember("checks") && doc["checks"].IsArray()) {
            for (const auto& chk : doc["checks"].GetArray()) {
                DefineConstraintRequest::RangeCheck rc;
                rc.column = get_string(chk, "column");
                if (chk.HasMember("min") && chk["min"].IsNumber())
                    rc.min_val = chk["min"].GetDouble();
                if (chk.HasMember("max") && chk["max"].IsNumber())
                    rc.max_val = chk["max"].GetDouble();
                dc_req.checks.push_back(rc);
            }
        }

        auto result = service_.define_constraint(dc_req);
        if (!result.ok()) {
            res.status = error_to_http_status(result.error().code);
            res.set_content(error_to_json(result.error().code,
                result.error().message, result.error().component), "application/json");
            return;
        }

        rapidjson::StringBuffer buf;
        rapidjson::Writer<rapidjson::StringBuffer> w(buf);
        w.StartObject();
        w.Key("constraint_name"); w.String(result.value().constraint_name.c_str());
        w.Key("type_name"); w.String(result.value().type_name.c_str());
        w.Key("check_count"); w.Int(result.value().check_count);
        w.EndObject();
        res.set_content(std::string(buf.GetString(), buf.GetSize()), "application/json");
    });

    // POST /v1/explain
    server_->Post("/v1/explain", [this](const httplib::Request& req, httplib::Response& res) {
        scaffold::SpanGuard span("api", "explain", "api_ex");
        rapidjson::Document doc;
        if (!parse_json_body(req, doc) || !doc.IsObject()) {
            res.status = 400;
            res.set_content(error_to_json(ErrorCode::kInvalidArgument,
                "Invalid JSON body", "api"), "application/json");
            return;
        }

        ExplainRequest ex_req;
        ex_req.type_name = get_string(doc, "type_name");
        if (doc.HasMember("constraints") && doc["constraints"].IsArray()) {
            for (const auto& c : doc["constraints"].GetArray()) {
                if (c.IsString()) ex_req.constraints.push_back(c.GetString());
            }
        }

        auto result = service_.explain(ex_req);
        if (!result.ok()) {
            res.status = error_to_http_status(result.error().code);
            res.set_content(error_to_json(result.error().code,
                result.error().message, result.error().component), "application/json");
            return;
        }

        rapidjson::StringBuffer buf;
        rapidjson::Writer<rapidjson::StringBuffer> w(buf);
        w.StartObject();
        w.Key("execution_mode"); w.String(result.value().execution_mode.c_str());
        w.Key("path"); w.String(result.value().path.c_str());
        w.Key("constraint_classification");
        w.StartObject();
        for (const auto& [k, v] : result.value().constraint_classification) {
            w.Key(k.c_str()); w.Int(v);
        }
        w.EndObject();
        w.EndObject();
        res.set_content(std::string(buf.GetString(), buf.GetSize()), "application/json");
    });

    // POST /v1/generate
    server_->Post("/v1/generate", [this](const httplib::Request& req, httplib::Response& res) {
        scaffold::SpanGuard span("api", "generate", "api_gen");
        rapidjson::Document doc;
        if (!parse_json_body(req, doc) || !doc.IsObject()) {
            res.status = 400;
            res.set_content(error_to_json(ErrorCode::kInvalidArgument,
                "Invalid JSON body", "api"), "application/json");
            return;
        }

        GenerateRequest gen_req;
        gen_req.type_name = get_string(doc, "type_name");
        if (doc.HasMember("limit") && doc["limit"].IsInt64())
            gen_req.limit = doc["limit"].GetInt64();
        gen_req.distribution = get_string(doc, "distribution", "uniform");
        if (doc.HasMember("seed") && doc["seed"].IsUint64())
            gen_req.seed = doc["seed"].GetUint64();
        if (doc.HasMember("constraints") && doc["constraints"].IsArray()) {
            for (const auto& c : doc["constraints"].GetArray()) {
                if (c.IsString()) gen_req.constraints.push_back(c.GetString());
            }
        }

        auto result = service_.generate(gen_req);
        if (!result.ok()) {
            res.status = error_to_http_status(result.error().code);
            res.set_content(error_to_json(result.error().code,
                result.error().message, result.error().component), "application/json");
            return;
        }

        // Return the evidence JSON directly (it's already a complete JSON string)
        // Wrap with stats
        const auto& gen = result.value();
        std::string evidence = gen.evidence_json;

        // We need to add data_format and stats around the evidence
        // Parse evidence and add wrapper
        rapidjson::Document resp_doc;
        resp_doc.Parse(evidence.c_str());
        if (resp_doc.HasParseError()) {
            res.status = 500;
            res.set_content("{\"error\":{\"code\":\"internal\",\"message\":\"Evidence serialization failed\"}}",
                          "application/json");
            return;
        }

        // Add data and stats fields
        rapidjson::Value data_val(rapidjson::kObjectType);
        data_val.AddMember("format", "parquet", resp_doc.GetAllocator());
        resp_doc.AddMember("data", data_val, resp_doc.GetAllocator());

        rapidjson::Value stats_val(rapidjson::kObjectType);
        stats_val.AddMember("rows_generated", gen.stats.rows_generated, resp_doc.GetAllocator());
        stats_val.AddMember("elapsed_ms", gen.stats.elapsed_ms, resp_doc.GetAllocator());
        stats_val.AddMember("distribution_used",
            rapidjson::Value(gen.stats.distribution_used.c_str(), resp_doc.GetAllocator()),
            resp_doc.GetAllocator());
        resp_doc.AddMember("stats", stats_val, resp_doc.GetAllocator());

        rapidjson::StringBuffer buf;
        rapidjson::Writer<rapidjson::StringBuffer> w(buf);
        resp_doc.Accept(w);
        res.set_content(std::string(buf.GetString(), buf.GetSize()), "application/json");
    });

    // GET /v1/metrics
    server_->Get("/v1/metrics", [](const httplib::Request&, httplib::Response& res) {
        std::ostringstream oss;
        for (const auto& [name, val] : scaffold::MetricsRegistry::instance().all_counters()) {
            oss << name << " " << val << "\n";
        }
        res.set_content(oss.str(), "text/plain");
    });

    // GET /v1/health
    server_->Get("/v1/health", [this](const httplib::Request&, httplib::Response& res) {
        auto health = service_.health();
        rapidjson::StringBuffer buf;
        rapidjson::Writer<rapidjson::StringBuffer> w(buf);
        w.StartObject();
        w.Key("status"); w.String(health.status.c_str());
        w.Key("version"); w.String(health.version.c_str());
        w.Key("components");
        w.StartObject();
        for (const auto& [k, v] : health.components) {
            w.Key(k.c_str()); w.String(v.c_str());
        }
        w.EndObject();
        w.EndObject();
        res.set_content(std::string(buf.GetString(), buf.GetSize()), "application/json");
    });
}

void SynthGenServer::start() {
    std::cout << "Starting SynthGen server on port " << port_ << std::endl;
    server_->listen("0.0.0.0", port_);
}

void SynthGenServer::stop() {
    if (server_) server_->stop();
}

SynthGenService& SynthGenServer::service() {
    return service_;
}

}  // namespace synthgen::api
