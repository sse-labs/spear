cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 14)

include_directories(../Rapl-reader)

add_executable(bencher
        Profiler.cpp Profiler.h ../Rapl-reader/RegisterReader.h ../Rapl-reader/RegisterReader.cpp)
