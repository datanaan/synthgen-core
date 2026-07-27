add_library(synthgen_constraint
    value_range_validator.cpp
    inter_row_engine.cpp
    aggregate_engine.cpp
)
target_link_libraries(synthgen_constraint PUBLIC synthgen_common synthgen_schema synthgen_scaffold Arrow::arrow_shared)
