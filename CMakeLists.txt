cmake_minimum_required(VERSION 3.25)

# Necola-ADS — L4N plugin (necola_ads.dll).
#
# Parallel build definition to xmake.lua, intended for the faster
# CMake + Ninja + sccache toolchain while keeping MSVC as the compiler.
# Same target, same sources, same dependencies.
project(necola_ads LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "necola_ads must be built with MSVC")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "necola_ads must be built for x86 (L4D2 is 32-bit)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# UTF-8 sources (the code base is full of Chinese comments/strings).
add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /MP)

# ---------------------------------------------------------------- dependencies
# spdlog: vcpkg (x86-windows) exports a CMake package.
find_package(spdlog CONFIG REQUIRED)

# minhook / inipp: vcpkg ports without a CMake package file, so locate them
# through the vcpkg include/lib directories that the toolchain file exports.
find_path(MINHOOK_INCLUDE_DIR MinHook.h HINTS ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include)
find_library(MINHOOK_LIBRARY NAMES minhook HINTS ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib)
find_path(INIPP_INCLUDE_DIR inipp.h HINTS ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include)
if(NOT MINHOOK_INCLUDE_DIR OR NOT INIPP_INCLUDE_DIR)
  message(FATAL_ERROR "minhook/inipp headers not found; run: vcpkg install minhook inipp --triplet x86-windows")
endif()

# ---------------------------------------------------------------------- target
file(GLOB_RECURSE NECOLA_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/necola/hook/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/necola/sdk/*.cpp"
)
list(APPEND NECOLA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/necola/dllmain.cpp")

add_library(necola_ads SHARED ${NECOLA_SOURCES})

target_include_directories(necola_ads PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/necola"
  "${MINHOOK_INCLUDE_DIR}"
  "${INIPP_INCLUDE_DIR}"
)

target_link_libraries(necola_ads PRIVATE spdlog::spdlog user32 gdi32)
if(MINHOOK_LIBRARY)
  target_link_libraries(necola_ads PRIVATE "${MINHOOK_LIBRARY}")
else()
  message(FATAL_ERROR "minhook library not found")
endif()

set_target_properties(necola_ads PROPERTIES
  OUTPUT_NAME "necola_ads"
  PREFIX ""
  SUFFIX ".dll"
  MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
)
