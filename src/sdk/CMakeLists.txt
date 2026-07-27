find_package(pybind11 REQUIRED)

pybind11_add_module(synthgen_native bindings.cpp)
target_link_libraries(synthgen_native PRIVATE
    synthgen_api synthgen_common synthgen_parser synthgen_schema
    synthgen_physics synthgen_constraint synthgen_evidence
    synthgen_storage synthgen_scaffold
)
target_include_directories(synthgen_native PRIVATE ${CMAKE_SOURCE_DIR}/src)
