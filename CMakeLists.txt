cmake_minimum_required(VERSION 3.5)
project(statsrelay_private)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES main.cpp)
add_executable(statsrelay_private ${SOURCE_FILES})