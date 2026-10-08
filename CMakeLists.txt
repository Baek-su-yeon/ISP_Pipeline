cmake_minimum_required(VERSION 3.16)
project(ISP_Pipeline CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw)

# src/test.cpp는 리팩토링 전 단일 파일 버전이라 빌드에서 뺀다 (파일은 남겨 둠)
file(GLOB ISP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(isp ${ISP_SOURCES})
target_compile_options(isp PRIVATE -Wall -Wextra)
target_link_libraries(isp PRIVATE ${OpenCV_LIBS} PkgConfig::LIBRAW)
