cmake_minimum_required(VERSION 3.15)
project(lang-interpreter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Sources ────────────────────────────────────────────────────────────────────
set(SOURCES
    src/lexer.c
    src/ast.c
    src/parser.c
    src/value.c
    src/interpreter.c
    src/builtins.c
    src/module.c
    src/main.c
)

# ── Interpreter binary ─────────────────────────────────────────────────────────
add_executable(interpreter ${SOURCES})

target_include_directories(interpreter PRIVATE src)

target_compile_options(interpreter PRIVATE -Wall -Wextra)

# math library (needed for sqrt, pow, floor, ceil)
target_link_libraries(interpreter PRIVATE m)

# ── Install ────────────────────────────────────────────────────────────────────
install(TARGETS interpreter RUNTIME DESTINATION bin)

# ── Tests (CTest) ──────────────────────────────────────────────────────────────
enable_testing()

add_test(
    NAME test_basic
    COMMAND interpreter ${CMAKE_SOURCE_DIR}/tests/test_basic.txt
)

add_test(
    NAME test_functions
    COMMAND interpreter ${CMAKE_SOURCE_DIR}/tests/test_functions.txt
)

add_test(
    NAME test_patterns
    COMMAND interpreter ${CMAKE_SOURCE_DIR}/tests/test_patterns.txt
)
