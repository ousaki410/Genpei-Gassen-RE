# ===========================================================================
# Genpei Kassen RE — native Linux build (GCC / Clang, SDL2 / SDL3)
#
#   cmake -S . -B build
#   cmake --build build
#
# Options:
#   -DYPHZ_ARCH=native|32|64        host ABI (-m32 / -m64); default native
#   -DYPHZ_USE_SDL3=ON              link SDL3 instead of SDL2 (opt-in)
#   -DYPHZ_PREFER_BUNDLED_SDL2=ON   ignore system SDL2, use third_party/
#   -DCMAKE_BUILD_TYPE=RelWithDebInfo
# ===========================================================================
cmake_minimum_required(VERSION 3.16)

project(GenpeiGassenRE
        VERSION 0.1.0
        DESCRIPTION "Native Linux port / reverse-engineering harness for Koei's Genpei Kassen (PC-98 DOS)"
        LANGUAGES C)

# ---------------------------------------------------------------------------
# C standard and default build type
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo"
        CACHE STRING "Build type (Debug/Release/RelWithDebInfo/MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------------------------------------------------------------------------
# Strict warnings (GCC / Clang)
# ---------------------------------------------------------------------------
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
else()
    message(WARNING "This project targets GCC/Clang; got '${CMAKE_C_COMPILER_ID}'.")
endif()

# ---------------------------------------------------------------------------
# Host word size: -m32 / -m64
#
# The DOS / PC-98 game data is 16-bit little-endian. On-disk records are
# modelled byte-for-byte with #pragma pack(push, 1) in include/*.h ("DOS layout"
# structs) so the host representation matches the original images regardless of
# the host ABI selected here.
# ---------------------------------------------------------------------------
set(YPHZ_ARCH "native" CACHE STRING "Host ABI: native (default) | 32 | 64")
set_property(CACHE YPHZ_ARCH PROPERTY STRINGS native 32 64)

if(YPHZ_ARCH STREQUAL "32")
    add_compile_options(-m32)
    add_link_options(-m32)
elseif(YPHZ_ARCH STREQUAL "64")
    add_compile_options(-m64)
    add_link_options(-m64)
elseif(NOT YPHZ_ARCH STREQUAL "native")
    message(FATAL_ERROR "YPHZ_ARCH must be one of: native, 32, 64")
endif()

# ---------------------------------------------------------------------------
# SDL dependency: SDL2 (default) or SDL3 (opt-in)
# ---------------------------------------------------------------------------
option(YPHZ_USE_SDL3 "Link against SDL3 instead of SDL2 (API migration pending)" OFF)
option(YPHZ_PREFER_BUNDLED_SDL2 "Force the vendored third_party/ SDL2 over a system copy" OFF)

# INTERFACE target carrying SDL include dirs, link libs and rpath to the engine.
add_library(yphz_sdl INTERFACE)

if(YPHZ_USE_SDL3)
    find_package(SDL3 QUIET CONFIG)
    if(NOT SDL3_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL3 QUIET sdl3)
        endif()
    endif()

    if(TARGET SDL3::SDL3)
        target_link_libraries(yphz_sdl INTERFACE SDL3::SDL3)
    elseif(SDL3_FOUND)
        target_include_directories(yphz_sdl INTERFACE ${SDL3_INCLUDE_DIRS})
        target_link_directories(yphz_sdl INTERFACE ${SDL3_LIBRARY_DIRS})
        target_link_libraries(yphz_sdl INTERFACE ${SDL3_LIBRARIES})
    else()
        message(FATAL_ERROR "YPHZ_USE_SDL3=ON but SDL3 was not found.")
    endif()
    target_compile_definitions(yphz_sdl INTERFACE YPHZ_SDL3=1)
else()
    # --- SDL2: prefer system, fall back to vendored third_party/ -----------
    set(_yphz_sdl2_found FALSE)

    if(NOT YPHZ_PREFER_BUNDLED_SDL2)
        find_package(SDL2 QUIET CONFIG)
        if(TARGET SDL2::SDL2)
            target_link_libraries(yphz_sdl INTERFACE SDL2::SDL2)
            set(_yphz_sdl2_found TRUE)
        else()
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(SDL2PC QUIET sdl2)
                if(SDL2PC_FOUND)
                    target_include_directories(yphz_sdl INTERFACE ${SDL2PC_INCLUDE_DIRS})
                    target_link_directories(yphz_sdl INTERFACE ${SDL2PC_LIBRARY_DIRS})
                    target_link_libraries(yphz_sdl INTERFACE ${SDL2PC_LINK_LIBRARIES})
                    set(_yphz_sdl2_found TRUE)
                endif()
            endif()
        endif()
    endif()

    if(NOT _yphz_sdl2_found)
        set(_yphz_bundled "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
        set(_yphz_bundled_lib "${_yphz_bundled}/lib/libSDL2.so")
        if(EXISTS "${_yphz_bundled}/include/SDL2/SDL.h" AND EXISTS "${_yphz_bundled_lib}")
            target_include_directories(yphz_sdl INTERFACE "${_yphz_bundled}/include/SDL2")
            target_link_libraries(yphz_sdl INTERFACE "${_yphz_bundled_lib}")
            target_link_options(yphz_sdl INTERFACE "-Wl,-rpath,${_yphz_bundled}/lib")
            message(STATUS "SDL2: using vendored copy under third_party/")
        else()
            message(FATAL_ERROR
                "SDL2 not found. Install libsdl2-dev, or keep the vendored "
                "third_party/include/SDL2 + third_party/lib/libSDL2.so tree.")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Engine target
# ---------------------------------------------------------------------------
add_executable(yphz_engine
    src/main.c
    src/hal_video.c
    src/input.c
    src/decompress.c
    src/message.c
    src/scenario.c
    src/turn.c
    src/npk.c)

target_include_directories(yphz_engine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(yphz_engine PRIVATE yphz_sdl)

# Keep the historical bin/yphz_engine location next to the game data tree.
set_target_properties(yphz_engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")

message(STATUS "host ABI .......... ${YPHZ_ARCH}")
message(STATUS "output binary ..... ${CMAKE_CURRENT_SOURCE_DIR}/bin/yphz_engine")
