cmake_minimum_required(VERSION 3.29)
project(yufs C)

set(CMAKE_C_STANDARD 11)

set(CORE_SRC
    src/yufs_core.c
)

# add_compile_definitions(__KERNEL__) # define for kernel build

add_compile_definitions(ENABLE_LOG)


