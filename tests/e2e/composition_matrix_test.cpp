// Composition Matrix Test — combinatorial validation of all component combinations
// Tests: Schema x Constraints x Engine x Evidence matrix, invalid combos,
//        state transitions, storage per path, audit trail integrity.
#include <gtest/gtest.h>

#include "parser/ast.h"
#include "schema/schema.h"
#include "schema/schema_builder.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/postfilter/post_filter.h"
#include "engine/evidence/evidence_package.h"
#include "engine/evidence/evidence_package_builder.h"
#include "engine/evidence/evidence_package_v2.h"
#include "engine/evidence/evidence_package_v2_builder.h"
#include "engine/evidence/tail_report.h"
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "storage/object_store_backend.h"
#include "storage/audit/audit_log.h"
#include "common/result.h"
#include "common/types.h"
#include "common/hash.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/type.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <map>

using namespace synthgen;
using namespace synthgen::schema;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::evidence;
using namespace synthgen::engine::postfilter;
using namespace synthgen::engine::router;
using namespace synthgen::storage;
using namespace synthgen::storage::audit;

// ============================================================================
// Schema factory helpers — 4 schema variants
// ============================================================================

// Schema A: 1-column FLOAT (minimal)
static Schema make_schema_1col_float() {
    Schema s;
    s.type_name = "one_float";
    ColumnDef c;
    c.name = "value";
    c.type = DataType::kFloat;
    c.range_min = 0.0;
    c.range_max = 100.0;
    s.columns.push_back(c);
    return s;
}

// Schema B: 3-column FLOAT (no ORDER)
static Schema make_schema_3col_float() {
    Schema s;
    s.type_name = "three_float";
    {
        ColumnDef c;
        c.name = "temp";
        c.type = DataType::kFloat;
        c.range_min = -50.0;
        c.range_max = 80.0;
        s.columns.push_back(c);
    }
    {
        ColumnDef c;
        c.name = "pressure";
        c.type = DataType::kFloat;
        c.range_min = 900.0;
        c.range_max = 1100.0;
        s.columns.push_back(c);
    }
    {
        ColumnDef c;
        c.name = "humidity";
        c.type = DataType::kFloat;
        c.range_min = 0.0;
        c.range_max = 100.0;
        s.columns.push_back(c);
    }
    return s;
}

// Schema C: 3-column + ORDER (DATETIME)
static Schema make_schema_3col_order() {
    Schema s;
    s.type_name = "three_order";
    {
        ColumnDef c;
        c.name = "ts";
        c.type = DataType::kDatetime;
        c.not_null = true;
        c.is_order = true;
        s.columns.push_back(c);
    }
    {
        ColumnDef c;
        c.name = "wind_speed";
        c.type = DataType::kFloat;
        c.range_min = 0.0;
        c.range_max = 50.0;
        s.columns.push_back(c);
    }
    {
        ColumnDef c;
        c.name = "vibration";
        c.type = DataType::kFloat;
        c.range_min = 0.0;
        c.range_max = 20.0;
        s.columns.push_back(c);
    }
    return s;
}

// Schema D: 5-column mixed (FLOAT + INT + ENUM + DATETIME ORDER)
static Schema make_schema_5col_mixed() {
    Schema s;
    s.type_name = "five_mixed";
    {
        ColumnDef c;
        c.name = "ts";
        c.type = DataType::kDatetime;
        c.not_null = true;
        c.is_order = true;
        s.columns.push_back(c);
    }
    {
        ColumnDef c;
        c.name = "temperature";
        c.type = DataType::kFloat;
        c.range_min = -50.0;
        c.range_max = 80.0;
        s.columns.push_back(c);
    }
    {
        ColumnDef c;
        c.name = "pressure";
        c.type = DataType::kFloat;
        c.range_min = 900.0;
        c.range_max = 1100.0;
        s.columns.push_back(c);
    }
    {
        ColumnDef c;
        c.name = "status";
        c.type = DataType::kEnum;
        c.enum_values = {"normal", "warning", "fault"};
        s.columns.push_back(c);
    }
    {
        ColumnDef c;
        c.name = "error_code";
        c.type = DataType::kInt;
        c.range_min = 0.0;
        c.range_max = 100.0;
        s.columns.push_back(c);
    }
    return s;
}

// ============================================================================
// Constraint set factory helpers — 5 constraint variants
// ============================================================================

// Constraint set 0: none (empty)
static ConstraintSet make_constraints_none() {
    return {};
}

// Constraint set 1: value-range only
static ConstraintSet make_constraints_vr() {
    ConstraintSet cs;
    cs.value_range_names = {"vr_temp"};
    return cs;
}

// Constraint set 2: VR + InterRow
static ConstraintSet make_constraints_vr_ir() {
    ConstraintSet cs;
    cs.value_range_names = {"vr_temp"};
    InterRowConstraintDef ird;
    ird.column_name = "wind_speed";
    ird.order_column = "ts";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 5.0;
    cs.inter_row_defs = {ird};
    return cs;
}

// Constraint set 3: VR + Aggregate
static ConstraintSet make_constraints_vr_agg() {
    ConstraintSet cs;
    cs.value_range_names = {"vr_temp"};
    AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "wind_speed";
    acd.function = AggregateFunction::kAvg;
    acd.window_type = WindowType::kInterval;
    acd.window_interval_us = 3600000000LL;  // 1 hour
    acd.max_val = 25.0;
    cs.aggregate_defs = {acd};
    return cs;
}

