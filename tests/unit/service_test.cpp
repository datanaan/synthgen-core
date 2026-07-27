#include <gtest/gtest.h>
#include "api/service.h"
#include "api/server.h"
#include "scaffold/metrics.h"

#include <thread>
#include <chrono>

using namespace synthgen::api;

class ServiceTest : public ::testing::Test {
protected:
    SynthGenService service;

    DefineTypeRequest make_sensor_type() {
        DefineTypeRequest req;
        req.type_name = "sensor_log";
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
};

// ===== DefineType Tests =====

TEST_F(ServiceTest, DefineTypeSuccess) {
    auto req = make_sensor_type();
    auto result = service.define_type(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().type_name, "sensor_log");
    EXPECT_EQ(result.value().column_count, 2);
}

TEST_F(ServiceTest, DefineTypeEmptyName) {
    DefineTypeRequest req;
    req.type_name = "";
    DefineTypeRequest::ColumnDef col;
    col.name = "x";
    col.type = "FLOAT";
    req.columns.push_back(col);

    auto result = service.define_type(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kInvalidArgument);
}

TEST_F(ServiceTest, DefineTypeEmptyColumns) {
    DefineTypeRequest req;
    req.type_name = "empty_schema";
    auto result = service.define_type(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kInvalidArgument);
}

TEST_F(ServiceTest, DefineTypeEnumColumn) {
    DefineTypeRequest req;
    req.type_name = "status_type";
    DefineTypeRequest::ColumnDef col;
    col.name = "status";
    col.type = "ENUM";
    col.enum_values = {"normal", "warning", "fault"};
    req.columns.push_back(col);

    auto result = service.define_type(req);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().column_count, 1);
}

// ===== LoadData Tests =====

TEST_F(ServiceTest, LoadDataSuccess) {
    service.define_type(make_sensor_type());

    LoadDataRequest req;
    req.type_name = "sensor_log";
    req.path = "/data/test.parquet";
    auto result = service.load_data(req);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().status, "success");
}

TEST_F(ServiceTest, LoadDataTypeNotFound) {
    LoadDataRequest req;
    req.type_name = "nonexistent";
    req.path = "/data/test.parquet";
    auto result = service.load_data(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kNotFound);
}

// ===== DefineConstraint Tests =====

TEST_F(ServiceTest, DefineConstraintSuccess) {
    service.define_type(make_sensor_type());

    DefineConstraintRequest req;
    req.constraint_name = "safe_range";
    req.type_name = "sensor_log";
    DefineConstraintRequest::RangeCheck rc;
    rc.column = "temperature";
    rc.min_val = -10.0;
    rc.max_val = 45.0;
    req.checks.push_back(rc);

    auto result = service.define_constraint(req);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().constraint_name, "safe_range");
    EXPECT_EQ(result.value().check_count, 1);
}

TEST_F(ServiceTest, DefineConstraintTypeNotFound) {
    DefineConstraintRequest req;
    req.constraint_name = "test";
    req.type_name = "nonexistent";
    DefineConstraintRequest::RangeCheck rc;
    rc.column = "temp";
    rc.min_val = 0.0;
    req.checks.push_back(rc);

    auto result = service.define_constraint(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kNotFound);
}

TEST_F(ServiceTest, DefineConstraintEmptyChecks) {
    service.define_type(make_sensor_type());

    DefineConstraintRequest req;
    req.constraint_name = "empty_constraint";
    req.type_name = "sensor_log";

    auto result = service.define_constraint(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kInvalidArgument);
}

// ===== Explain Tests =====

TEST_F(ServiceTest, ExplainSuccess) {
    service.define_type(make_sensor_type());

    ExplainRequest req;
    req.type_name = "sensor_log";
    req.constraints = {"safe_range"};

    auto result = service.explain(req);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, "row_by_row");
    EXPECT_EQ(result.value().path, "physics_sampling");
}

TEST_F(ServiceTest, ExplainTypeNotFound) {
    ExplainRequest req;
    req.type_name = "nonexistent";
    auto result = service.explain(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kNotFound);
}

// ===== Generate Tests =====

