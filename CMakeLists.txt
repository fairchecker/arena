cmake_minimum_required(VERSION 3.16)
project(arena LANGUAGES CXX)

option(ARENA_BUILD_TESTS "Build arena unit tests" ON)
option(ARENA_BUILD_DEMO "Build the arena demo executable" ON)
option(ARENA_BUILD_GUI "Build the local GUI simulation backend" ON)
option(ARENA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(arena_apply_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(ARENA_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

add_library(arena_core
    src/agent.cpp
    src/arena.cpp
    src/entity.cpp
    src/entity_registry.cpp
    src/hdc_brain.cpp
    src/graph_brain.cpp
    src/learned_brain.cpp
    src/world.cpp
)
target_include_directories(arena_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(arena_core PUBLIC cxx_std_17)

arena_apply_compile_options(arena_core)

# Keep executable locations predictable across single-config generators (Ninja,
# MinGW Makefiles) and multi-config generators (Visual Studio, Ninja Multi-Config).
# Explicit $<CONFIG> prevents multi-config generators from adding another
# configuration directory automatically.
set(ARENA_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

if(ARENA_BUILD_DEMO)
    add_executable(arena_demo src/main.cpp)
    target_link_libraries(arena_demo PRIVATE arena_core)
    set_target_properties(arena_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ARENA_RUNTIME_OUTPUT_DIRECTORY}"
    )
    arena_apply_compile_options(arena_demo)
endif()

if(ARENA_BUILD_GUI)
    add_executable(arena_gui_backend src/gui_backend.cpp)
    target_link_libraries(arena_gui_backend PRIVATE arena_core)
    set_target_properties(arena_gui_backend PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ARENA_RUNTIME_OUTPUT_DIRECTORY}"
    )
    arena_apply_compile_options(arena_gui_backend)
endif()

if(ARENA_BUILD_TESTS)
    enable_testing()
    add_executable(arena_tests tests/arena_tests.cpp)
    target_link_libraries(arena_tests PRIVATE arena_core)
    set_target_properties(arena_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ARENA_RUNTIME_OUTPUT_DIRECTORY}"
    )
    arena_apply_compile_options(arena_tests)
    add_test(NAME arena_tests COMMAND arena_tests)
    add_executable(hdc_tests tests/hdc_tests.cpp)
    target_link_libraries(hdc_tests PRIVATE arena_core)
    arena_apply_compile_options(hdc_tests)
    add_test(NAME hdc_tests COMMAND hdc_tests)
    add_executable(graph_tests tests/graph_tests.cpp)
    target_link_libraries(graph_tests PRIVATE arena_core)
    arena_apply_compile_options(graph_tests)
    add_test(NAME graph_tests COMMAND graph_tests)
    add_executable(learned_tests tests/learned_tests.cpp)
    target_link_libraries(learned_tests PRIVATE arena_core)
    arena_apply_compile_options(learned_tests)
    add_test(NAME learned_tests COMMAND learned_tests)
    add_executable(skill_generalization tests/skill_generalization.cpp)
    target_link_libraries(skill_generalization PRIVATE arena_core)
    arena_apply_compile_options(skill_generalization)
    add_test(NAME skill_generalization COMMAND skill_generalization)
    add_executable(learning_benchmark tests/learning_benchmark.cpp)
    target_link_libraries(learning_benchmark PRIVATE arena_core)
    arena_apply_compile_options(learning_benchmark)
    add_test(NAME arena_learning_benchmark COMMAND learning_benchmark)
    # Task 2 benchmark (out-of-sight fixed food, learning curve). Full runs are done by hand;
    # CTest only runs its self-test (BFS optimum, greedy baseline, determinism).
    add_executable(memory_curve tests/memory_curve.cpp)
    target_link_libraries(memory_curve PRIVATE arena_core)
    arena_apply_compile_options(memory_curve)
    add_test(NAME arena_memory_curve_self_test COMMAND memory_curve --self-test)
    if(ARENA_BUILD_GUI)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME arena_gui_smoke
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_smoke.py
                        $<TARGET_FILE:arena_gui_backend> ${CMAKE_CURRENT_SOURCE_DIR}/gui/server.py)
            add_test(NAME arena_experiment_smoke
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/experiment_smoke.py
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_experiment.py $<TARGET_FILE:arena_gui_backend>
                        $<TARGET_FILE:skill_generalization>)
        endif()
    endif()
endif()
