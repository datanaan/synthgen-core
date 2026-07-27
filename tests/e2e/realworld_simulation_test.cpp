// Real-world Industrial Simulation Tests
// 5 end-to-end scenarios simulating how a factory would use SynthGen Core
// to generate synthetic sensor data for training ML models.
//
// Each scenario generates data, stores it, reads it back, and verifies
// that the ACTUAL DATA VALUES make physical sense -- not just "didn't crash."

#include <gtest/gtest.h>

#include "api/service.h"
#include "api/request.h"
#include "api/response.h"
#include "schema/schema.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "engine/postfilter/post_filter.h"
#include "engine/evidence/evidence_package_builder.h"
#include "engine/evidence/evidence_package_json.h"
#include "engine/evidence/tail_report.h"
#include "storage/object_store_backend.h"
#include "storage/audit/audit_log.h"
#include "common/result.h"
#include "common/types.h"
#include "common/hash.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/type.h>
#include <cmath>
#include <filesystem>
#include <map>
#include <numeric>
#include <string>
#include <vector>
#include <algorithm>

using namespace synthgen;

// Type aliases to avoid arrow::Schema ambiguity
using Schema = schema::Schema;
using ColDef = ColumnDef;
using GenRequest = engine::physics::GenerationRequest;
using Sampler = engine::physics::RectangularSampler;
using Validator = engine::constraint::ValueRangeValidator;
using InterRowEngine = engine::constraint::InterRowEngine;
using InterRowDef = engine::constraint::InterRowConstraintDef;
using InterRowState = engine::constraint::InterRowState;
using AggEngine = engine::constraint::AggregateEngine;
using AggDef = engine::constraint::AggregateConstraintDef;
using AggFunc = engine::constraint::AggregateFunction;
using WinType = engine::constraint::WindowType;
using StoreBackend = storage::ObjectStoreBackend;
using AuditLog = storage::audit::AuditLog;
using ConstraintItem = parser::ast::ConstraintItem;
using ConstraintOp = parser::ast::ConstraintOperator;

// ============================================================================
// Helpers
// ============================================================================

static std::vector<double> extract_doubles(
    const std::shared_ptr<arrow::Table>& table, const std::string& col_name) {
    std::vector<double> values;
    int idx = table->schema()->GetFieldIndex(col_name);
    if (idx < 0) return values;
    auto chunked = table->column(idx);
    for (int c = 0; c < chunked->num_chunks(); ++c) {
        auto arr = std::static_pointer_cast<arrow::DoubleArray>(chunked->chunk(c));
        if (arr) {
            for (int64_t i = 0; i < arr->length(); ++i) {
                values.push_back(arr->Value(i));
            }
        }
    }
    return values;
}

static std::vector<int64_t> extract_int64s(
    const std::shared_ptr<arrow::Table>& table, const std::string& col_name) {
    std::vector<int64_t> values;
    int idx = table->schema()->GetFieldIndex(col_name);
    if (idx < 0) return values;
    auto chunked = table->column(idx);
    for (int c = 0; c < chunked->num_chunks(); ++c) {
        auto arr = std::static_pointer_cast<arrow::Int64Array>(chunked->chunk(c));
        if (arr) {
            for (int64_t i = 0; i < arr->length(); ++i) {
                values.push_back(arr->Value(i));
            }
        }
    }
    return values;
}

static std::vector<std::string> extract_strings(
    const std::shared_ptr<arrow::Table>& table, const std::string& col_name) {
    std::vector<std::string> values;
    int idx = table->schema()->GetFieldIndex(col_name);
    if (idx < 0) return values;
    auto chunked = table->column(idx);
    for (int c = 0; c < chunked->num_chunks(); ++c) {
        auto arr = std::static_pointer_cast<arrow::StringArray>(chunked->chunk(c));
        if (arr) {
            for (int64_t i = 0; i < arr->length(); ++i) {
                values.push_back(arr->GetString(i));
            }
        }
    }
    return values;
}

static double compute_mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

static double compute_stddev(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    double mean = compute_mean(v);
    double sq_sum = 0.0;
    for (auto x : v) { double d = x - mean; sq_sum += d * d; }
    return std::sqrt(sq_sum / static_cast<double>(v.size()));
}

static double compute_min(const std::vector<double>& v) {
    return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end());
}

