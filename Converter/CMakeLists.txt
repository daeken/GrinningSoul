cmake_minimum_required(VERSION 3.15)
project(Converter)

set(CMAKE_CXX_STANDARD 20)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost 1.72.0 COMPONENTS filesystem)
include_directories(${Boost_INCLUDE_DIRS})

include_directories(/usr/local/include)
link_directories(/usr/local/lib)
link_libraries(clang Boost::filesystem)

add_executable(converter main.cpp simenv.cpp simenv.h macho.cpp macho.h)
