// Service-level integration tests — bypassing HTTP, directly testing SynthGenService
#include <gtest/gtest.h>

#include "api/service.h"
#include "common/result.h"

#include <string>
#include <vector>

using namespace synthgen;
using namespace synthgen::api;

// Helper: build a minimal 2-column FLOAT type definition
static DefineTypeRequest make_two_float_type(const std::string& name = "sensor_log") {
    DefineTypeRequest req;
    req.type_name = name;

    DefineTypeRequest::ColumnDef temp;
    temp.name = "temperature";
    temp.type = "FLOAT";
    temp.range_min = -50.0;
    temp.range_max = 80.0;
    req.columns.push_back(temp);

    DefineTypeRequest::ColumnDef pressure;
    pressure.name = "pressure";
    pressure.type = "FLOAT";
    pressure.range_min = 900.0;
    pressure.range_max = 1100.0;
    req.columns.push_back(pressure);

    return req;
}

// Helper: build a type with ENUM column
static DefineTypeRequest make_enum_type(const std::string& name = "status_type") {
    DefineTypeRequest req;
    req.type_name = name;

    DefineTypeRequest::ColumnDef status;
    status.name = "status";
    status.type = "ENUM";
    status.enum_values = {"normal", "warning", "fault"};
    req.columns.push_back(status);

    DefineTypeRequest::ColumnDef value;
    value.name = "value";
    value.type = "FLOAT";
    value.range_min = 0.0;
    value.range_max = 100.0;
    req.columns.push_back(value);

    return req;
}

// ============================================================================
// 1. define_type basic (2 FLOAT columns)
// ============================================================================
TEST(ServiceIntegrationTest, DefineTypeBasic) {
    SynthGenService service;
    auto req = make_two_float_type();
    auto result = service.define_type(req);

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().type_name, "sensor_log");
    EXPECT_EQ(result.value().column_count, 2);
}

// ============================================================================
// 2. define_type with ENUM column
// ============================================================================
TEST(ServiceIntegrationTest, DefineTypeWithEnum) {
    SynthGenService service;
    auto req = make_enum_type();
    auto result = service.define_type(req);

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().type_name, "status_type");
    EXPECT_EQ(result.value().column_count, 2);
}

// ============================================================================
// 3. define_type multiple types independently
// ============================================================================
TEST(ServiceIntegrationTest, DefineTypeMultipleIndependently) {
    SynthGenService service;

    auto r1 = service.define_type(make_two_float_type("type_a"));
    ASSERT_TRUE(r1.ok()) << r1.error().message;
    EXPECT_EQ(r1.value().type_name, "type_a");

    auto r2 = service.define_type(make_enum_type("type_b"));
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    EXPECT_EQ(r2.value().type_name, "type_b");

    // Both should have correct column counts
    EXPECT_EQ(r1.value().column_count, 2);
    EXPECT_EQ(r2.value().column_count, 2);
}