static ColDef make_float_col(const std::string& name, double lo, double hi,
                              bool not_null = false, bool is_order = false) {
    ColDef c;
    c.name = name; c.type = DataType::kFloat;
    c.range_min = lo; c.range_max = hi;
    c.not_null = not_null; c.is_order = is_order;
    return c;
}

static ColDef make_int_col(const std::string& name, double lo, double hi) {
    ColDef c;
    c.name = name; c.type = DataType::kInt;
    c.range_min = lo; c.range_max = hi;
    return c;
}

static ColDef make_datetime_col(const std::string& name, bool is_order = false) {
    ColDef c;
    c.name = name; c.type = DataType::kDatetime;
    c.is_order = is_order; c.not_null = true;
    return c;
}

static ColDef make_enum_col(const std::string& name,
                             std::vector<std::string> values) {
    ColDef c;
    c.name = name; c.type = DataType::kEnum;
    c.enum_values = std::move(values);
    return c;
}

// ============================================================================
// Scenario 1: Weather Station Network
// ============================================================================
// A meteorological service generates 5000 synthetic weather readings for ML
// model training. The data must satisfy operational safety constraints.
// ============================================================================
TEST(RealWorldSimulation, WeatherStationNetwork) {
    Schema schema;
    schema.type_name = "weather_station";
    schema.columns = {
        make_float_col("temperature", -40.0, 50.0),
        make_float_col("humidity", 0.0, 100.0),
        make_float_col("wind_speed", 0.0, 200.0),
        make_float_col("pressure", 950.0, 1050.0),
        make_enum_col("status", {"normal", "high_wind", "extreme"}),
    };
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"temperature", ConstraintOp::kBetween, -20.0, 40.0},
        {"humidity", ConstraintOp::kBetween, 20.0, 95.0},
        {"wind_speed", ConstraintOp::kLessThan, 0.0, 100.0},
    };

    GenRequest gen_req{schema, constraints, 5000, 42, "uniform", 5000};
    Sampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    auto table = gen_result.value().data;
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->num_rows(), 5000);

    // Store to disk
    auto tmp_dir = std::filesystem::temp_directory_path() / "synthgen_weather_test";
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    StoreBackend backend(tmp_dir);
    ASSERT_TRUE(backend.register_table("weather_station", "{}").ok());
    ASSERT_TRUE(backend.append("weather_station", table).ok());

    // Scan back
    auto scan_result = backend.scan("weather_station");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    auto read_table = scan_result.value();
    ASSERT_NE(read_table, nullptr);
    EXPECT_EQ(read_table->num_rows(), 5000);

    // Temperature: all values in [-20, 40]
    auto temps = extract_doubles(read_table, "temperature");
    ASSERT_EQ(temps.size(), 5000u);
    for (size_t i = 0; i < temps.size(); ++i) {
        EXPECT_GE(temps[i], -20.0) << "row " << i;
        EXPECT_LE(temps[i], 40.0) << "row " << i;
    }
    double temp_mean = compute_mean(temps);
    EXPECT_GT(temp_mean, -10.0) << temp_mean;
    EXPECT_LT(temp_mean, 30.0) << temp_mean;
    EXPECT_GT(compute_stddev(temps), 1.0) << "data degenerate?";

    // Humidity: all values in [20, 95]
    auto humids = extract_doubles(read_table, "humidity");
    ASSERT_EQ(humids.size(), 5000u);
    for (size_t i = 0; i < humids.size(); ++i) {
        EXPECT_GE(humids[i], 20.0) << "row " << i;
        EXPECT_LE(humids[i], 95.0) << "row " << i;
    }
    double humid_mean = compute_mean(humids);
    EXPECT_GT(humid_mean, 30.0);
    EXPECT_LT(humid_mean, 85.0);

    // Wind speed: all values < 100
    auto winds = extract_doubles(read_table, "wind_speed");
    ASSERT_EQ(winds.size(), 5000u);
    for (size_t i = 0; i < winds.size(); ++i) {
        EXPECT_LT(winds[i], 100.0) << "row " << i;
    }
    EXPECT_GE(compute_min(winds), 0.0);

    // Pressure: [950, 1050]
    auto pressures = extract_doubles(read_table, "pressure");
    ASSERT_EQ(pressures.size(), 5000u);
    for (size_t i = 0; i < pressures.size(); ++i) {
        EXPECT_GE(pressures[i], 950.0) << "row " << i;
        EXPECT_LE(pressures[i], 1050.0) << "row " << i;
    }

    // Status: only valid enum values
    auto statuses = extract_strings(read_table, "status");
    ASSERT_EQ(statuses.size(), 5000u);
    for (size_t i = 0; i < statuses.size(); ++i) {
        EXPECT_TRUE(statuses[i] == "normal" ||
                    statuses[i] == "high_wind" ||
                    statuses[i] == "extreme")
            << "row " << i << ": " << statuses[i];
    }

    // No NULL values anywhere
    for (int col = 0; col < read_table->num_columns(); ++col) {
        auto chunked = read_table->column(col);
        for (int c = 0; c < chunked->num_chunks(); ++c) {
            for (int64_t r = 0; r < chunked->chunk(c)->length(); ++r) {
                EXPECT_FALSE(chunked->chunk(c)->IsNull(r))
                    << "NULL in col " << col << " row " << r;
            }
        }
    }

    std::filesystem::remove_all(tmp_dir);
}

