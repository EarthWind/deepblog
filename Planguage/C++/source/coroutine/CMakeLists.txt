cmake_minimum_required(VERSION 3.20)
project(cpp_coroutine_tutorial LANGUAGES CXX)

add_executable(mini_coroutine mini_coroutine.cpp)
target_compile_features(mini_coroutine PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(mini_coroutine PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        mini_coroutine
        PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
endif()

enable_testing()
add_test(NAME mini_coroutine_runs COMMAND mini_coroutine)