// ============================================================================
// 4. define_type error: empty name -> kInvalidArgument
// ============================================================================
TEST(ServiceIntegrationTest, DefineTypeEmptyName) {
    SynthGenService service;
    DefineTypeRequest req;
    req.type_name = "";
    DefineTypeRequest::ColumnDef col;
    col.name = "x";
    col.type = "FLOAT";
    req.columns.push_back(col);

    auto result = service.define_type(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ============================================================================
// 5. define_type error: no columns -> kInvalidArgument
// ============================================================================
TEST(ServiceIntegrationTest, DefineTypeNoColumns) {
    SynthGenService service;
    DefineTypeRequest req;
    req.type_name = "empty_type";

    auto result = service.define_type(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ============================================================================
// 6. load_data success (type exists, v1 stub)
// ============================================================================
TEST(ServiceIntegrationTest, LoadDataSuccess) {
    SynthGenService service;
    service.define_type(make_two_float_type());

    LoadDataRequest req;
    req.type_name = "sensor_log";
    req.path = "/data/test.parquet";
    auto result = service.load_data(req);

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().type_name, "sensor_log");
    EXPECT_EQ(result.value().status, "success");
}

// ============================================================================
// 7. load_data error: type not found -> kNotFound
// ============================================================================
TEST(ServiceIntegrationTest, LoadDataTypeNotFound) {
    SynthGenService service;
    LoadDataRequest req;
    req.type_name = "nonexistent";
    req.path = "/data/test.parquet";

    auto result = service.load_data(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kNotFound);
}

// ============================================================================
// 8. define_constraint basic (1 check)
// ============================================================================
TEST(ServiceIntegrationTest, DefineConstraintBasic) {
    SynthGenService service;
    service.define_type(make_two_float_type());

    DefineConstraintRequest req;
    req.constraint_name = "safe_temp";
    req.type_name = "sensor_log";
    DefineConstraintRequest::RangeCheck rc;
    rc.column = "temperature";
    rc.min_val = -10.0;
    rc.max_val = 45.0;
    req.checks.push_back(rc);

    auto result = service.define_constraint(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().constraint_name, "safe_temp");
    EXPECT_EQ(result.value().type_name, "sensor_log");
    EXPECT_EQ(result.value().check_count, 1);
}

// ============================================================================
// 9. define_constraint multiple checks (2 checks)
// ============================================================================
TEST(ServiceIntegrationTest, DefineConstraintMultipleChecks) {
    SynthGenService service;
    service.define_type(make_two_float_type());

    DefineConstraintRequest req;
    req.constraint_name = "safe_range";
    req.type_name = "sensor_log";

    DefineConstraintRequest::RangeCheck rc1;
    rc1.column = "temperature";
    rc1.min_val = -10.0;
    rc1.max_val = 45.0;
    req.checks.push_back(rc1);

    DefineConstraintRequest::RangeCheck rc2;
    rc2.column = "pressure";
    rc2.min_val = 950.0;
    rc2.max_val = 1050.0;
    req.checks.push_back(rc2);

    auto result = service.define_constraint(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().constraint_name, "safe_range");
    EXPECT_EQ(result.value().check_count, 2);
}

// ============================================================================
// 10. define_constraint error: type not found -> kNotFound
// ============================================================================
TEST(ServiceIntegrationTest, DefineConstraintTypeNotFound) {
    SynthGenService service;

    DefineConstraintRequest req;
    req.constraint_name = "test";
    req.type_name = "nonexistent";
    DefineConstraintRequest::RangeCheck rc;
    rc.column = "temp";
    rc.min_val = 0.0;
    req.checks.push_back(rc);

    auto result = service.define_constraint(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kNotFound);
}

// ============================================================================
// 11. explain basic
// ============================================================================
TEST(ServiceIntegrationTest, ExplainBasic) {
    SynthGenService service;
    service.define_type(make_two_float_type());

    ExplainRequest req;
    req.type_name = "sensor_log";
    req.constraints = {"safe_range"};

    auto result = service.explain(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().execution_mode, "row_by_row");
    EXPECT_EQ(result.value().path, "physics_sampling");
    EXPECT_GT(result.value().constraint_classification.size(), 0u);
}

// ============================================================================
// 12. health check (verify components)
// ============================================================================
TEST(ServiceIntegrationTest, HealthCheckComponents) {
    SynthGenService service;
    auto health = service.health();

    EXPECT_EQ(health.status, "healthy");
    EXPECT_EQ(health.version, "v1.0.0");
    EXPECT_EQ(health.components.at("parser"), "ok");
    EXPECT_EQ(health.components.at("storage"), "ok");
    EXPECT_EQ(health.components.at("physics_engine"), "ok");
}

// ============================================================================
// 13. generate full pipeline (define -> constraint -> generate -> verify evidence_json)
// ============================================================================
TEST(ServiceIntegrationTest, GenerateFullPipeline) {
    SynthGenService service;
    service.define_type(make_two_float_type());

    DefineConstraintRequest creq;
    creq.constraint_name = "safe_range";
    creq.type_name = "sensor_log";
    DefineConstraintRequest::RangeCheck rc;
    rc.column = "temperature";
    rc.min_val = -10.0;
    rc.max_val = 45.0;
    creq.checks.push_back(rc);
    ASSERT_TRUE(service.define_constraint(creq).ok());

    GenerateRequest req;
    req.type_name = "sensor_log";
    req.constraints = {"safe_range"};
    req.limit = 500;
    req.seed = 42;

    auto result = service.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 500);
    EXPECT_EQ(result.value().data_format, "parquet");
    EXPECT_NE(result.value().evidence_json.find("physics_guaranteed"), std::string::npos);
}

// ============================================================================
// 14. generate with multiple constraints
// ============================================================================
TEST(ServiceIntegrationTest, GenerateMultipleConstraints) {
    SynthGenService service;
    service.define_type(make_two_float_type());

    // Constraint 1: temperature range
    DefineConstraintRequest creq1;
    creq1.constraint_name = "temp_check";
    creq1.type_name = "sensor_log";
    DefineConstraintRequest::RangeCheck rc1;
    rc1.column = "temperature";
    rc1.min_val = -10.0;
    rc1.max_val = 45.0;
    creq1.checks.push_back(rc1);
    ASSERT_TRUE(service.define_constraint(creq1).ok());

    // Constraint 2: pressure range
    DefineConstraintRequest creq2;
    creq2.constraint_name = "press_check";
    creq2.type_name = "sensor_log";
    DefineConstraintRequest::RangeCheck rc2;
    rc2.column = "pressure";
    rc2.min_val = 950.0;
    rc2.max_val = 1050.0;
    creq2.checks.push_back(rc2);
    ASSERT_TRUE(service.define_constraint(creq2).ok());

    GenerateRequest req;
    req.type_name = "sensor_log";
    req.constraints = {"temp_check", "press_check"};
    req.limit = 200;
    req.seed = 99;

    auto result = service.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 200);
    EXPECT_NE(result.value().evidence_json.find("physics_guaranteed"), std::string::npos);
}

// ============================================================================
// 15. generate error: type not found -> kNotFound
// ============================================================================
TEST(ServiceIntegrationTest, GenerateTypeNotFound) {
    SynthGenService service;
    GenerateRequest req;
    req.type_name = "nonexistent";
    req.limit = 10;

    auto result = service.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kNotFound);
}

