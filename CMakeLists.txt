cmake_minimum_required(VERSION 3.20)
project(BMUSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BMS_ENABLE_LUA "Build the experimental Lua session" OFF)

# ── PCAN-Basic SDK (required) ───────────────────────────────────────────────
# Download from https://www.peak-system.com/PCAN-Basic.239.0.html
# Extract the ZIP and set PCAN_SDK_DIR to the root folder (contains Include/ and x64/).
set(PCAN_SDK_DIR "" CACHE PATH "Path to PCAN-Basic SDK root (must contain Include/PCANBasic.h)")

# ── ImGui (Win32 + DirectX 11 backend, docking branch) ──────────────────────
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# ── ImPlot ───────────────────────────────────────────────────────────────────
set(IMPLOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot)
add_library(implot STATIC
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${IMPLOT_DIR})
target_link_libraries(implot PUBLIC imgui)

# ── dbcppp ───────────────────────────────────────────────────────────────────
set(build_tests    OFF CACHE BOOL "" FORCE)
set(build_examples OFF CACHE BOOL "" FORCE)
set(build_tools    OFF CACHE BOOL "" FORCE)
set(build_kcd      OFF CACHE BOOL "" FORCE)

# dbcppp hardcodes add_library(dbcppp SHARED ...) but its Export.h never gets
# DBCPPP_EXPORT defined when building, making every symbol dllimport.
# Intercept add_library to force a static build instead.
macro(add_library name)
    set(_bmu_args ${ARGN})
    if("${name}" STREQUAL "dbcppp")
        list(REMOVE_ITEM _bmu_args SHARED MODULE)
        _add_library(${name} STATIC ${_bmu_args})
    else()
        _add_library(${name} ${_bmu_args})
    endif()
endmacro()

include(cmake/PatchDbcppp.cmake)
add_subdirectory(third_party/dbcppp)

# Define DBCPPP_EXPORT so that DBCPPP_API expands to dllexport rather than
# dllimport. On a static lib dllexport is harmless; without it the build fails.
target_compile_definitions(dbcppp PUBLIC DBCPPP_EXPORT)

# MinGW + GCC 15 workaround: on Windows the filesystem is case-insensitive, so
# #include <signal.h> (issued deep inside GCC's gthr-default.h → pthread.h)
# resolves to dbcppp's include/dbcppp/Signal.h instead of the system header.
# That causes a circular include that breaks shared_ptr_base.h / concurrence.h.
# Fix: replace INCLUDE_DIRECTORIES with an explicit list that omits include/dbcppp/
# (dbcppp's own headers use relative/quoted includes, so they don't need it;
# consumers use #include <dbcppp/Signal.h> which only needs the include/ parent.)
set_property(TARGET dbcppp PROPERTY INCLUDE_DIRECTORIES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dbcppp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dbcppp/third-party/boost"
)
set_property(TARGET dbcppp PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dbcppp/include"
)

# ── PCAN-Basic availability check ────────────────────────────────────────────
if(NOT PCAN_SDK_DIR)
    # Auto-detect standard PEAK-System installation on Windows
    set(_pcan_default "C:/Program Files/PEAK-System/PCAN-Basic")
    if(EXISTS "${_pcan_default}/Include/PCANBasic.h")
        set(PCAN_SDK_DIR "${_pcan_default}" CACHE PATH "Path to PCAN-Basic SDK root" FORCE)
    endif()
    unset(_pcan_default)
endif()
if(NOT EXISTS "${PCAN_SDK_DIR}/Include/PCANBasic.h")
    message(FATAL_ERROR "PCAN-Basic SDK not found. Set PCAN_SDK_DIR to the root folder containing Include/PCANBasic.h.\nDownload from https://www.peak-system.com/PCAN-Basic.239.0.html")
endif()
message(STATUS "PCAN-Basic SDK found: ${PCAN_SDK_DIR}")

