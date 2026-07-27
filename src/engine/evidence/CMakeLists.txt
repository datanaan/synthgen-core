add_library(synthgen_evidence
    tail_report.cpp
    tail_report_v3.cpp
    evidence_package_json.cpp
    schema_validator.cpp
    evidence_package_builder.cpp
    evidence_package_v2_builder.cpp
)
target_link_libraries(synthgen_evidence PUBLIC synthgen_common synthgen_schema synthgen_physics synthgen_constraint synthgen_scaffold synthgen_router synthgen_postfilter)
