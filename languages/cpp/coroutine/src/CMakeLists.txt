cmake_minimum_required(VERSION 3.20)
project(cpp_coroutine_tutorial LANGUAGES CXX)

add_executable(mini_coroutine mini_coroutine.cpp)
target_compile_features(mini_coroutine PRIVATE cxx_std_20)

add_executable(gcc_coroutine_probe gcc_coroutine_probe.cpp)
target_compile_features(gcc_coroutine_probe PRIVATE cxx_std_20)

if(MSVC)
    foreach(target mini_coroutine gcc_coroutine_probe)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endforeach()
else()
    foreach(target mini_coroutine gcc_coroutine_probe)
        target_compile_options(
            ${target}
            PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endforeach()
endif()

enable_testing()
add_test(NAME mini_coroutine_runs COMMAND mini_coroutine)
add_test(NAME gcc_coroutine_probe_runs COMMAND gcc_coroutine_probe)
