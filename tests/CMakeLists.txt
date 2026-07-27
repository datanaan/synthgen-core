function(add_synthgen_test test_name test_source)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE
        synthgen_common synthgen_parser synthgen_schema
        synthgen_physics synthgen_constraint synthgen_evidence
        synthgen_storage synthgen_scaffold synthgen_api synthgen_router synthgen_audit synthgen_postfilter
        synthgen_version synthgen_model_storage synthgen_gc synthgen_alignment synthgen_timetravel
        GTest::gtest GTest::gtest_main GTest::gmock
    )
    gtest_discover_tests(${test_name})
endfunction()

# Storage unit tests
add_synthgen_test(parquet_io_test unit/parquet_io_test.cpp)
add_synthgen_test(metadata_test unit/metadata_test.cpp)

# Storage integration tests
add_synthgen_test(storage_integration_test integration/storage_integration_test.cpp)

# Parser + Schema unit tests
add_synthgen_test(lexer_test unit/lexer_test.cpp)
add_synthgen_test(parser_test unit/parser_test.cpp)
add_synthgen_test(schema_test unit/schema_test.cpp)

# Common module tests
add_synthgen_test(common_test unit/common_test.cpp)

# Physics Engine unit tests
add_synthgen_test(seed_controller_test unit/seed_controller_test.cpp)
add_synthgen_test(distribution_test unit/distribution_test.cpp)
add_synthgen_test(rectangular_sampler_test unit/rectangular_sampler_test.cpp)
add_synthgen_test(physics_deep_test unit/physics_deep_test.cpp)

# Scaffold tests
add_synthgen_test(scaffold_test unit/scaffold_test.cpp)
add_synthgen_test(scaffold_deep_test unit/scaffold_deep_test.cpp)

# Wave 2 tests
add_synthgen_test(data_import_test unit/data_import_test.cpp)
add_synthgen_test(validation_test unit/validation_test.cpp)

# Wave 3 tests (Unit F: EvidencePackage Builder)
add_synthgen_test(evidence_package_test unit/evidence_package_test.cpp)

# Wave 3 tests (Unit I: Tool Line)
add_synthgen_test(test_helpers_test unit/test_helpers_test.cpp)

# Wave 3 tests (Unit G: SDK + REST API)
add_synthgen_test(service_test unit/service_test.cpp)
add_synthgen_test(api_test integration/api_test.cpp)

# v2 tests (Unit J: Inter-row engine)
add_synthgen_test(inter_row_test unit/inter_row_test.cpp)

# v2 tests (Unit K: Aggregate engine)
add_synthgen_test(aggregate_test unit/aggregate_test.cpp)

# v2 tests (Unit L: Constraint classifier)
add_synthgen_test(classifier_test unit/classifier_test.cpp)

# v2 tests (Unit M: Execution router)
add_synthgen_test(router_test unit/router_test.cpp)

# v2 tests (Unit N: Post-filter)
add_synthgen_test(postfilter_test unit/postfilter_test.cpp)

# v2 tests (Unit P: EvidencePackage v2)
add_synthgen_test(evidence_v2_test unit/evidence_v2_test.cpp)

# v2 tests (Unit O: Audit log)
add_synthgen_test(audit_test unit/audit_test.cpp)

# v2 extended tests
add_synthgen_test(inter_row_extended_test unit/inter_row_extended_test.cpp)
add_synthgen_test(aggregate_extended_test unit/aggregate_extended_test.cpp)
add_synthgen_test(classifier_extended_test unit/classifier_extended_test.cpp)
add_synthgen_test(router_extended_test unit/router_extended_test.cpp)
add_synthgen_test(postfilter_extended_test unit/postfilter_extended_test.cpp)
add_synthgen_test(audit_extended_test unit/audit_extended_test.cpp)
add_synthgen_test(evidence_v2_extended_test unit/evidence_v2_extended_test.cpp)
add_synthgen_test(v2_integration_test unit/v2_integration_test.cpp)