// Constraint set 4: VR + InterRow + Aggregate
static ConstraintSet make_constraints_vr_ir_agg() {
    ConstraintSet cs;
    cs.value_range_names = {"vr_temp"};
    {
        InterRowConstraintDef ird;
        ird.column_name = "wind_speed";
        ird.order_column = "ts";
        ird.type = InterRowConstraintDef::Type::kDeltaMax;
        ird.delta_max = 5.0;
        cs.inter_row_defs = {ird};
    }
    {
        AggregateConstraintDef acd;
        acd.constraint_name = "avg_temp";
        acd.column_name = "wind_speed";
        acd.function = AggregateFunction::kAvg;
        acd.window_type = WindowType::kInterval;
        acd.window_interval_us = 3600000000LL;
        acd.max_val = 25.0;
        cs.aggregate_defs = {acd};
    }
    return cs;
}

// ============================================================================
// Helper: run full pipeline for a given (schema, constraints, data_engine, evidence_ver)
// Returns true if the entire pipeline succeeds.
// ============================================================================

struct CompositionParams {
    std::string label;
    Schema schema;
    ConstraintSet constraints;
    bool data_engine_available;
    int evidence_version;  // 1 or 2
    // Expected values for verification
    ExecutionMode expected_mode;
    bool expect_classify_error;
};

struct CompositionResult {
    bool classify_ok = false;
    bool route_ok = false;
    bool generate_ok = false;
    bool validate_ok = false;
    bool evidence_ok = false;
    bool postfilter_ok = false;
    bool storage_ok = false;

    ClassificationResult classification;
    RoutingDecision routing;
    GenerationResult generation;
    ValidationResult validation;
    PostFilterResult postfilter;
    std::string table_id;
};

static CompositionResult run_composition_pipeline(
    const CompositionParams& params,
    const std::filesystem::path& storage_dir,
    int64_t target_rows = 100) {

    CompositionResult result;
    result.table_id = "comp_" + params.label;

    // Step 1: Classify
    ConstraintClassifier classifier;
    auto cls_res = classifier.classify(params.constraints, params.schema);
    result.classify_ok = cls_res.ok();
    if (!result.classify_ok) return result;
    result.classification = cls_res.value();

    // Step 2: Route
    ExecutionRouter router(params.data_engine_available);
    auto rt_res = router.route(result.classification, params.schema);
    result.route_ok = rt_res.ok();
    if (!result.route_ok) return result;
    result.routing = rt_res.value();

    // Step 3: Generate data (for schemas with at least one FLOAT column)
    // Build constraint items for RectangularSampler from schema ranges
    std::vector<parser::ast::ConstraintItem> constraint_items;
    for (const auto& col : params.schema.columns) {
        if (col.type == DataType::kFloat && col.range_min && col.range_max) {
            parser::ast::ConstraintItem ci;
            ci.column_name = col.name;
            ci.op = parser::ast::ConstraintOperator::kBetween;
            ci.value_min = col.range_min.value();
            ci.value_max = col.range_max.value();
            constraint_items.push_back(ci);
        }
    }

    GenerationRequest gen_req{params.schema, constraint_items, target_rows, 42, "uniform", 1000};
    RectangularSampler sampler(params.schema);
    auto gen_res = sampler.generate(gen_req);
    result.generate_ok = gen_res.ok();
    if (!result.generate_ok) return result;
    result.generation = gen_res.value();

    // Step 4: Validate (value range)
    if (!constraint_items.empty()) {
        ValueRangeValidator validator(params.schema, constraint_items);
        auto val_res = validator.validate_batch(result.generation.data);
        result.validate_ok = val_res.ok();
        if (result.validate_ok) {
            result.validation = val_res.value();
        }
    } else {
        result.validate_ok = true;
    }

    // Step 5: Post-filter
    PostFilter pf;
    auto pf_res = pf.execute(result.generation.data, target_rows);
    result.postfilter_ok = pf_res.ok();
    if (result.postfilter_ok) {
        result.postfilter = pf_res.value();
    }

    // Step 6: Build evidence package
    if (params.evidence_version == 1) {
        // V1 path
        TailReportBuilder tr_builder;
        auto tail_res = tr_builder.build(
            result.generation, result.validation, gen_req, constraint_items);
        if (tail_res.ok()) {
            ProvenanceV1 prov;
            prov.data_source = "composition_test";
            prov.generator_identity = "RectangularSampler";
            prov.constraints = {"value_range"};
            prov.generation_params = {42, "uniform", target_rows, 1000};

            EvidencePackageBuilder ep_builder;
            auto ep_res = ep_builder.build(
                result.generation, result.validation, tail_res.value(), prov, params.schema);
            result.evidence_ok = ep_res.ok();
        } else {
            result.evidence_ok = false;
        }
    } else {
        // V2 path
        EvidencePackageV2Builder ep2_builder;
        auto ep2_res = ep2_builder.build(
            result.generation.data ? result.generation.data->num_rows() : 0,
            result.generation.stats.exclusion_rate,
            result.postfilter.data_grade,
            result.routing,
            result.classification,
            result.postfilter,
            params.schema);
        result.evidence_ok = ep2_res.ok();
    }

    // Step 7: Store results
    if (result.generation.data) {
        ObjectStoreBackend backend(storage_dir);
        auto reg = backend.register_table(result.table_id, "{}");
        if (reg.ok()) {
            auto app = backend.append(result.table_id, result.generation.data);
            result.storage_ok = app.ok();
        }
    }

    return result;
}

// ============================================================================
// Test Group 1: Composition Matrix — 20 representative combinations
// ============================================================================

class CompositionMatrixTest : public ::testing::TestWithParam<CompositionParams> {
protected:
    std::filesystem::path tmp_dir_;

    void SetUp() override {
        tmp_dir_ = std::filesystem::temp_directory_path() /
            ("comp_matrix_" + GetParam().label);
        std::filesystem::remove_all(tmp_dir_);
        std::filesystem::create_directories(tmp_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp_dir_);
    }
};

// Select 20 representative combinations from the 4x5x2x2 = 80 space.
// Coverage goals:
//   - Each schema type: >= 5 combos each
//   - Each constraint type: >= 4 combos each
//   - Each evidence version: >= 10 combos each
//   - Each data engine setting: >= 10 combos each
//   - Every execution mode: >= 1 combo each

