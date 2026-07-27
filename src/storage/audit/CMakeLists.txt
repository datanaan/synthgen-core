add_library(synthgen_audit
    audit_log.cpp
)
target_link_libraries(synthgen_audit PUBLIC synthgen_common synthgen_scaffold)
