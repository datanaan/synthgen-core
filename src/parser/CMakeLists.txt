add_library(synthgen_parser
    lexer.cpp
    parser.cpp
)
target_link_libraries(synthgen_parser PUBLIC synthgen_common)
