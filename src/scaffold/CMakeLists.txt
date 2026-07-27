add_library(synthgen_scaffold
    trace.cpp
    explain.cpp
    metrics.cpp
)
target_link_libraries(synthgen_scaffold PUBLIC synthgen_common)