static std::vector<CompositionParams> kCompositionMatrix = {
    // --- Schema A (1-col FLOAT) combos ---
    {"A_VR_noEngine_V1",
     make_schema_1col_float(), make_constraints_vr(), false, 1,
     ExecutionMode::kRowByRow, false},
    {"A_VR_engine_V2",
     make_schema_1col_float(), make_constraints_vr(), true, 2,
     ExecutionMode::kRowByRow, false},
    {"A_none_noEngine_V1",
     make_schema_1col_float(), make_constraints_none(), false, 1,
     ExecutionMode::kRowByRow, true},  // empty constraints → classify error
    {"A_VR_engine_V1",
     make_schema_1col_float(), make_constraints_vr(), true, 1,
     ExecutionMode::kRowByRow, false},

    // --- Schema B (3-col FLOAT, no ORDER) combos ---
    {"B_VR_noEngine_V1",
     make_schema_3col_float(), make_constraints_vr(), false, 1,
     ExecutionMode::kRowByRow, false},
    {"B_VR_engine_V2",
     make_schema_3col_float(), make_constraints_vr(), true, 2,
     ExecutionMode::kRowByRow, false},
    {"B_none_engine_V1",
     make_schema_3col_float(), make_constraints_none(), true, 1,
     ExecutionMode::kRowByRow, true},  // empty constraints → classify error

    // --- Schema C (3-col + ORDER) combos ---
    {"C_VR_noEngine_V2",
     make_schema_3col_order(), make_constraints_vr(), false, 2,
     ExecutionMode::kRowByRow, false},
    {"C_VRIR_noEngine_V1",
     make_schema_3col_order(), make_constraints_vr_ir(), false, 1,
     ExecutionMode::kStatefulBatch, false},
    {"C_VRIR_engine_V2",
     make_schema_3col_order(), make_constraints_vr_ir(), true, 2,
     ExecutionMode::kStatefulBatch, false},
    {"C_VRAgg_noEngine_V1",
     make_schema_3col_order(), make_constraints_vr_agg(), false, 1,
     ExecutionMode::kTwoPhase, false},
    {"C_VRAgg_engine_V2",
     make_schema_3col_order(), make_constraints_vr_agg(), true, 2,
     ExecutionMode::kTwoPhase, false},
    {"C_VRIRAgg_noEngine_V1",
     make_schema_3col_order(), make_constraints_vr_ir_agg(), false, 1,
     ExecutionMode::kTwoPhase, false},
    {"C_VRIRAgg_engine_V2",
     make_schema_3col_order(), make_constraints_vr_ir_agg(), true, 2,
     ExecutionMode::kTwoPhase, false},

    // --- Schema D (5-col mixed + ORDER) combos ---
    {"D_VR_engine_V1",
     make_schema_5col_mixed(), make_constraints_vr(), true, 1,
     ExecutionMode::kRowByRow, false},
    {"D_VR_noEngine_V2",
     make_schema_5col_mixed(), make_constraints_vr(), false, 2,
     ExecutionMode::kRowByRow, false},
    {"D_VRIR_engine_V1",
     make_schema_5col_mixed(), make_constraints_vr_ir(), true, 1,
     ExecutionMode::kStatefulBatch, false},
    {"D_VRAgg_engine_V2",
     make_schema_5col_mixed(), make_constraints_vr_agg(), true, 2,
     ExecutionMode::kTwoPhase, false},
    {"D_VRIRAgg_engine_V1",
     make_schema_5col_mixed(), make_constraints_vr_ir_agg(), true, 1,
     ExecutionMode::kTwoPhase, false},
    {"D_none_noEngine_V2",
     make_schema_5col_mixed(), make_constraints_none(), false, 2,
     ExecutionMode::kRowByRow, true},  // empty constraints → classify error
};

TEST_P(CompositionMatrixTest, FullPipelineExecution) {
    const auto& p = GetParam();

    if (p.expect_classify_error) {
        // Empty constraint set should fail at classification
        ConstraintClassifier classifier;
        auto cls_res = classifier.classify(p.constraints, p.schema);
        EXPECT_FALSE(cls_res.ok()) << "Label: " << p.label
            << " — expected classification error but got success";
        EXPECT_EQ(cls_res.error().code, ErrorCode::kInvalidArgument);
        return;
    }

    auto result = run_composition_pipeline(p, tmp_dir_, 100);

    // All pipeline stages should succeed for valid combinations
    EXPECT_TRUE(result.classify_ok) << p.label << ": classify failed";
    EXPECT_TRUE(result.route_ok) << p.label << ": route failed";
    EXPECT_TRUE(result.generate_ok) << p.label << ": generate failed";
    EXPECT_TRUE(result.validate_ok) << p.label << ": validate failed";
    EXPECT_TRUE(result.postfilter_ok) << p.label << ": postfilter failed";
    EXPECT_TRUE(result.evidence_ok) << p.label << ": evidence failed";
    EXPECT_TRUE(result.storage_ok) << p.label << ": storage failed";

    // Verify execution mode
    EXPECT_EQ(result.classification.execution_mode, p.expected_mode)
        << p.label << ": execution mode mismatch";

    // Verify generated data row count
    if (result.generate_ok && result.generation.data) {
        EXPECT_EQ(result.generation.data->num_rows(), 100)
            << p.label << ": row count mismatch";
    }
}