TEST_F(ServiceTest, GenerateSuccess) {
    service.define_type(make_sensor_type());

    DefineConstraintRequest creq;
    creq.constraint_name = "safe_range";
    creq.type_name = "sensor_log";
    DefineConstraintRequest::RangeCheck rc;
    rc.column = "temperature";
    rc.min_val = -10.0;
    rc.max_val = 45.0;
    creq.checks.push_back(rc);
    service.define_constraint(creq);

    GenerateRequest req;
    req.type_name = "sensor_log";
    req.constraints = {"safe_range"};
    req.limit = 100;
    req.seed = 42;

    auto result = service.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 100);
    EXPECT_EQ(result.value().data_format, "parquet");
    EXPECT_FALSE(result.value().evidence_json.empty());
}

TEST_F(ServiceTest, GenerateTypeNotFound) {
    GenerateRequest req;
    req.type_name = "nonexistent";
    req.limit = 10;
    auto result = service.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kNotFound);
}

TEST_F(ServiceTest, GenerateInvalidLimit) {
    service.define_type(make_sensor_type());

    GenerateRequest req;
    req.type_name = "sensor_log";
    req.limit = 0;
    auto result = service.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kInvalidArgument);
}

TEST_F(ServiceTest, GenerateConstraintNotFound) {
    service.define_type(make_sensor_type());

    GenerateRequest req;
    req.type_name = "sensor_log";
    req.constraints = {"nonexistent_constraint"};
    req.limit = 10;
    auto result = service.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kNotFound);
}

// ===== Health Test =====

TEST_F(ServiceTest, HealthCheck) {
    auto health = service.health();
    EXPECT_EQ(health.status, "healthy");
    EXPECT_EQ(health.version, "v1.0.0");
    EXPECT_EQ(health.components.at("parser"), "ok");
    EXPECT_EQ(health.components.at("storage"), "ok");
    EXPECT_EQ(health.components.at("physics_engine"), "ok");
}

// ===== Full Pipeline Test =====

TEST_F(ServiceTest, FullPipeline) {
    // 1. Define type
    auto dt = service.define_type(make_sensor_type());
    ASSERT_TRUE(dt.ok());

    // 2. Load data (stub)
    LoadDataRequest ld;
    ld.type_name = "sensor_log";
    ld.path = "/data/test.parquet";
    auto li = service.load_data(ld);
    ASSERT_TRUE(li.ok());

    // 3. Define constraint
    DefineConstraintRequest dc;
    dc.constraint_name = "safe_range";
    dc.type_name = "sensor_log";
    DefineConstraintRequest::RangeCheck rc;
    rc.column = "temperature";
    rc.min_val = -10.0;
    rc.max_val = 45.0;
    dc.checks.push_back(rc);
    auto cr = service.define_constraint(dc);
    ASSERT_TRUE(cr.ok());

    // 4. Explain
    ExplainRequest ex;
    ex.type_name = "sensor_log";
    ex.constraints = {"safe_range"};
    auto er = service.explain(ex);
    ASSERT_TRUE(er.ok());

    // 5. Generate
    GenerateRequest gen;
    gen.type_name = "sensor_log";
    gen.constraints = {"safe_range"};
    gen.limit = 50;
    gen.seed = 42;
    auto gr = service.generate(gen);
    ASSERT_TRUE(gr.ok()) << gr.error().message;

    // Verify evidence contains correct fields
    EXPECT_NE(gr.value().evidence_json.find("\"physics_guaranteed\""), std::string::npos);
    EXPECT_NE(gr.value().evidence_json.find("\"not_applicable\""), std::string::npos);
    EXPECT_NE(gr.value().evidence_json.find("\"physical_first\""), std::string::npos);
}

// ===== Deterministic Seed Test =====

TEST_F(ServiceTest, DeterministicGeneration) {
    service.define_type(make_sensor_type());

    DefineConstraintRequest dc;
    dc.constraint_name = "range1";
    dc.type_name = "sensor_log";
    DefineConstraintRequest::RangeCheck rc;
    rc.column = "temperature";
    rc.min_val = -10.0;
    rc.max_val = 45.0;
    dc.checks.push_back(rc);
    service.define_constraint(dc);

    GenerateRequest req1;
    req1.type_name = "sensor_log";
    req1.constraints = {"range1"};
    req1.limit = 10;
    req1.seed = 42;

    GenerateRequest req2 = req1;

    auto r1 = service.generate(req1);
    auto r2 = service.generate(req2);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(r1.value().evidence_json, r2.value().evidence_json);
}
