add_library(synthgen_schema
    schema.cpp
    schema_registry.cpp
    schema_builder.cpp
)
target_link_libraries(synthgen_schema PUBLIC synthgen_common synthgen_parser)
