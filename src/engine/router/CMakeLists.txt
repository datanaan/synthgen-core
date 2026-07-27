add_library(synthgen_router
    constraint_classifier.cpp
    execution_router.cpp
)
target_link_libraries(synthgen_router PUBLIC
    synthgen_common synthgen_schema synthgen_constraint synthgen_scaffold
)