// ============================================================================
// Scenario 2: Industrial Motor Monitoring
// ============================================================================
TEST(RealWorldSimulation, IndustrialMotorMonitoring) {
    Schema schema;
    schema.type_name = "motor_sensor";
    schema.columns = {
        make_datetime_col("timestamp", true /*is_order*/),
        make_float_col("rpm", 0.0, 10000.0),
        make_float_col("vibration", 0.0, 50.0),
        make_float_col("temperature", -20.0, 200.0),
        make_float_col("current", 0.0, 500.0),
    };
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints;
    GenRequest gen_req{schema, constraints, 2000, 777, "uniform", 2000};
    Sampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    auto table = gen_result.value().data;
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->num_rows(), 2000);

    // Verify basic schema ranges
    auto rpms = extract_doubles(table, "rpm");
    EXPECT_EQ(rpms.size(), 2000u);
    for (auto v : rpms) { EXPECT_GE(v, 0.0); EXPECT_LE(v, 10000.0); }

    auto vib_vals = extract_doubles(table, "vibration");
    EXPECT_EQ(vib_vals.size(), 2000u);
    for (auto v : vib_vals) { EXPECT_GE(v, 0.0); EXPECT_LE(v, 50.0); }

    // Inter-row constraint: vibration delta < 5.0
    std::vector<InterRowDef> ir_constraints;
    InterRowDef vib_delta;
    vib_delta.column_name = "vibration";
    vib_delta.order_column = "timestamp";
    vib_delta.type = InterRowDef::Type::kDeltaMax;
    vib_delta.delta_max = 5.0;
    ir_constraints.push_back(vib_delta);

    InterRowEngine ir_engine(schema, ir_constraints);
    std::vector<InterRowState> initial_states;
    auto ir_result = ir_engine.execute_batch(table, initial_states);
    ASSERT_TRUE(ir_result.ok()) << ir_result.error().message;

    auto& filtered = ir_result.value();
    EXPECT_GT(filtered.rows_passed, 0);
    EXPECT_LT(filtered.rows_passed, 2001);

    // In the filtered output, verify vibration changes gradually
    if (filtered.filtered_batch && filtered.filtered_batch->num_rows() > 1) {
        auto fv = extract_doubles(filtered.filtered_batch, "vibration");
        ASSERT_GE(fv.size(), 2u);
        for (size_t i = 1; i < fv.size(); ++i) {
            double delta = std::abs(fv[i] - fv[i - 1]);
            EXPECT_LT(delta, 5.0)
                << "rows " << (i-1) << "->" << i
                << ": " << fv[i-1] << " -> " << fv[i];
        }
        // Filtered data still in valid schema range
        for (auto v : fv) { EXPECT_GE(v, 0.0); EXPECT_LE(v, 50.0); }
    }

    // Verify outgoing states are populated
    for (const auto& state : filtered.outgoing_states) {
        EXPECT_EQ(state.column_name, "vibration");
    }
}

