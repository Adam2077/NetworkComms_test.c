cmake_minimum_required(VERSION 3.16)
project(untitled C)

set(CMAKE_C_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/lib/mpack)

set(MPACK_SOURCES
        ${PROJECT_SOURCE_DIR}/lib/mpack/mpack.c
)

add_executable(untitled main.c ${MPACK_SOURCES})

target_link_libraries(untitled Ws2_32)