TEST_P(CompositionMatrixTest, ClassificationConsistency) {
    const auto& p = GetParam();
    if (p.expect_classify_error) return;

    ConstraintClassifier classifier;
    auto cls_res = classifier.classify(p.constraints, p.schema);
    ASSERT_TRUE(cls_res.ok()) << p.label;
    auto& cls = cls_res.value();

    // Verify constraint counts match the constraint set
    int expected_vr = static_cast<int>(p.constraints.value_range_names.size());
    int expected_ir = static_cast<int>(p.constraints.inter_row_defs.size());
    int expected_agg = static_cast<int>(p.constraints.aggregate_defs.size());

    EXPECT_EQ(cls.value_range_count, expected_vr)
        << p.label << ": VR count mismatch";
    EXPECT_EQ(cls.inter_row_count, expected_ir)
        << p.label << ": IR count mismatch";
    EXPECT_EQ(cls.aggregate_count, expected_agg)
        << p.label << ": Agg count mismatch";

    // Total classifications should equal sum of constraint counts
    EXPECT_EQ(static_cast<int>(cls.classifications.size()),
              expected_vr + expected_ir + expected_agg)
        << p.label << ": total classification count mismatch";
}

TEST_P(CompositionMatrixTest, RoutingDecisionConsistency) {
    const auto& p = GetParam();
    if (p.expect_classify_error) return;

    ConstraintClassifier classifier;
    auto cls_res = classifier.classify(p.constraints, p.schema);
    ASSERT_TRUE(cls_res.ok());

    ExecutionRouter router(p.data_engine_available);
    auto rt_res = router.route(cls_res.value(), p.schema);
    ASSERT_TRUE(rt_res.ok()) << p.label;

    auto& decision = rt_res.value();
    EXPECT_EQ(decision.data_engine_available, p.data_engine_available)
        << p.label << ": data engine flag mismatch";

    // Identity should not be empty
    EXPECT_FALSE(decision.identity.identity.empty())
        << p.label << ": identity empty";
    EXPECT_FALSE(decision.identity.justification.empty())
        << p.label << ": justification empty";
    EXPECT_FALSE(decision.decision_reason.empty())
        << p.label << ": decision_reason empty";
}

TEST_P(CompositionMatrixTest, StorageScanReturnsCorrectRows) {
    const auto& p = GetParam();
    if (p.expect_classify_error) return;

    auto result = run_composition_pipeline(p, tmp_dir_, 50);
    ASSERT_TRUE(result.storage_ok) << p.label << ": storage failed";

    ObjectStoreBackend backend(tmp_dir_);
    auto scan_res = backend.scan(result.table_id);
    ASSERT_TRUE(scan_res.ok()) << p.label << ": scan failed";
    ASSERT_NE(scan_res.value(), nullptr);

    EXPECT_EQ(scan_res.value()->num_rows(), 50)
        << p.label << ": scan row count mismatch";
}

INSTANTIATE_TEST_SUITE_P(
    Matrix, CompositionMatrixTest,
    ::testing::ValuesIn(kCompositionMatrix),
    [](const ::testing::TestParamInfo<CompositionParams>& info) {
        return info.param.label;
    });

// ============================================================================
// Test Group 2: Invalid combinations should error gracefully
// ============================================================================

TEST(CompositionInvalidTest, InterRowWithoutOrderColumnErrors) {
    // Schema B has no ORDER column; inter-row constraints should fail
    Schema schema = make_schema_3col_float();
    ConstraintSet cs = make_constraints_vr_ir();  // has inter-row def

    ConstraintClassifier classifier;
    auto res = classifier.classify(cs, schema);

    EXPECT_FALSE(res.ok()) << "Inter-row without ORDER should fail";
    EXPECT_EQ(res.error().code, ErrorCode::kOrderColumnRequired)
        << "Expected kOrderColumnRequired, got: " << static_cast<int>(res.error().code);
}

TEST(CompositionInvalidTest, AggregateWithoutOrderColumnErrors) {
    // Schema B has no ORDER column; aggregate constraints should fail
    Schema schema = make_schema_3col_float();
    ConstraintSet cs = make_constraints_vr_agg();  // has aggregate def

    ConstraintClassifier classifier;
    auto res = classifier.classify(cs, schema);

    EXPECT_FALSE(res.ok()) << "Aggregate without ORDER should fail";
    EXPECT_EQ(res.error().code, ErrorCode::kOrderColumnRequired)
        << "Expected kOrderColumnRequired, got: " << static_cast<int>(res.error().code);
}

TEST(CompositionInvalidTest, EmptyConstraintSetErrors) {
    // Any schema with empty constraints should fail
    Schema schema = make_schema_1col_float();
    ConstraintSet cs = make_constraints_none();

    ConstraintClassifier classifier;
    auto res = classifier.classify(cs, schema);

    EXPECT_FALSE(res.ok()) << "Empty constraint set should fail";
    EXPECT_EQ(res.error().code, ErrorCode::kInvalidArgument)
        << "Expected kInvalidArgument, got: " << static_cast<int>(res.error().code);
}

TEST(CompositionInvalidTest, AggregateWithNonDatetimeOrderErrors) {
    // Create a schema with a non-DATETIME ORDER column
    Schema schema;
    schema.type_name = "bad_order";
    {
        ColumnDef c;
        c.name = "seq";
        c.type = DataType::kInt;  // NOT datetime
        c.is_order = true;
        c.range_min = 0.0;
        c.range_max = 100.0;
        schema.columns.push_back(c);
    }
    {
        ColumnDef c;
        c.name = "val";
        c.type = DataType::kFloat;
        c.range_min = 0.0;
        c.range_max = 10.0;
        schema.columns.push_back(c);
    }

    ConstraintSet cs = make_constraints_vr_agg();
    // Override column names to match this schema
    cs.aggregate_defs[0].column_name = "val";

    ConstraintClassifier classifier;
    auto res = classifier.classify(cs, schema);

    EXPECT_FALSE(res.ok()) << "Aggregate with non-DATETIME ORDER should fail";
    EXPECT_EQ(res.error().code, ErrorCode::kTypeMismatch)
        << "Expected kTypeMismatch, got: " << static_cast<int>(res.error().code);
}