# ── Embed DBC file at configure time ────────────────────────────────────────
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/bms_main_v1.dbc"
    "${CMAKE_CURRENT_SOURCE_DIR}/BMS2000_Epiroc_0x300-0.4.0+0.dbc")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/bms_main_v1.dbc" EMBEDDED_DBC_CONTENT)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/bms_main_v1.dbc" EMBEDDED_FAULT_DBC_CONTENT)
file(CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedDbc.h"
    CONTENT
"// Auto-generated from bms_main_v1.dbc — do not edit
#pragma once

namespace EmbeddedDbc {
static const char kContent[] = R\"DBCRAW(@EMBEDDED_DBC_CONTENT@)DBCRAW\";
} // namespace EmbeddedDbc
"
    @ONLY
    NEWLINE_STYLE LF)

file(CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedFaultDbc.h"
    CONTENT
"// Auto-generated from bms_main_v1.dbc — do not edit
#pragma once

namespace EmbeddedFaultDbc {
static const char kContent[] = R\"DBCRAW(@EMBEDDED_FAULT_DBC_CONTENT@)DBCRAW\";
} // namespace EmbeddedFaultDbc
"
    @ONLY
    NEWLINE_STYLE LF)

# ── Application ──────────────────────────────────────────────────────────────
set(APP_SOURCES
    src/main.cpp
    src/App.cpp
    src/can/CanBus.cpp
    src/can/CyclicCanSender.cpp
    src/can/VirtualInterface.cpp
    src/dbc/DbcModel.cpp
    src/ui/BusMonitorPanel.cpp
    src/ui/SystemPanel.cpp
    src/ui/FaultPanel.cpp
    src/ui/ParametersPanel.cpp
    src/ui/PlotPanel.cpp
)

list(APPEND APP_SOURCES src/can/PcanInterface.cpp)

add_executable(BMUSimulator WIN32 ${APP_SOURCES})

if(BMS_ENABLE_LUA)
    include(cmake/LuaSession.cmake)
    target_link_libraries(BMUSimulator PRIVATE bms_lua_session)
endif()

target_include_directories(BMUSimulator PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(BMUSimulator PRIVATE
    imgui
    implot
    dbcppp
    d3d11
    d3dcompiler
    dxgi
    dwmapi
)

target_include_directories(BMUSimulator PRIVATE "${PCAN_SDK_DIR}/Include")

# The PCAN SDK ships only MSVC .lib files which MinGW ld cannot use.
# Generate a MinGW-compatible import library from the x64 DLL at configure time.
find_program(GENDEF_EXECUTABLE  gendef  REQUIRED)
find_program(DLLTOOL_EXECUTABLE dlltool REQUIRED)
set(_pcan_dll    "${PCAN_SDK_DIR}/x64/PCANBasic.dll")
set(_pcan_def    "${CMAKE_CURRENT_BINARY_DIR}/PCANBasic.def")
set(_pcan_implib "${CMAKE_CURRENT_BINARY_DIR}/libPCANBasic.a")
if(NOT EXISTS "${_pcan_implib}")
    execute_process(
        COMMAND "${GENDEF_EXECUTABLE}" - "${_pcan_dll}"
        OUTPUT_FILE "${_pcan_def}"
        RESULT_VARIABLE _gendef_ret)
    if(_gendef_ret)
        message(FATAL_ERROR "gendef failed for ${_pcan_dll}")
    endif()
    execute_process(
        COMMAND "${DLLTOOL_EXECUTABLE}" -d "${_pcan_def}" -D "PCANBasic.dll" -l "${_pcan_implib}"
        RESULT_VARIABLE _dlltool_ret)
    if(_dlltool_ret)
        message(FATAL_ERROR "dlltool failed to create ${_pcan_implib}")
    endif()
endif()
target_link_libraries(BMUSimulator PRIVATE "${_pcan_implib}")

# ── Copy assets and DLL to build output directory ────────────────────────────
add_custom_command(TARGET BMUSimulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/RobotoMono-Regular.ttf"
        "$<TARGET_FILE_DIR:BMUSimulator>/RobotoMono-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PCAN_SDK_DIR}/x64/PCANBasic.dll"
        "$<TARGET_FILE_DIR:BMUSimulator>/PCANBasic.dll"
)
