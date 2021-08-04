cmake_minimum_required(VERSION 2.8)
project(main)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

include_directories()

link_directories()

set(SRC_LIST muduo_server.cpp)
#aux_source_directory(. SRC_LIST)
add_executable(server SRC_LIST)


target_link_libraries(server muduo_net muduo_base pthread)