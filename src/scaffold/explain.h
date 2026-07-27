#pragma once
#include <string>
#include <map>
#include <vector>

namespace synthgen::scaffold {

enum class ExecutionMode {
    kRowByRow,
    kStatefulBatch,
    kTwoPhase,
};

struct ConstraintClassification {
    int value_range = 0;
    int inter_row = 0;
    int aggregate = 0;
};

struct ExplainInfo {
    ExecutionMode execution_mode = ExecutionMode::kRowByRow;
    std::string path;
    ConstraintClassification constraint_classification;
    std::string distribution;
    double estimated_exclusion_rate = 0.0;
    std::vector<std::string> supported_statements;
    std::vector<std::string> unsupported_in_v1;
    std::string version = "v1";

    // v2 additions
    double volume_ratio = 0.0;
    std::string data_source;
    int degradation_path = 0;  // static_cast<int>(DegradationPath)
    std::string selection_reason;
    bool data_engine_available = false;
};

}  // namespace synthgen::scaffold