// ============================================================================
// Scenario 3: Power Grid Readings
// ============================================================================
TEST(RealWorldSimulation, PowerGridReadings) {
    Schema schema;
    schema.type_name = "power_grid";
    schema.columns = {
        make_datetime_col("timestamp", true),
        make_float_col("voltage", 190.0, 250.0),
        make_float_col("frequency", 49.0, 51.0),
        make_float_col("load_mw", 0.0, 5000.0),
    };
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints;
    GenRequest gen_req{schema, constraints, 24, 2024, "uniform", 1000};
    Sampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    auto table = gen_result.value().data;
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->num_rows(), 24);

    // Verify column ranges
    auto voltages = extract_doubles(table, "voltage");
    ASSERT_EQ(voltages.size(), 24u);
    for (size_t i = 0; i < voltages.size(); ++i) {
        EXPECT_GE(voltages[i], 190.0) << "row " << i;
        EXPECT_LE(voltages[i], 250.0) << "row " << i;
    }

    auto frequencies = extract_doubles(table, "frequency");
    ASSERT_EQ(frequencies.size(), 24u);
    for (size_t i = 0; i < frequencies.size(); ++i) {
        EXPECT_GE(frequencies[i], 49.0) << "row " << i;
        EXPECT_LE(frequencies[i], 51.0) << "row " << i;
    }

    auto loads = extract_doubles(table, "load_mw");
    ASSERT_EQ(loads.size(), 24u);
    for (size_t i = 0; i < loads.size(); ++i) {
        EXPECT_GE(loads[i], 0.0) << "row " << i;
        EXPECT_LE(loads[i], 5000.0) << "row " << i;
    }

    // Average voltage near midpoint (220V)
    double avg_voltage = compute_mean(voltages);
    EXPECT_GT(avg_voltage, 200.0) << avg_voltage;
    EXPECT_LT(avg_voltage, 240.0) << avg_voltage;

    // Run AggregateEngine
    std::vector<AggDef> agg_constraints;
    AggDef avg_check;
    avg_check.constraint_name = "avg_voltage_check";
    avg_check.column_name = "voltage";
    avg_check.function = AggFunc::kAvg;
    avg_check.window_type = WinType::kInterval;
    avg_check.window_interval_us = 3600000000LL;
    avg_check.min_val = 215.0;
    avg_check.max_val = 225.0;
    agg_constraints.push_back(avg_check);

    AggEngine agg_engine(schema, agg_constraints);
    std::vector<InterRowState> empty_states;
    auto agg_result = agg_engine.execute(table, empty_states);
    ASSERT_TRUE(agg_result.ok()) << agg_result.error().message;

    auto& phase_two = agg_result.value().phase_two;
    EXPECT_GT(phase_two.total_windows, 0);

    for (const auto& window : phase_two.windows) {
        auto agg_val_result = agg_engine.compute_aggregate(
            table, window, avg_check);
        ASSERT_TRUE(agg_val_result.ok());
        double agg_val = agg_val_result.value();
        EXPECT_GE(agg_val, 190.0);
        EXPECT_LE(agg_val, 250.0);
    }

    // Statistical sanity
    EXPECT_GT(compute_stddev(voltages), 5.0) << "voltage too uniform";
}

