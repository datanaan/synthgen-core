add_library(synthgen_postfilter
    post_filter.cpp
)
target_link_libraries(synthgen_postfilter PUBLIC synthgen_common synthgen_scaffold Arrow::arrow_shared)
