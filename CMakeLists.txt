cmake_minimum_required(VERSION 3.20)

project(yufs LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# enable kernel build
option(KERNEL_BUILD "Build Linux Kernel Module instead of Userspace Tests" OFF)

# log define
add_compile_definitions(ENABLE_LOG)

set(CORE_SRC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/yufs_core.c"
)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")

if(KERNEL_BUILD)
    # kernel build
    message(STATUS "Configuring for KERNEL build...")
    set(MODULE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
    execute_process(
            COMMAND uname -r
            OUTPUT_VARIABLE KERNEL_RELEASE
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "Target Kernel Release: ${KERNEL_RELEASE}")
    add_custom_target(yufs_kmod ALL
            COMMAND make -C /lib/modules/${KERNEL_RELEASE}/build M=${MODULE_SOURCE_DIR} modules
            WORKING_DIRECTORY ${MODULE_SOURCE_DIR}
            COMMENT "Building Linux Kernel Module via Kbuild"
    )
    add_custom_target(yufs_clean
            COMMAND make -C /lib/modules/${KERNEL_RELEASE}/build M=${MODULE_SOURCE_DIR} clean
            WORKING_DIRECTORY ${MODULE_SOURCE_DIR}
    )
else()
    add_compile_definitions(__RAM_VERSION__)
    # build tests
    message(STATUS "Configuring for USERSPACE tests (Google Test)...")

    include(FetchContent)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/heads/main.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    add_executable(yufs_test
            tests/main_test.cpp
            ${CORE_SRC}
    )
    target_link_libraries(yufs_test PRIVATE GTest::gtest_main)
    target_compile_definitions(yufs_test PRIVATE VTFS_USERSPACE)
endif()