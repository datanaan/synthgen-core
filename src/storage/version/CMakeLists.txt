add_library(synthgen_version
    model_version_chain.cpp
)
target_link_libraries(synthgen_version PUBLIC synthgen_common synthgen_scaffold synthgen_storage)
