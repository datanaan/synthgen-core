add_library(synthgen_storage
    parquet_io.cpp
    metadata.cpp
    object_store_backend.cpp
    data_importer.cpp
)
target_link_libraries(synthgen_storage PUBLIC synthgen_common synthgen_schema synthgen_scaffold Arrow::arrow_shared Parquet::parquet_shared)