// ============================================================================
// Scenario 4: Multi-table Factory
// ============================================================================
TEST(RealWorldSimulation, MultiTableFactory) {
    auto tmp_dir = std::filesystem::temp_directory_path() / "synthgen_factory_test";
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    StoreBackend backend(tmp_dir);

    // --- Table 1: temperature_sensor ---
    Schema temp_schema;
    temp_schema.type_name = "temperature_sensor";
    temp_schema.columns = {
        make_datetime_col("timestamp", true),
        make_float_col("celsius", -50.0, 150.0),
        make_int_col("sensor_id", 1.0, 100.0),
    };
    ASSERT_TRUE(temp_schema.validate().ok());

    // --- Table 2: pressure_sensor ---
    Schema press_schema;
    press_schema.type_name = "pressure_sensor";
    press_schema.columns = {
        make_datetime_col("timestamp", true),
        make_float_col("pascal", 90000.0, 110000.0),
        make_int_col("sensor_id", 1.0, 50.0),
    };
    ASSERT_TRUE(press_schema.validate().ok());

    // --- Table 3: flow_sensor ---
    Schema flow_schema;
    flow_schema.type_name = "flow_sensor";
    flow_schema.columns = {
        make_datetime_col("timestamp", true),
        make_float_col("liters_per_sec", 0.0, 1000.0),
        make_int_col("sensor_id", 1.0, 30.0),
    };
    ASSERT_TRUE(flow_schema.validate().ok());

    // Generate
    std::vector<ConstraintItem> no_constraints;

    Sampler temp_sampler(temp_schema);
    auto temp_gen = temp_sampler.generate(
        GenRequest{temp_schema, no_constraints, 300, 100, "uniform", 1000});
    ASSERT_TRUE(temp_gen.ok()) << temp_gen.error().message;

    Sampler press_sampler(press_schema);
    auto press_gen = press_sampler.generate(
        GenRequest{press_schema, no_constraints, 200, 200, "uniform", 1000});
    ASSERT_TRUE(press_gen.ok()) << press_gen.error().message;

    Sampler flow_sampler(flow_schema);
    auto flow_gen = flow_sampler.generate(
        GenRequest{flow_schema, no_constraints, 150, 300, "uniform", 1000});
    ASSERT_TRUE(flow_gen.ok()) << flow_gen.error().message;

    // Store each
    ASSERT_TRUE(backend.register_table("temperature_sensor", "{}").ok());
    ASSERT_TRUE(backend.append("temperature_sensor", temp_gen.value().data).ok());

    ASSERT_TRUE(backend.register_table("pressure_sensor", "{}").ok());
    ASSERT_TRUE(backend.append("pressure_sensor", press_gen.value().data).ok());

    ASSERT_TRUE(backend.register_table("flow_sensor", "{}").ok());
    ASSERT_TRUE(backend.append("flow_sensor", flow_gen.value().data).ok());

    // Scan back
    auto temp_scan = backend.scan("temperature_sensor");
    ASSERT_TRUE(temp_scan.ok()) << temp_scan.error().message;
    EXPECT_EQ(temp_scan.value()->num_rows(), 300);

    auto press_scan = backend.scan("pressure_sensor");
    ASSERT_TRUE(press_scan.ok()) << press_scan.error().message;
    EXPECT_EQ(press_scan.value()->num_rows(), 200);

    auto flow_scan = backend.scan("flow_sensor");
    ASSERT_TRUE(flow_scan.ok()) << flow_scan.error().message;
    EXPECT_EQ(flow_scan.value()->num_rows(), 150);

    // Tables are independent
    auto temp_cols = temp_scan.value()->schema()->field_names();
    EXPECT_EQ(temp_cols.size(), 3u);
    EXPECT_NE(temp_cols.end(), std::find(temp_cols.begin(), temp_cols.end(), "celsius"));
    EXPECT_EQ(temp_cols.end(), std::find(temp_cols.begin(), temp_cols.end(), "pascal"))
        << "temp table should not have pascal";

    auto press_cols = press_scan.value()->schema()->field_names();
    EXPECT_EQ(press_cols.end(), std::find(press_cols.begin(), press_cols.end(), "celsius"))
        << "pressure table should not have celsius";
    EXPECT_NE(press_cols.end(), std::find(press_cols.begin(), press_cols.end(), "pascal"));

    // Self-consistency
    auto celsius = extract_doubles(temp_scan.value(), "celsius");
    ASSERT_EQ(celsius.size(), 300u);
    for (auto v : celsius) { EXPECT_GE(v, -50.0); EXPECT_LE(v, 150.0); }
    EXPECT_GT(compute_stddev(celsius), 1.0);

    auto pascal = extract_doubles(press_scan.value(), "pascal");
    ASSERT_EQ(pascal.size(), 200u);
    for (auto v : pascal) { EXPECT_GE(v, 90000.0); EXPECT_LE(v, 110000.0); }
    double press_mean = compute_mean(pascal);
    EXPECT_GT(press_mean, 92000.0);
    EXPECT_LT(press_mean, 108000.0);

    auto flow = extract_doubles(flow_scan.value(), "liters_per_sec");
    ASSERT_EQ(flow.size(), 150u);
    for (auto v : flow) { EXPECT_GE(v, 0.0); EXPECT_LE(v, 1000.0); }
    EXPECT_GT(compute_stddev(flow), 1.0);

    // Sensor IDs in range
    auto temp_ids = extract_int64s(temp_scan.value(), "sensor_id");
    for (auto id : temp_ids) { EXPECT_GE(id, 1); EXPECT_LE(id, 100); }
    auto press_ids = extract_int64s(press_scan.value(), "sensor_id");
    for (auto id : press_ids) { EXPECT_GE(id, 1); EXPECT_LE(id, 50); }

    std::filesystem::remove_all(tmp_dir);
}

