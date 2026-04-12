cmake_minimum_required(VERSION 3.15)
project(sqx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP REQUIRED)

# Sequential solver (no OpenMP needed, but link it anyway for compatibility)
add_executable(sqx_seq src/main_sequential.cpp)

# Task parallelism (OpenMP)
add_executable(sqx_task src/main_task_paral.cpp)
target_link_libraries(sqx_task PRIVATE OpenMP::OpenMP_CXX)

# Data parallelism (OpenMP)
add_executable(sqx_data src/main_data_paral.cpp)
target_link_libraries(sqx_data PRIVATE OpenMP::OpenMP_CXX)

# MPI + OpenMP (original main.cpp)
add_executable(sqx src/main.cpp)
target_link_libraries(sqx PRIVATE OpenMP::OpenMP_CXX)

if(MINGW OR WIN32)
    target_link_options(sqx PRIVATE -static)
    target_link_options(sqx_seq PRIVATE -static)
    target_link_options(sqx_task PRIVATE -static)
    target_link_options(sqx_data PRIVATE -static)
endif()
