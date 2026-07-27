#pragma once

#include "api/request.h"
#include "api/response.h"
#include "common/result.h"
#include "schema/schema.h"
#include "schema/schema_registry.h"
#include "schema/schema_builder.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/evidence/tail_report.h"
#include "engine/evidence/evidence_package_builder.h"
#include "storage/storage_error.h"

#include <map>
#include <string>
#include <memory>

namespace synthgen::api {

class SynthGenService {
public:
    SynthGenService();

    Result<SchemaRef> define_type(const DefineTypeRequest& req);
    Result<ImportResult> load_data(const LoadDataRequest& req);
    Result<ConstraintRef> define_constraint(const DefineConstraintRequest& req);
    Result<ExplainResult> explain(const ExplainRequest& req);
    Result<GenerateResult> generate(const GenerateRequest& req);
    HealthResponse health() const;

private:
    schema::SchemaRegistry registry_;
    // Store schemas by type name
    std::map<std::string, schema::Schema> schemas_;
    // Store constraints by constraint name
    struct StoredConstraint {
        std::string type_name;
        std::vector<parser::ast::ConstraintItem> items;
    };
    std::map<std::string, StoredConstraint> constraints_;
};

}  // namespace synthgen::api
