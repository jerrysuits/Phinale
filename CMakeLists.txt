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