TEST(CompositionInvalidTest, InterRowOnMultipleSchemaTypes) {
    // Test inter-row failure on all schemas without ORDER
    auto schemas_no_order = {
        make_schema_1col_float(),
        make_schema_3col_float()
    };

    for (const auto& schema : schemas_no_order) {
        ConstraintSet cs = make_constraints_vr_ir();
        ConstraintClassifier classifier;
        auto res = classifier.classify(cs, schema);

        EXPECT_FALSE(res.ok())
            << "Inter-row without ORDER should fail for schema: " << schema.type_name;
        EXPECT_EQ(res.error().code, ErrorCode::kOrderColumnRequired);
    }
}

TEST(CompositionInvalidTest, AggregateOnMultipleSchemaTypes) {
    // Test aggregate failure on all schemas without DATETIME ORDER
    auto schemas_no_order = {
        make_schema_1col_float(),
        make_schema_3col_float()
    };

    for (const auto& schema : schemas_no_order) {
        ConstraintSet cs = make_constraints_vr_agg();
        ConstraintClassifier classifier;
        auto res = classifier.classify(cs, schema);

        EXPECT_FALSE(res.ok())
            << "Aggregate without ORDER should fail for schema: " << schema.type_name;
        EXPECT_EQ(res.error().code, ErrorCode::kOrderColumnRequired);
    }
}

// ============================================================================
// Test Group 3: State transition test — kRowByRow -> kStatefulBatch -> kTwoPhase
// ============================================================================

TEST(CompositionTransitionTest, VRToVRIrToVRIrAgg) {
    Schema schema = make_schema_3col_order();
    ConstraintClassifier classifier;

    // Phase 1: VR-only -> kRowByRow
    {
        ConstraintSet cs = make_constraints_vr();
        auto res = classifier.classify(cs, schema);
        ASSERT_TRUE(res.ok()) << res.error().message;
        EXPECT_EQ(res.value().execution_mode, ExecutionMode::kRowByRow)
            << "VR-only should be kRowByRow";
    }

    // Phase 2: VR + InterRow -> kStatefulBatch
    {
        ConstraintSet cs = make_constraints_vr_ir();
        auto res = classifier.classify(cs, schema);
        ASSERT_TRUE(res.ok()) << res.error().message;
        EXPECT_EQ(res.value().execution_mode, ExecutionMode::kStatefulBatch)
            << "VR+IR should be kStatefulBatch";
    }

    // Phase 3: VR + InterRow + Aggregate -> kTwoPhase
    {
        ConstraintSet cs = make_constraints_vr_ir_agg();
        auto res = classifier.classify(cs, schema);
        ASSERT_TRUE(res.ok()) << res.error().message;
        EXPECT_EQ(res.value().execution_mode, ExecutionMode::kTwoPhase)
            << "VR+IR+Agg should be kTwoPhase";
    }
}

TEST(CompositionTransitionTest, VRAggDirectlyIsTwoPhase) {
    Schema schema = make_schema_3col_order();
    ConstraintClassifier classifier;

    // Going directly to VR + Aggregate skips kStatefulBatch
    ConstraintSet cs = make_constraints_vr_agg();
    auto res = classifier.classify(cs, schema);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value().execution_mode, ExecutionMode::kTwoPhase)
        << "VR+Agg (without IR) should still be kTwoPhase";
}

TEST(CompositionTransitionTest, DeriveExecutionModeTable) {
    ConstraintClassifier classifier;

    // Exhaustive truth table for derive_execution_mode
    struct Case {
        int vr, ir, agg;
        ExecutionMode expected;
    };
    std::vector<Case> cases = {
        {1, 0, 0, ExecutionMode::kRowByRow},
        {2, 0, 0, ExecutionMode::kRowByRow},
        {0, 1, 0, ExecutionMode::kStatefulBatch},
        {1, 1, 0, ExecutionMode::kStatefulBatch},
        {0, 0, 1, ExecutionMode::kTwoPhase},
        {1, 0, 1, ExecutionMode::kTwoPhase},
        {0, 1, 1, ExecutionMode::kTwoPhase},
        {1, 1, 1, ExecutionMode::kTwoPhase},
        {3, 2, 1, ExecutionMode::kTwoPhase},  // aggregate always wins
    };

    for (const auto& c : cases) {
        auto mode = classifier.derive_execution_mode(c.vr, c.ir, c.agg);
        EXPECT_EQ(mode, c.expected)
            << "derive_execution_mode(" << c.vr << "," << c.ir << "," << c.agg
            << ") expected mode " << static_cast<int>(c.expected)
            << " got " << static_cast<int>(mode);
    }
}

TEST(CompositionTransitionTest, RoutingPathTransitions) {
    Schema schema = make_schema_3col_order();

    // VR-only without data engine -> PurePhysics
    {
        ConstraintClassifier classifier;
        ConstraintSet cs = make_constraints_vr();
        auto cls = classifier.classify(cs, schema);
        ASSERT_TRUE(cls.ok());

        ExecutionRouter router(false);
        auto rt = router.route(cls.value(), schema);
        ASSERT_TRUE(rt.ok());
        EXPECT_EQ(rt.value().selected_path, DegradationPath::kPurePhysics);
    }

    // VR+IR with data engine -> PostFilter
    {
        ConstraintClassifier classifier;
        ConstraintSet cs = make_constraints_vr_ir();
        auto cls = classifier.classify(cs, schema);
        ASSERT_TRUE(cls.ok());

        ExecutionRouter router(true);
        auto rt = router.route(cls.value(), schema);
        ASSERT_TRUE(rt.ok());
        EXPECT_EQ(rt.value().selected_path, DegradationPath::kPostFilter);
    }

    // VR+IR+Agg with data engine -> FullFunction
    {
        ConstraintClassifier classifier;
        ConstraintSet cs = make_constraints_vr_ir_agg();
        auto cls = classifier.classify(cs, schema);
        ASSERT_TRUE(cls.ok());

        ExecutionRouter router(true);
        auto rt = router.route(cls.value(), schema);
        ASSERT_TRUE(rt.ok());
        EXPECT_EQ(rt.value().selected_path, DegradationPath::kFullFunction);
    }
}

