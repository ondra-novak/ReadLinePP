cmake_minimum_required (VERSION 3.2)
project (readlinepp)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

add_library (readlinepp readlinepp.cpp)

add_executable(rldemo demo.cpp)
target_link_libraries(rldemo readlinepp readline pthread)

