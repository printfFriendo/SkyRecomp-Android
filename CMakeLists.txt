cmake_minimum_required(VERSION 3.22)
project(SkyRecomp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (ANDROID)
    add_compile_definitions(__ANDROID__ VK_USE_PLATFORM_ANDROID_KHR)
    add_compile_options(-fPIC -Wno-format -Wno-unused-parameter -Wno-unused-variable)
endif()

# Global includes
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/hle
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/nvn
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/nvn/maxwell
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/nvn/vulkan
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/services
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reshade/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/Textures
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xxhash
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/shaderc/include
)

# -------------------------------------------------------------
# Target 1: skygame (libskygame.so)
# -------------------------------------------------------------
file(GLOB_RECURSE GEN_SRC "generated/*.c")

set(SKYGAME_SOURCES
    tools/gamelib_entry.c
    tools/subsdk8_empty.c
)

if (GEN_SRC)
    list(APPEND SKYGAME_SOURCES ${GEN_SRC})
else()
    list(APPEND SKYGAME_SOURCES tools/skygame_stubs.c)
endif()

add_library(skygame SHARED ${SKYGAME_SOURCES})
set_target_properties(skygame PROPERTIES OUTPUT_NAME "skygame")
target_include_directories(skygame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

if (ANDROID)
    target_compile_options(skygame PRIVATE
        -O3
        -march=armv8-a+simd+crc
        -fomit-frame-pointer
        -fno-strict-aliasing
        -fPIC
        -Wno-return-type
        -Wno-unused-label
    )
endif()

# -------------------------------------------------------------
# Target 2: main / skyrecomp (libmain.so / skyrecomp)
# -------------------------------------------------------------
file(GLOB RT_CORE_C
    "runtime/*.c"
    "runtime/hle/*.c"
    "runtime/services/*.c"
    "runtime/nvn/*.c"
    "runtime/nvn/maxwell/*.c"
    "runtime/nvn/vulkan/*.c"
)
file(GLOB RT_CORE_CPP
    "runtime/nvn/vulkan/*.cpp"
)

set(RT_CORE_SOURCES ${RT_CORE_C} ${RT_CORE_CPP})

if (ANDROID OR CMAKE_SYSTEM_NAME MATCHES "Linux")
    list(APPEND RT_CORE_SOURCES "runtime/platform/linux.c")
elseif (WIN32)
    list(APPEND RT_CORE_SOURCES "runtime/platform/windows.c")
elseif (APPLE)
    list(APPEND RT_CORE_SOURCES "runtime/platform/macos.c")
endif()

file(GLOB UI_MM "runtime/ui/*.mm")
file(GLOB UI_CPP "runtime/ui/*.cpp")
set(UI_SOURCES ${UI_MM} ${UI_CPP})
set_source_files_properties(${UI_MM} PROPERTIES LANGUAGE CXX)

set(IMGUI_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_sdl3.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
)

set(ALL_MAIN_SOURCES
    ${RT_CORE_SOURCES}
    ${UI_SOURCES}
    ${IMGUI_SOURCES}
)

if (ANDROID)
    add_library(SDL3_imported SHARED IMPORTED)
    set_target_properties(SDL3_imported PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3/lib/${ANDROID_ABI}/libSDL3.so"
    )

    add_library(shaderc_imported STATIC IMPORTED)
    set_target_properties(shaderc_imported PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/shaderc/lib/${ANDROID_ABI}/libshaderc.a"
    )

    add_library(main SHARED ${ALL_MAIN_SOURCES})
    set_target_properties(main PROPERTIES OUTPUT_NAME "main")
    add_library(skyrecomp ALIAS main)

    find_library(LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(VULKAN_LIB vulkan)
    find_library(DL_LIB dl)
    find_library(M_LIB m)

    target_link_libraries(main PRIVATE
        SDL3_imported
        shaderc_imported
        ${VULKAN_LIB}
        ${LOG_LIB}
        ${ANDROID_LIB}
        ${DL_LIB}
        ${M_LIB}
    )

    target_link_options(main PRIVATE -Wl,-z,nostart-stop-gc)
else()
    add_executable(skyrecomp ${ALL_MAIN_SOURCES})
endif()
