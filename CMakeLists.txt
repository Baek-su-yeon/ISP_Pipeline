cmake_minimum_required(VERSION 3.16)
project(ISP_Pipeline CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw)

add_executable(check_env src/check_env.cpp)
target_link_libraries(check_env PRIVATE ${OpenCV_LIBS} PkgConfig::LIBRAW)

add_executable(file_open src/file_open.cpp)
target_link_libraries(file_open PRIVATE ${OpenCV_LIBS} PkgConfig::LIBRAW)