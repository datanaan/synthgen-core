add_library(synthgen_physics
    seed_controller.cpp
    random.cpp
    uniform_sampler.cpp
    gaussian_sampler.cpp
    range_extractor.cpp
    rectangular_sampler.cpp
)
target_link_libraries(synthgen_physics PUBLIC synthgen_common synthgen_schema Arrow::arrow_shared)