// ============================================================================
// Scenario 5: Data Pipeline Quality Audit
// ============================================================================
TEST(RealWorldSimulation, DataPipelineQualityAudit) {
    Schema schema;
    schema.type_name = "pipeline_sensor";
    schema.columns = {
        make_datetime_col("timestamp", true),
        make_float_col("value", 0.0, 100.0),
        make_float_col("quality_score", 0.0, 1.0),
    };
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"value", ConstraintOp::kBetween, 10.0, 90.0},
        {"quality_score", ConstraintOp::kBetween, 0.5, 1.0},
    };

    const uint64_t seed = 9999;
    const int64_t row_count = 1000;

    // Step 1: Generate
    GenRequest gen_req{schema, constraints, row_count, seed, "uniform", 1000};
    Sampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    auto table = gen_result.value().data;
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->num_rows(), row_count);

    // Step 2: Validate
    Validator validator(schema, constraints);
    auto val_result = validator.validate_batch(table);
    ASSERT_TRUE(val_result.ok()) << val_result.error().message;
    auto& validation = val_result.value();
    EXPECT_EQ(validation.rows_checked, row_count);
    EXPECT_EQ(validation.rows_failed, 0);

    // Step 3: Evidence package
    engine::evidence::TailReportBuilder tail_builder;
    auto tail_result = tail_builder.build(
        gen_result.value(), validation, gen_req, constraints);
    ASSERT_TRUE(tail_result.ok()) << tail_result.error().message;

    engine::evidence::ProvenanceV1 provenance;
    provenance.data_source = "synthetic";
    provenance.constraints = {"value_range_10_90", "quality_05_1"};
    provenance.generation_params = engine::evidence::GenerationParams{seed, "uniform", row_count, 1000};
    provenance.generator_identity = "RectangularSampler";

    engine::evidence::EvidencePackageBuilder evp_builder;
    auto evp_result = evp_builder.build(
        gen_result.value(), validation, tail_result.value(), provenance, schema);
    ASSERT_TRUE(evp_result.ok()) << evp_result.error().message;
    auto& evidence = evp_result.value();

    EXPECT_EQ(evidence.schema_version, "v1");
    EXPECT_EQ(evidence.row_count, row_count);
    EXPECT_FALSE(evidence.schema_hash.empty());
    EXPECT_EQ(evidence.provenance.generator_identity, "RectangularSampler");
    EXPECT_EQ(evidence.provenance.generation_params.seed, seed);
    EXPECT_EQ(evidence.rows_generated, row_count);
    EXPECT_EQ(evidence.rows_validated, row_count);
    EXPECT_EQ(evidence.rows_failed_validation, 0);
    EXPECT_EQ(evidence.seed_used, seed);

    // Step 4: JSON round-trip
    auto json_result = evp_builder.to_json(evidence);
    ASSERT_TRUE(json_result.ok()) << json_result.error().message;
    auto& json_str = json_result.value();
    EXPECT_GT(json_str.size(), 100u);

    auto deserialized = evp_builder.from_json(json_str);
    ASSERT_TRUE(deserialized.ok()) << deserialized.error().message;
    auto& evp_rt = deserialized.value();

    EXPECT_EQ(evp_rt.schema_version, evidence.schema_version);
    EXPECT_EQ(evp_rt.schema_hash, evidence.schema_hash);
    EXPECT_EQ(evp_rt.row_count, evidence.row_count);
    EXPECT_DOUBLE_EQ(evp_rt.exclusion_rate, evidence.exclusion_rate);
    EXPECT_EQ(evp_rt.data_grade, evidence.data_grade);
    EXPECT_EQ(evp_rt.provenance.generator_identity, evidence.provenance.generator_identity);
    EXPECT_EQ(evp_rt.provenance.generation_params.seed, evidence.provenance.generation_params.seed);
    EXPECT_EQ(evp_rt.rows_generated, evidence.rows_generated);
    EXPECT_EQ(evp_rt.rows_validated, evidence.rows_validated);
    EXPECT_EQ(evp_rt.rows_failed_validation, evidence.rows_failed_validation);
    EXPECT_EQ(evp_rt.seed_used, evidence.seed_used);

    // Step 5: Audit log
    AuditLog audit;
    ASSERT_TRUE(audit.create_genesis().ok());
    EXPECT_EQ(audit.record_count(), 1);

    std::map<std::string, std::string> meta1;
    meta1["type"] = "pipeline_sensor";
    meta1["rows"] = std::to_string(row_count);
    auto s1 = audit.append("data_generation", "RectangularSampler", meta1);
    ASSERT_TRUE(s1.ok()) << s1.error().message;
    EXPECT_EQ(audit.record_count(), 2);

    std::map<std::string, std::string> meta2;
    meta2["rows_checked"] = std::to_string(validation.rows_checked);
    meta2["pass_rate"] = std::to_string(validation.pass_rate);
    auto s2 = audit.append("validation", "ValueRangeValidator", meta2);
    ASSERT_TRUE(s2.ok()) << s2.error().message;
    EXPECT_EQ(audit.record_count(), 3);

    std::map<std::string, std::string> meta3;
    meta3["row_count"] = std::to_string(evidence.row_count);
    meta3["schema_hash"] = evidence.schema_hash;
    auto s3 = audit.append("evidence_package_build", "EvidencePackageBuilder", meta3);
    ASSERT_TRUE(s3.ok()) << s3.error().message;
    EXPECT_EQ(audit.record_count(), 4);

    std::map<std::string, std::string> meta4;
    meta4["json_size"] = std::to_string(json_str.size());
    auto s4 = audit.append("evidence_export", "EvidencePackageJson", meta4);
    ASSERT_TRUE(s4.ok()) << s4.error().message;
    EXPECT_EQ(audit.record_count(), 5);

    // Chain integrity
    auto chain_ok = audit.verify_chain();
    ASSERT_TRUE(chain_ok.ok()) << chain_ok.error().message;
    EXPECT_TRUE(chain_ok.value()) << "Chain broken";

    auto daily = audit.daily_verification();
    ASSERT_TRUE(daily.ok()) << daily.error().message;
    EXPECT_TRUE(daily.value().is_valid);
    EXPECT_EQ(daily.value().total_records, 5);
    EXPECT_TRUE(daily.value().broken_links.empty());

    auto latest = audit.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().operation, "evidence_export");

    // Step 6: Re-generate with same seed -> bitwise identical
    GenRequest gen_req2{schema, constraints, row_count, seed, "uniform", 1000};
    Sampler sampler2(schema);
    auto gen_result2 = sampler2.generate(gen_req2);
    ASSERT_TRUE(gen_result2.ok()) << gen_result2.error().message;
    auto table2 = gen_result2.value().data;
    ASSERT_NE(table2, nullptr);
    EXPECT_EQ(table2->num_rows(), row_count);

    ASSERT_EQ(table->num_columns(), table2->num_columns());
    for (int col = 0; col < table->num_columns(); ++col) {
        auto c1 = table->column(col);
        auto c2 = table2->column(col);
        ASSERT_EQ(c1->length(), c2->length());
        ASSERT_EQ(c1->type()->id(), c2->type()->id());
        auto col_name = table->schema()->field(col)->name();

        if (c1->type()->id() == arrow::Type::DOUBLE) {
            auto v1 = extract_doubles(table, col_name);
            auto v2 = extract_doubles(table2, col_name);
            ASSERT_EQ(v1.size(), v2.size());
            for (size_t i = 0; i < v1.size(); ++i) {
                EXPECT_DOUBLE_EQ(v1[i], v2[i]) << "col " << col << " row " << i;
            }
        } else if (c1->type()->id() == arrow::Type::INT64) {
            auto v1 = extract_int64s(table, col_name);
            auto v2 = extract_int64s(table2, col_name);
            ASSERT_EQ(v1.size(), v2.size());
            for (size_t i = 0; i < v1.size(); ++i) {
                EXPECT_EQ(v1[i], v2[i]) << "col " << col << " row " << i;
            }
        } else if (c1->type()->id() == arrow::Type::STRING) {
            auto v1 = extract_strings(table, col_name);
            auto v2 = extract_strings(table2, col_name);
            ASSERT_EQ(v1.size(), v2.size());
            for (size_t i = 0; i < v1.size(); ++i) {
                EXPECT_EQ(v1[i], v2[i]) << "col " << col << " row " << i;
            }
        }
    }

    // Evidence JSON should be identical
    auto val_result2 = validator.validate_batch(table2);
    ASSERT_TRUE(val_result2.ok());
    auto tail2 = tail_builder.build(
        gen_result2.value(), val_result2.value(), gen_req2, constraints);
    ASSERT_TRUE(tail2.ok());
    auto evp2 = evp_builder.build(
        gen_result2.value(), val_result2.value(), tail2.value(), provenance, schema);
    ASSERT_TRUE(evp2.ok());
    auto json2 = evp_builder.to_json(evp2.value());
    ASSERT_TRUE(json2.ok());
    EXPECT_EQ(json_str, json2.value())
        << "Evidence JSON differs on re-generation with same seed";
}

