# CMakeLists.txt: configuration to make Makefiles with CMAKE_CXX_FLAGS
# 
# Usage: 
# 
# To generate a Makefile to build the project with default options:
#     cmake . 
# 
# To generate a Makefile to build a static version that does not depend 
# on the BMI2 instruction set (suitable for the AMD Jaguar architecture)
#     cmake -DCMAKE_STATIC:BOOL=ON .
# 
# MIT License
# 
# Copyright (c) 2018 Manuel Martinez Torres, portions by Miguel Hernández-Cabronero
# 
# Marlin: A Fast Entropy Codec
# 
# MIT License
# 
# Copyright (c) 2018 Manuel Martinez Torres
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


cmake_minimum_required(VERSION 3.5)
project(Marlin VERSION 0.01)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wextra -Wcast-qual -Wcast-align -Wstrict-aliasing=1 -Wswitch-enum -Wundef -pedantic -Wfatal-errors -Wshadow")

# Set of flags to generate static, AMD Jaguar compatible binaries
if(CMAKE_STATIC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static -DNO_BMI2 -mno-bmi2")
    set(NAME_TAIL "_static")
else()
    set(NAME_TAIL "")
endif()

################################
# Marlin library (entropy codec only)
file(GLOB MAIN_SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cc)
file(GLOB IMAGE_ONLY_SRC_FILES ${PROJECT_SOURCE_DIR}/src/image*.cc)
list(REMOVE_ITEM MAIN_SRC_FILES ${IMAGE_ONLY_SRC_FILES})

add_library(marlin${NAME_TAIL} ${MAIN_SRC_FILES})
set_target_properties(marlin${NAME_TAIL} PROPERTIES
    VERSION ${PROJECT_VERSION}
    PUBLIC_HEADER inc/marlin.h)
target_include_directories(marlin${NAME_TAIL} PRIVATE inc)

# ImageMarlin library (image codec + entropy codec)
file(GLOB MAIN_SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cc)
add_library(imarlin${NAME_TAIL} ${MAIN_SRC_FILES} inc/imageMarlin.hpp)
set_target_properties(imarlin${NAME_TAIL} PROPERTIES
        VERSION ${PROJECT_VERSION}
        PUBLIC_HEADER inc/imageMarlin.hpp)
target_include_directories(imarlin${NAME_TAIL} PRIVATE inc)

# Library of codecs to compare with in the benchmark
file(GLOB BENCHMARK_FILES ${PROJECT_SOURCE_DIR}/test/codecs/*.cc)
add_library(libbenchmark ${BENCHMARK_FILES} ${MAIN_SRC_FILES})
set_target_properties(libbenchmark PROPERTIES
        VERSION ${PROJECT_VERSION}
        )
target_include_directories(libbenchmark PRIVATE ext inc)

################################
# Standalone utilities
option(WITH_UTILS "Build Utilities" ON)
if(WITH_UTILS)
    file(GLOB UTILS_SRC_FILES ${PROJECT_SOURCE_DIR}/utils/*.cc)
    foreach(_util_file ${UTILS_SRC_FILES})
        get_filename_component(_util_name ${_util_file} NAME_WE)
        add_executable(${_util_name}${NAME_TAIL} ${_util_file})
        target_include_directories(${_util_name}${NAME_TAIL} PRIVATE inc utils)
        target_link_libraries(${_util_name}${NAME_TAIL} imarlin${NAME_TAIL})
    endforeach()
endif()

#################################
# Testing binaries
find_package(OpenCV REQUIRED)
option(WITH_TESTS "Build Tests" ON)
if(WITH_TESTS)
    enable_testing()
    file(GLOB TEST_SRC_FILES ${PROJECT_SOURCE_DIR}/test/*.cc)

    ## from list of files we'll create tests test_name.cpp -> test_name
    foreach(_test_file ${TEST_SRC_FILES})
        get_filename_component(_test_name ${_test_file} NAME_WE)
        add_executable(${_test_name}${NAME_TAIL} ${_test_file})
        target_include_directories(${_test_name}${NAME_TAIL} PRIVATE inc utils src test)
        target_link_libraries(${_test_name}${NAME_TAIL} marlin${NAME_TAIL} libbenchmark ${OpenCV_LIBS} -lsnappy -lCharLS -lzstd -llz4 -llzo2 -lpthread -lz ${PROJECT_SOURCE_DIR}/ext/FiniteStateEntropy.a ${PROJECT_SOURCE_DIR}/ext/gipfeli.a)
        add_test(${_test_name}${NAME_TAIL} ${_test_name}${NAME_TAIL})
    endforeach()
endif()
