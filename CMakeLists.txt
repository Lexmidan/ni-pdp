cmake_minimum_required(VERSION 3.15)
project(sqx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP REQUIRED)

add_executable(sqx src/main.cpp)
target_link_libraries(sqx PRIVATE OpenMP::OpenMP_CXX)

if(MINGW OR WIN32)
    target_link_options(sqx PRIVATE -static)
endif()