// ============================================================================
// Bonus: Weather station via SynthGenService API
// ============================================================================
TEST(RealWorldSimulation, WeatherStationViaServiceAPI) {
    api::SynthGenService service;

    api::DefineTypeRequest type_req;
    type_req.type_name = "weather_api_test";

    api::DefineTypeRequest::ColumnDef tc;
    tc.name = "temperature"; tc.type = "FLOAT";
    tc.range_min = -40.0; tc.range_max = 50.0;
    type_req.columns.push_back(tc);

    api::DefineTypeRequest::ColumnDef hc;
    hc.name = "humidity"; hc.type = "FLOAT";
    hc.range_min = 0.0; hc.range_max = 100.0;
    type_req.columns.push_back(hc);

    auto type_result = service.define_type(type_req);
    ASSERT_TRUE(type_result.ok()) << type_result.error().message;
    EXPECT_EQ(type_result.value().type_name, "weather_api_test");
    EXPECT_EQ(type_result.value().column_count, 2);

    api::DefineConstraintRequest cr;
    cr.constraint_name = "weather_safety";
    cr.type_name = "weather_api_test";
    api::DefineConstraintRequest::RangeCheck tchk;
    tchk.column = "temperature"; tchk.min_val = -20.0; tchk.max_val = 40.0;
    cr.checks.push_back(tchk);
    api::DefineConstraintRequest::RangeCheck hchk;
    hchk.column = "humidity"; hchk.min_val = 20.0; hchk.max_val = 95.0;
    cr.checks.push_back(hchk);

    auto constraint_result = service.define_constraint(cr);
    ASSERT_TRUE(constraint_result.ok()) << constraint_result.error().message;
    EXPECT_EQ(constraint_result.value().check_count, 2);

    api::GenerateRequest gr;
    gr.type_name = "weather_api_test";
    gr.constraints = {"weather_safety"};
    gr.limit = 1000;
    gr.seed = 42;
    gr.distribution = "uniform";

    auto gen_result = service.generate(gr);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    auto& result = gen_result.value();
    EXPECT_EQ(result.stats.rows_generated, 1000);
    EXPECT_GE(result.stats.elapsed_ms, 0);
    EXPECT_EQ(result.stats.distribution_used, "uniform");
    EXPECT_FALSE(result.evidence_json.empty());

    // Parse evidence JSON
    engine::evidence::EvidencePackageBuilder evp_builder;
    auto evp_parsed = evp_builder.from_json(result.evidence_json);
    ASSERT_TRUE(evp_parsed.ok()) << evp_parsed.error().message;
    auto& pkg = evp_parsed.value();

    EXPECT_EQ(pkg.row_count, 1000);
    EXPECT_EQ(pkg.schema_version, "v1");
    EXPECT_FALSE(pkg.schema_hash.empty());
    EXPECT_EQ(pkg.provenance.generator_identity, "physics_sampler");
    EXPECT_EQ(pkg.provenance.constraints.size(), 1u);
    EXPECT_EQ(pkg.provenance.constraints[0], "weather_safety");
    EXPECT_EQ(pkg.rows_generated, 1000);
    EXPECT_EQ(pkg.rows_validated, 1000);
    EXPECT_EQ(pkg.seed_used, 42u);

    // Honest applicability declarations
    EXPECT_EQ(pkg.audit_immutability, "not_applicable");
    EXPECT_EQ(pkg.statistical_fidelity, "not_applicable");
    EXPECT_EQ(pkg.drift_detection, "not_applicable");

    // Health check
    auto health = service.health();
    EXPECT_EQ(health.status, "healthy");
    EXPECT_EQ(health.components["parser"], "ok");
    EXPECT_EQ(health.components["physics_engine"], "ok");
}
