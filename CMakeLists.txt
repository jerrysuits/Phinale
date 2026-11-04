cmake_minimum_required(VERSION 3.20)
project(Phinale LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# core library — lexer/parser/etc. Testable independent of main().
add_library(phinale_core
        src/token.cpp
        src/lexer.cpp
        src/parser.cpp
        src/sema.cpp
        src/tac.cpp
        src/ir_builder.cpp
        src/cfg.cpp
        src/cfg_builder.cpp
        src/dominance.cpp
        src/ssa_builder.cpp
        src/ssa_destruction.cpp
        src/liveness.cpp
        src/interference_graph.cpp
        src/coalescing.cpp
)
target_include_directories(phinale_core PUBLIC include)

# CLI driver
add_executable(phinale src/main.cpp)
target_link_libraries(phinale PRIVATE phinale_core)

# Tests
enable_testing()

add_executable(test_lexer tests/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE phinale_core)
add_test(NAME test_lexer COMMAND test_lexer)

add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE phinale_core)
add_test(NAME test_parser COMMAND test_parser)

add_executable(test_ir tests/test_ir.cpp)
target_link_libraries(test_ir PRIVATE phinale_core)
add_test(NAME test_ir COMMAND test_ir)

add_executable(test_cfg tests/test_cfg.cpp)
target_link_libraries(test_cfg PRIVATE phinale_core)
add_test(NAME test_cfg COMMAND test_cfg)

add_executable(test_dominance tests/test_dominance.cpp)
target_link_libraries(test_dominance PRIVATE phinale_core)
add_test(NAME test_dominance COMMAND test_dominance)

add_executable(test_ssa tests/test_ssa.cpp)
target_link_libraries(test_ssa PRIVATE phinale_core)
add_test(NAME test_ssa COMMAND test_ssa)

add_executable(test_ssa_destruction tests/test_ssa_destruction.cpp)
target_link_libraries(test_ssa_destruction PRIVATE phinale_core)
add_test(NAME test_ssa_destruction COMMAND test_ssa_destruction)

add_executable(test_liveness_interference tests/test_liveness_interference.cpp)
target_link_libraries(test_liveness_interference PRIVATE phinale_core)
add_test(NAME test_liveness_interference COMMAND test_liveness_interference)
