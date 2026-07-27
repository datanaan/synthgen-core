add_library(synthgen_common
    hash.cpp
)
target_include_directories(synthgen_common PUBLIC ${CMAKE_SOURCE_DIR}/src)
find_package(OpenSSL REQUIRED)
target_link_libraries(synthgen_common PUBLIC OpenSSL::Crypto)
