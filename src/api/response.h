#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace synthgen::api {

struct SchemaRef {
    std::string type_name;
    int column_count = 0;
};

struct ImportResult {
    std::string type_name;
    int64_t rows_imported = 0;
    std::string status;  // "success" or "partial"
};

struct ConstraintRef {
    std::string constraint_name;
    std::string type_name;
    int check_count = 0;
};

struct ExplainResult {
    std::string execution_mode = "row_by_row";
    std::string path = "physics_sampling";
    std::map<std::string, int> constraint_classification;
};

struct GenerationStatsResponse {
    int64_t rows_generated = 0;
    int64_t elapsed_ms = 0;
    std::string distribution_used;
};

struct GenerateResult {
    std::string data_format = "parquet";
    std::string data_path;
    std::string evidence_json;
    GenerationStatsResponse stats;
};

struct HealthResponse {
    std::string status = "healthy";
    std::string version = "v1.0.0";
    std::map<std::string, std::string> components;
};

}  // namespace synthgen::api
