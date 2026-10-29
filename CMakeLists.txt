cmake_minimum_required(VERSION 4.4)
project(vk-AutoHDR LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
	message(FATAL_ERROR "GCC >= 16 required, found ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

set(RESHADE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../reshade" CACHE PATH "ReShade fork checkout (branch linux-vulkan)")
if(NOT EXISTS "${RESHADE_SOURCE_DIR}/include/reshade.hpp")
	message(FATAL_ERROR "RESHADE_SOURCE_DIR does not contain include/reshade.hpp: ${RESHADE_SOURCE_DIR}")
endif()

add_library(autohdr_warnings INTERFACE)
target_compile_options(autohdr_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror -Wno-changes-meaning)

add_library(autohdr_core STATIC
	src/decide.cpp
	src/config.cpp
	src/edid.cpp
	src/kwin_config.cpp
	src/display_info.cpp
	src/calibration.cpp
	src/advice.cpp
	src/wayland_hint.cpp
	src/surface_report.cpp
)
set_target_properties(autohdr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(autohdr_core PUBLIC src)
target_include_directories(autohdr_core SYSTEM PUBLIC "${RESHADE_SOURCE_DIR}/include")
target_link_libraries(autohdr_core PUBLIC autohdr_warnings)

enable_testing()
set(AUTOHDR_PUMBO_FX "$ENV{HOME}/.local/share/reshade/reshade-shaders/Shaders/Pumbo/AdvancedAutoHDR.fx" CACHE FILEPATH "Installed Pumbo AdvancedAutoHDR.fx (name check skipped if missing)")

function(autohdr_add_test name)
	add_executable(${name} tests/${name}.cpp)
	target_link_libraries(${name} PRIVATE autohdr_core)
	target_compile_options(${name} PRIVATE -UNDEBUG)
	target_compile_definitions(${name} PRIVATE AUTOHDR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
	add_test(NAME ${name} COMMAND ${name})
endfunction()

foreach(t test_decide test_config test_edid test_kwin_config test_display_info test_calibration test_advice test_wayland_hint test_surface_report)
	autohdr_add_test(${t})
endforeach()

if(NOT EXISTS "${AUTOHDR_PUMBO_FX}")
	message(STATUS "Pumbo shader not found at ${AUTOHDR_PUMBO_FX}: skipping Pumbo check")
endif()
autohdr_add_test(test_shader_names)
target_compile_definitions(test_shader_names PRIVATE
	AUTOHDR_PUMBO_FX="${AUTOHDR_PUMBO_FX}"
	AUTOHDR_UI_FX="${CMAKE_CURRENT_SOURCE_DIR}/shaders/AutoHDR_UI.fx"
	AUTOHDR_PRESET="${CMAKE_CURRENT_SOURCE_DIR}/presets/AutoHDR.ini")

set(RESHADE_LIBRARY "${RESHADE_SOURCE_DIR}/build-linux/ReShade64.so" CACHE FILEPATH "Fork's ReShade64.so the add-on links against")
if(NOT EXISTS "${RESHADE_LIBRARY}")
	message(FATAL_ERROR "RESHADE_LIBRARY not found: ${RESHADE_LIBRARY} (build the fork first)")
endif()
add_library(reshade_imported SHARED IMPORTED)
set_target_properties(reshade_imported PROPERTIES IMPORTED_LOCATION "${RESHADE_LIBRARY}")

find_package(PkgConfig REQUIRED)
# 1.23: wl_display_create_queue_with_name; 1.47: color-management-v1 v2 (ready2, preferred_changed2).
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client>=1.23)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.47)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(CM_XML "${WAYLAND_PROTOCOLS_DIR}/staging/color-management/color-management-v1.xml")
if(NOT WAYLAND_SCANNER OR NOT EXISTS "${CM_XML}")
	message(FATAL_ERROR "wayland-scanner or wayland-protocols (staging/color-management/color-management-v1.xml) not found")
endif()
set(CM_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-gen")
file(MAKE_DIRECTORY "${CM_GEN_DIR}")
add_custom_command(
	OUTPUT "${CM_GEN_DIR}/color-management-v1-client-protocol.h" "${CM_GEN_DIR}/color-management-v1-protocol.c"
	COMMAND "${WAYLAND_SCANNER}" client-header "${CM_XML}" "${CM_GEN_DIR}/color-management-v1-client-protocol.h"
	COMMAND "${WAYLAND_SCANNER}" private-code "${CM_XML}" "${CM_GEN_DIR}/color-management-v1-protocol.c"
	DEPENDS "${CM_XML}"
	VERBATIM)
# Generated C code: no -Werror, and hidden symbols so its *_interface objects never interpose the host process's own.
add_library(autohdr_wl_protocol STATIC "${CM_GEN_DIR}/color-management-v1-protocol.c")
set_target_properties(autohdr_wl_protocol PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_include_directories(autohdr_wl_protocol SYSTEM PUBLIC "${CM_GEN_DIR}")
target_link_libraries(autohdr_wl_protocol PUBLIC PkgConfig::WAYLAND_CLIENT)

add_library(vk-AutoHDR MODULE src/addon.cpp src/reshade_config_store.cpp src/binder.cpp src/overlay.cpp src/surface_watcher.cpp)
set_target_properties(vk-AutoHDR PROPERTIES
	PREFIX ""
	OUTPUT_NAME vk-AutoHDR
	SUFFIX .addon64
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON)
target_compile_definitions(vk-AutoHDR PRIVATE RESHADE_API_LIBRARY=1 ImTextureID=ImU64)
target_include_directories(vk-AutoHDR SYSTEM PRIVATE "${RESHADE_SOURCE_DIR}/deps/imgui")
target_link_libraries(vk-AutoHDR PRIVATE autohdr_core autohdr_warnings reshade_imported autohdr_wl_protocol)
target_link_options(vk-AutoHDR PRIVATE -Wl,--no-undefined)
install(TARGETS vk-AutoHDR LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/reshade)

install(FILES shaders/AutoHDR_UI.fx DESTINATION ${CMAKE_INSTALL_DATADIR}/reshade/reshade-shaders/Shaders/AutoHDR)
install(FILES presets/AutoHDR.ini DESTINATION ${CMAKE_INSTALL_DATADIR}/reshade/presets)

add_executable(watch_probe EXCLUDE_FROM_ALL tools/watch_probe.cpp src/surface_watcher.cpp)
target_include_directories(watch_probe PRIVATE src)
target_link_libraries(watch_probe PRIVATE autohdr_core autohdr_warnings autohdr_wl_protocol)
