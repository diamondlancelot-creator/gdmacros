cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(GDSpeedMacro VERSION 1.0.0)

add_library(${PROJECT_NAME} SHARED src/main.cpp)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK belum diset. Install Geode CLI & SDK dulu (lihat README).")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Library format replay GDR2 (dipakai bot-bot lain). Letakkan SETELAH add_subdirectory
# dan SEBELUM setup_geode_mod. Untuk build yang bisa diulang, ganti "gdr2" dengan commit hash.
CPMAddPackage("gh:maxnut/GDReplayFormat#gdr2")
target_link_libraries(${PROJECT_NAME} libGDR)

setup_geode_mod(${PROJECT_NAME})
