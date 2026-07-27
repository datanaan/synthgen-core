add_library(synthgen_alignment
    drift_detector.cpp
    continuous_alignment_engine.cpp
)
target_link_libraries(synthgen_alignment PUBLIC synthgen_common synthgen_version synthgen_model_storage synthgen_scaffold)
