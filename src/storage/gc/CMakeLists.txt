add_library(synthgen_gc
    protection.cpp
    gc_compactor.cpp
)
target_link_libraries(synthgen_gc PUBLIC synthgen_common synthgen_version synthgen_scaffold)