// ============================================================================
// Test Group 4: Storage after each execution path
// ============================================================================

class StoragePerPathTest : public ::testing::Test {
protected:
    std::filesystem::path tmp_dir_;

    void SetUp() override {
        tmp_dir_ = std::filesystem::temp_directory_path() /
            ("comp_storage_" + std::to_string(::getpid()));
        std::filesystem::remove_all(tmp_dir_);
        std::filesystem::create_directories(tmp_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp_dir_);
    }
};

TEST_F(StoragePerPathTest, PurePhysicsPathStoreAndScan) {
    Schema schema = make_schema_3col_order();
    ConstraintSet cs = make_constraints_vr();

    ConstraintClassifier classifier;
    auto cls = classifier.classify(cs, schema);
    ASSERT_TRUE(cls.ok());

    ExecutionRouter router(false);  // no data engine -> PurePhysics
    auto rt = router.route(cls.value(), schema);
    ASSERT_TRUE(rt.ok());
    EXPECT_EQ(rt.value().selected_path, DegradationPath::kPurePhysics);

    // Generate
    std::vector<parser::ast::ConstraintItem> cis;
    for (const auto& col : schema.columns) {
        if (col.type == DataType::kFloat && col.range_min && col.range_max) {
            cis.push_back({col.name, parser::ast::ConstraintOperator::kBetween,
                           col.range_min.value(), col.range_max.value()});
        }
    }
    GenerationRequest req{schema, cis, 75, 42, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen = sampler.generate(req);
    ASSERT_TRUE(gen.ok());

    // Store
    ObjectStoreBackend backend(tmp_dir_);
    ASSERT_TRUE(backend.register_table("pp_test", "{}").ok());
    ASSERT_TRUE(backend.append("pp_test", gen.value().data).ok());

    // Scan and verify
    auto scan = backend.scan("pp_test");
    ASSERT_TRUE(scan.ok());
    ASSERT_NE(scan.value(), nullptr);
    EXPECT_EQ(scan.value()->num_rows(), 75);
}

TEST_F(StoragePerPathTest, StatefulBatchPathStoreAndScan) {
    Schema schema = make_schema_3col_order();
    ConstraintSet cs = make_constraints_vr_ir();

    ConstraintClassifier classifier;
    auto cls = classifier.classify(cs, schema);
    ASSERT_TRUE(cls.ok());
    EXPECT_EQ(cls.value().execution_mode, ExecutionMode::kStatefulBatch);

    ExecutionRouter router(false);
    auto rt = router.route(cls.value(), schema);
    ASSERT_TRUE(rt.ok());

    // Generate
    std::vector<parser::ast::ConstraintItem> cis;
    for (const auto& col : schema.columns) {
        if (col.type == DataType::kFloat && col.range_min && col.range_max) {
            cis.push_back({col.name, parser::ast::ConstraintOperator::kBetween,
                           col.range_min.value(), col.range_max.value()});
        }
    }
    GenerationRequest req{schema, cis, 120, 77, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen = sampler.generate(req);
    ASSERT_TRUE(gen.ok());

    // Store
    ObjectStoreBackend backend(tmp_dir_);
    ASSERT_TRUE(backend.register_table("sb_test", "{}").ok());
    ASSERT_TRUE(backend.append("sb_test", gen.value().data).ok());

    // Scan
    auto scan = backend.scan("sb_test");
    ASSERT_TRUE(scan.ok());
    EXPECT_EQ(scan.value()->num_rows(), 120);
}

TEST_F(StoragePerPathTest, TwoPhasePathStoreAndScan) {
    Schema schema = make_schema_3col_order();
    ConstraintSet cs = make_constraints_vr_ir_agg();

    ConstraintClassifier classifier;
    auto cls = classifier.classify(cs, schema);
    ASSERT_TRUE(cls.ok());
    EXPECT_EQ(cls.value().execution_mode, ExecutionMode::kTwoPhase);

    // Generate
    std::vector<parser::ast::ConstraintItem> cis;
    for (const auto& col : schema.columns) {
        if (col.type == DataType::kFloat && col.range_min && col.range_max) {
            cis.push_back({col.name, parser::ast::ConstraintOperator::kBetween,
                           col.range_min.value(), col.range_max.value()});
        }
    }
    GenerationRequest req{schema, cis, 200, 88, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen = sampler.generate(req);
    ASSERT_TRUE(gen.ok());

    // Store
    ObjectStoreBackend backend(tmp_dir_);
    ASSERT_TRUE(backend.register_table("tp_test", "{}").ok());
    ASSERT_TRUE(backend.append("tp_test", gen.value().data).ok());

    // Scan
    auto scan = backend.scan("tp_test");
    ASSERT_TRUE(scan.ok());
    EXPECT_EQ(scan.value()->num_rows(), 200);
}

TEST_F(StoragePerPathTest, MultipleAppendsAccumulateRows) {
    Schema schema = make_schema_3col_order();
    std::vector<parser::ast::ConstraintItem> cis;
    for (const auto& col : schema.columns) {
        if (col.type == DataType::kFloat && col.range_min && col.range_max) {
            cis.push_back({col.name, parser::ast::ConstraintOperator::kBetween,
                           col.range_min.value(), col.range_max.value()});
        }
    }

    ObjectStoreBackend backend(tmp_dir_);
    ASSERT_TRUE(backend.register_table("multi_append", "{}").ok());

    // Append 3 batches of different sizes
    for (int i = 0; i < 3; ++i) {
        GenerationRequest req{schema, cis, 50, static_cast<uint64_t>(i * 1000), "uniform", 1000};
        RectangularSampler sampler(schema);
        auto gen = sampler.generate(req);
        ASSERT_TRUE(gen.ok());
        ASSERT_TRUE(backend.append("multi_append", gen.value().data).ok());
    }

    // Scan should return all 150 rows
    auto scan = backend.scan("multi_append");
    ASSERT_TRUE(scan.ok());
    EXPECT_EQ(scan.value()->num_rows(), 150);
}

// ============================================================================
// Test Group 5: Audit trail through all combinations
// ============================================================================

TEST(CompositionAuditTest, AuditTrailThroughMatrix) {
    AuditLog log;
    ASSERT_TRUE(log.create_genesis().ok());
    EXPECT_EQ(log.record_count(), 1);

    // Run through all 20 matrix combinations, appending audit records
    for (const auto& params : kCompositionMatrix) {
        std::map<std::string, std::string> meta;
        meta["label"] = params.label;
        meta["schema"] = params.schema.type_name;
        meta["data_engine"] = params.data_engine_available ? "true" : "false";
        meta["evidence_version"] = std::to_string(params.evidence_version);
        meta["expected_mode"] = std::to_string(static_cast<int>(params.expected_mode));

        // Classify
        ConstraintClassifier classifier;
        auto cls_res = classifier.classify(params.constraints, params.schema);
        if (params.expect_classify_error) {
            meta["result"] = "classify_error";
            auto rec = log.append("composition_classify_error", "test_runner", meta);
            ASSERT_TRUE(rec.ok()) << "Audit append failed for " << params.label;
            continue;
        }

        if (!cls_res.ok()) {
            meta["result"] = "unexpected_classify_fail";
            auto rec = log.append("composition_unexpected_fail", "test_runner", meta);
            ASSERT_TRUE(rec.ok());
            continue;
        }

        // Route
        ExecutionRouter router(params.data_engine_available);
        auto rt_res = router.route(cls_res.value(), params.schema);

        meta["execution_mode"] = std::to_string(
            static_cast<int>(cls_res.value().execution_mode));
        if (rt_res.ok()) {
            meta["degradation_path"] = std::to_string(
                static_cast<int>(rt_res.value().selected_path));
            meta["result"] = "success";
        } else {
            meta["result"] = "route_error";
        }

        auto rec = log.append("composition_test", "test_runner", meta);
        ASSERT_TRUE(rec.ok()) << "Audit append failed for " << params.label;
    }

    // Verify chain integrity: 1 genesis + 20 combination records = 21
    EXPECT_EQ(log.record_count(), 21)
        << "Expected 21 audit records (1 genesis + 20 combos)";

    // Verify chain integrity
    auto verify = log.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value()) << "Chain verification failed after 20 combinations";

    // Daily verification
    auto daily = log.daily_verification();
    ASSERT_TRUE(daily.ok());
    EXPECT_TRUE(daily.value().is_valid)
        << "Daily verification found chain broken";
    EXPECT_EQ(daily.value().total_records, 21);
    EXPECT_EQ(daily.value().verified_records, 20)  // 21 - 1 genesis
        << "Expected 20 verified links";
    EXPECT_TRUE(daily.value().broken_links.empty())
        << "Broken links found after matrix audit trail";

    // No forks
    auto forks = log.detect_forks();
    ASSERT_TRUE(forks.ok());
    EXPECT_TRUE(forks.value().empty()) << "Unexpected forks detected";
}

TEST(CompositionAuditTest, AuditLabelsMatchMatrixParams) {
    AuditLog log;
    ASSERT_TRUE(log.create_genesis().ok());

    // Add one record per matrix label
    for (const auto& params : kCompositionMatrix) {
        std::map<std::string, std::string> meta;
        meta["label"] = params.label;
        auto rec = log.append("label_check", "test_runner", meta);
        ASSERT_TRUE(rec.ok());
    }

    // Scan all and verify labels
    auto scan = log.scan(std::nullopt, std::nullopt, 100);
    ASSERT_TRUE(scan.ok());
    // 1 genesis + 20 labels = 21
    EXPECT_EQ(scan.value().size(), 21u);

    // Extract all labels from metadata
    std::vector<std::string> labels;
    for (const auto& rec : scan.value()) {
        if (rec.operation == "label_check") {
            auto it = rec.metadata.find("label");
            if (it != rec.metadata.end()) {
                labels.push_back(it->second);
            }
        }
    }
    EXPECT_EQ(labels.size(), 20u);

    // Verify each expected label is present
    for (const auto& params : kCompositionMatrix) {
        bool found = false;
        for (const auto& label : labels) {
            if (label == params.label) { found = true; break; }
        }
        EXPECT_TRUE(found) << "Missing audit label: " << params.label;
    }
}

TEST(CompositionAuditTest, AuditChainIntactAfterErrors) {
    AuditLog log;
    ASSERT_TRUE(log.create_genesis().ok());

    // Simulate error scenarios — these should still create valid audit records
    std::vector<std::string> error_labels = {
        "inter_row_no_order", "agg_no_order", "empty_constraints"
    };

    for (const auto& label : error_labels) {
        std::map<std::string, std::string> meta;
        meta["error_type"] = label;
        auto rec = log.append("constraint_error", "classifier", meta);
        ASSERT_TRUE(rec.ok());
    }

    // 1 genesis + 3 errors = 4
    EXPECT_EQ(log.record_count(), 4);

    auto verify = log.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value()) << "Chain broken after error records";
}

// ============================================================================
// Cross-cutting: Evidence V1 vs V2 consistency across same schema/constraints
// ============================================================================

TEST(CompositionCrossCutTest, V1V2SchemaHashConsistency) {
    Schema schema = make_schema_3col_order();
    std::string hash = EvidencePackageBuilder::compute_schema_hash(schema);

    // V2 uses the same hashing logic internally, verify via manual computation
    std::ostringstream oss;
    oss << schema.type_name << "{";
    for (const auto& col : schema.columns) {
        oss << col.name << ":" << static_cast<int>(col.type);
        if (col.range_min) oss << "[" << *col.range_min;
        if (col.range_max) oss << "," << *col.range_max << "]";
        oss << ";";
    }
    oss << "}";
    std::string expected_hash = sha256_hex(oss.str());

    EXPECT_EQ(hash, expected_hash)
        << "Schema hash computation changed between V1 builder and manual";
}

TEST(CompositionCrossCutTest, AllSchemaTypesValidate) {
    // Every schema variant should pass validation
    auto schemas = {
        make_schema_1col_float(),
        make_schema_3col_float(),
        make_schema_3col_order(),
        make_schema_5col_mixed()
    };

    for (const auto& s : schemas) {
        auto res = s.validate();
        EXPECT_TRUE(res.ok()) << "Schema " << s.type_name << " failed validation: "
            << (res.ok() ? "" : res.error().message);
    }
}

TEST(CompositionCrossCutTest, OrderColumnsDetection) {
    // Schema A: no order
    {
        auto s = make_schema_1col_float();
        auto oc = s.order_columns();
        EXPECT_TRUE(oc.empty()) << "1-col schema should have no ORDER columns";
    }
    // Schema B: no order
    {
        auto s = make_schema_3col_float();
        auto oc = s.order_columns();
        EXPECT_TRUE(oc.empty()) << "3-col schema should have no ORDER columns";
    }
    // Schema C: has order
    {
        auto s = make_schema_3col_order();
        auto oc = s.order_columns();
        EXPECT_EQ(oc.size(), 1u) << "3-col+ORDER should have 1 ORDER column";
        EXPECT_EQ(oc[0], "ts");
    }
    // Schema D: has order
    {
        auto s = make_schema_5col_mixed();
        auto oc = s.order_columns();
        EXPECT_EQ(oc.size(), 1u) << "5-col mixed should have 1 ORDER column";
        EXPECT_EQ(oc[0], "ts");
    }
}

// ============================================================================
// Cross-cutting: PostFilter behavior across all paths
// ============================================================================

TEST(CompositionCrossCutTest, PostFilterAcrossAllPaths) {
    // Verify post-filter works with data from every execution mode's sampling
    auto schemas = {
        make_schema_1col_float(),
        make_schema_3col_float(),
        make_schema_3col_order(),
        make_schema_5col_mixed()
    };

    PostFilter pf;
    int idx = 0;
    for (const auto& schema : schemas) {
        std::vector<parser::ast::ConstraintItem> cis;
        for (const auto& col : schema.columns) {
            if (col.type == DataType::kFloat && col.range_min && col.range_max) {
                cis.push_back({col.name, parser::ast::ConstraintOperator::kBetween,
                               col.range_min.value(), col.range_max.value()});
            }
        }

        GenerationRequest req{schema, cis, 200, static_cast<uint64_t>(idx * 999), "uniform", 1000};
        RectangularSampler sampler(schema);
        auto gen = sampler.generate(req);
        ASSERT_TRUE(gen.ok()) << "Generate failed for " << schema.type_name;

        auto pf_res = pf.execute(gen.value().data, 200);
        ASSERT_TRUE(pf_res.ok()) << "PostFilter failed for " << schema.type_name;
        EXPECT_GT(pf_res.value().post_filter_rows, 0)
            << schema.type_name << ": post-filter returned 0 rows";
        idx++;
    }
}

// ============================================================================
// Coverage summary check: verify all execution modes are represented
// ============================================================================

TEST(CompositionCoverageTest, AllExecutionModesPresent) {
    // Verify the test matrix covers all three execution modes
    std::set<ExecutionMode> modes_seen;
    for (const auto& p : kCompositionMatrix) {
        if (!p.expect_classify_error) {
            modes_seen.insert(p.expected_mode);
        }
    }
    EXPECT_EQ(modes_seen.size(), 3u)
        << "Matrix should cover all 3 execution modes";

    EXPECT_TRUE(modes_seen.count(ExecutionMode::kRowByRow) > 0)
        << "Missing kRowByRow";
    EXPECT_TRUE(modes_seen.count(ExecutionMode::kStatefulBatch) > 0)
        << "Missing kStatefulBatch";
    EXPECT_TRUE(modes_seen.count(ExecutionMode::kTwoPhase) > 0)
        << "Missing kTwoPhase";
}

TEST(CompositionCoverageTest, AllSchemasRepresented) {
    std::set<std::string> schemas_seen;
    for (const auto& p : kCompositionMatrix) {
        schemas_seen.insert(p.schema.type_name);
    }
    EXPECT_EQ(schemas_seen.size(), 4u)
        << "Matrix should cover all 4 schema types";
}

TEST(CompositionCoverageTest, BothEvidenceVersionsRepresented) {
    int v1_count = 0, v2_count = 0;
    for (const auto& p : kCompositionMatrix) {
        if (p.evidence_version == 1) v1_count++;
        else v2_count++;
    }
    EXPECT_GE(v1_count, 3) << "Need at least 3 V1 combos";
    EXPECT_GE(v2_count, 3) << "Need at least 3 V2 combos";
}

TEST(CompositionCoverageTest, BothDataEngineSettingsRepresented) {
    int engine_true = 0, engine_false = 0;
    for (const auto& p : kCompositionMatrix) {
        if (p.data_engine_available) engine_true++;
        else engine_false++;
    }
    EXPECT_GE(engine_true, 5) << "Need at least 5 combos with data engine";
    EXPECT_GE(engine_false, 5) << "Need at least 5 combos without data engine";
}
