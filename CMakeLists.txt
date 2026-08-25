cmake_minimum_required(VERSION 3.18)
project(odpar_territorial_domain_greenfield LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(ODG_ENGINE_SOURCES
    engine/src/core.c
    engine/src/world.c
    engine/src/player.c
    engine/src/service.c
    engine/src/raster.c
    engine/src/android_surface.c
    engine/src/host_api.c
)

add_library(odpar_greenfield SHARED ${ODG_ENGINE_SOURCES})
target_include_directories(odpar_greenfield PRIVATE engine/include engine/src)
target_compile_definitions(odpar_greenfield PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(odpar_greenfield PRIVATE
    -fno-builtin
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror
    $<$<CONFIG:Release>:-O3>
    $<$<NOT:$<CONFIG:Release>>:-O1>
)

if(ANDROID)
    find_library(ANDROID_LIB android REQUIRED)
    target_link_libraries(odpar_greenfield PRIVATE ${ANDROID_LIB} m)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(odpar_greenfield PRIVATE Threads::Threads m)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(odpar_greenfield PRIVATE -fvisibility=hidden)
    target_link_options(odpar_greenfield PRIVATE
        "LINKER:--no-undefined"
        "LINKER:-z,relro"
        "LINKER:-z,now"
        "LINKER:-z,noexecstack"
    )
    if(ANDROID)
        target_link_options(odpar_greenfield PRIVATE
            "LINKER:-z,max-page-size=16384"
            "LINKER:-z,common-page-size=16384"
        )
    endif()
endif()