# v1 extended tests
add_synthgen_test(lexer_extended_test unit/lexer_extended_test.cpp)
add_synthgen_test(v1_extended_test unit/v1_extended_test.cpp)

# v3 tests (Unit Q: Model version chain)
add_synthgen_test(model_version_chain_test unit/model_version_chain_test.cpp)

# v3 tests (Unit T#24: Compaction bias report)
add_synthgen_test(compaction_bias_report_test unit/compaction_bias_report_test.cpp)

# v3 tests (Unit T#23: Storage model layer)
add_synthgen_test(model_storage_test unit/model_storage_test.cpp)

# v3 tests (Unit R: GC compaction)
add_synthgen_test(gc_compactor_test unit/gc_compactor_test.cpp)

# v3 tests (Unit T#22: TailReport v3)
add_synthgen_test(tail_report_v3_test unit/tail_report_v3_test.cpp)

# v3 tests (Unit S#21: DriftDetector)
add_synthgen_test(drift_detector_test unit/drift_detector_test.cpp)

# v3 tests (Unit S#20: TimeTravelEngine)
add_synthgen_test(time_travel_test unit/time_travel_test.cpp)

# v3 tests (Unit S#25-26: ContinuousAlignmentEngine)
add_synthgen_test(continuous_alignment_test unit/continuous_alignment_test.cpp)

# v3 integration tests
add_synthgen_test(v3_integration_test unit/v3_integration_test.cpp)

# Constraint pipeline integration tests
add_synthgen_test(constraint_pipeline_test integration/constraint_pipeline_test.cpp)

# Storage + Engine integration tests
add_synthgen_test(storage_engine_test integration/storage_engine_test.cpp)

# E2E smoke tests
add_synthgen_test(smoke_test e2e/smoke_test.cpp)

# E2E service integration tests
add_synthgen_test(service_integration_test e2e/service_integration_test.cpp)

# E2E pipeline stress tests
add_synthgen_test(pipeline_stress_test e2e/pipeline_stress_test.cpp)

# E2E parser fuzz tests
add_synthgen_test(parser_fuzz_test e2e/parser_fuzz_test.cpp)

# E2E storage chaos tests
add_synthgen_test(storage_chaos_test e2e/storage_chaos_test.cpp)

# E2E v2+v3 chaos integration tests
add_synthgen_test(v2_v3_chaos_test e2e/v2_v3_chaos_test.cpp)

# E2E data quality tests
add_synthgen_test(data_quality_test e2e/data_quality_test.cpp)

# E2E v3 deep edge-case tests
add_synthgen_test(v3_deep_test e2e/v3_deep_test.cpp)

# E2E constraint edge tests (chaos round 2)
add_synthgen_test(constraint_edge_test e2e/constraint_edge_test.cpp)

# E2E schema & evidence chaos tests (chaos round 2)
add_synthgen_test(schema_evidence_chaos_test e2e/schema_evidence_chaos_test.cpp)

# E2E service scaffold chaos tests (chaos round 2)
add_synthgen_test(service_scaffold_test e2e/service_scaffold_test.cpp)

# E2E negative destructive tests (round 3)
add_synthgen_test(negative_destructive_test e2e/negative_destructive_test.cpp)

# E2E pipeline chaos tests (chaos round 3)
add_synthgen_test(e2e_pipeline_chaos_test e2e/e2e_pipeline_chaos_test.cpp)

# E2E numeric precision tests
add_synthgen_test(numeric_precision_test e2e/numeric_precision_test.cpp)

# E2E type system tests (STRING, DATETIME, ENUM)
add_synthgen_test(type_system_test e2e/type_system_test.cpp)

# E2E real-world industrial simulation tests
add_synthgen_test(realworld_simulation_test e2e/realworld_simulation_test.cpp)

# E2E composition matrix tests
add_synthgen_test(composition_matrix_test e2e/composition_matrix_test.cpp)

# E2E concurrency and fault injection tests
add_synthgen_test(concurrency_fault_test e2e/concurrency_fault_test.cpp)

# E2E performance and stress tests
add_synthgen_test(performance_stress_test e2e/performance_stress_test.cpp)