// ============================================================================
// 16. generate error: invalid limit (0) -> kInvalidArgument
// ============================================================================
TEST(ServiceIntegrationTest, GenerateInvalidLimitZero) {
    SynthGenService service;
    service.define_type(make_two_float_type());

    GenerateRequest req;
    req.type_name = "sensor_log";
    req.limit = 0;

    auto result = service.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ============================================================================
// 17. generate error: negative limit -> kInvalidArgument
// ============================================================================
TEST(ServiceIntegrationTest, GenerateInvalidLimitNegative) {
    SynthGenService service;
    service.define_type(make_two_float_type());

    GenerateRequest req;
    req.type_name = "sensor_log";
    req.limit = -5;

    auto result = service.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ============================================================================
// 18. full workflow: define type with 3 columns -> 2 constraints -> generate 1000 rows
// ============================================================================
TEST(ServiceIntegrationTest, FullWorkflowThreeColumns) {
    SynthGenService service;

    // 1. Define type with 3 columns
    DefineTypeRequest dt_req;
    dt_req.type_name = "machine_sensor";

    DefineTypeRequest::ColumnDef temp_col;
    temp_col.name = "temperature";
    temp_col.type = "FLOAT";
    temp_col.range_min = -50.0;
    temp_col.range_max = 150.0;
    dt_req.columns.push_back(temp_col);

    DefineTypeRequest::ColumnDef vib_col;
    vib_col.name = "vibration";
    vib_col.type = "FLOAT";
    vib_col.range_min = 0.0;
    vib_col.range_max = 100.0;
    dt_req.columns.push_back(vib_col);

    DefineTypeRequest::ColumnDef speed_col;
    speed_col.name = "speed";
    speed_col.type = "FLOAT";
    speed_col.range_min = 0.0;
    speed_col.range_max = 5000.0;
    dt_req.columns.push_back(speed_col);

    auto dt_result = service.define_type(dt_req);
    ASSERT_TRUE(dt_result.ok()) << dt_result.error().message;
    EXPECT_EQ(dt_result.value().type_name, "machine_sensor");
    EXPECT_EQ(dt_result.value().column_count, 3);

    // 2. Define two constraints
    DefineConstraintRequest dc1;
    dc1.constraint_name = "operating_temp";
    dc1.type_name = "machine_sensor";
    DefineConstraintRequest::RangeCheck temp_rc;
    temp_rc.column = "temperature";
    temp_rc.min_val = 10.0;
    temp_rc.max_val = 90.0;
    dc1.checks.push_back(temp_rc);
    auto dc1_result = service.define_constraint(dc1);
    ASSERT_TRUE(dc1_result.ok()) << dc1_result.error().message;

    DefineConstraintRequest dc2;
    dc2.constraint_name = "safe_vibration";
    dc2.type_name = "machine_sensor";
    DefineConstraintRequest::RangeCheck vib_rc;
    vib_rc.column = "vibration";
    vib_rc.min_val = 5.0;
    vib_rc.max_val = 50.0;
    dc2.checks.push_back(vib_rc);
    auto dc2_result = service.define_constraint(dc2);
    ASSERT_TRUE(dc2_result.ok()) << dc2_result.error().message;

    // 3. Generate 1000 rows
    GenerateRequest gen_req;
    gen_req.type_name = "machine_sensor";
    gen_req.constraints = {"operating_temp", "safe_vibration"};
    gen_req.limit = 1000;
    gen_req.seed = 12345;

    auto gen_result = service.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    // 4. Verify evidence
    EXPECT_EQ(gen_result.value().stats.rows_generated, 1000);
    EXPECT_EQ(gen_result.value().data_format, "parquet");
    EXPECT_FALSE(gen_result.value().evidence_json.empty());
    EXPECT_NE(gen_result.value().evidence_json.find("physics_guaranteed"), std::string::npos);
}
