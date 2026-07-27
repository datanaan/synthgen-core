add_library(synthgen_api
    service.cpp
    server.cpp
)
target_link_libraries(synthgen_api PUBLIC
    synthgen_common synthgen_parser synthgen_schema
    synthgen_physics synthgen_constraint synthgen_evidence
    synthgen_storage synthgen_scaffold
)
target_include_directories(synthgen_api PUBLIC ${CMAKE_SOURCE_DIR}/src /usr/local/include)
