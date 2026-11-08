cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
endif()

project(AnimationMotionFix VERSION 1.3.2 LANGUAGES CXX RC)
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not allowed.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(COMMONLIBSSE_NG_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CommonLibSSE-NG-ng" CACHE PATH "Local CommonLibSSE-NG source directory")
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Build for SE" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Build for AE" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Build for VR" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Build CommonLibSSE tests")
set(COMMONLIB_PREBUILT OFF CACHE STRING "Build the selected local CommonLibSSE sources" FORCE)
add_subdirectory("${COMMONLIBSSE_NG_PATH}" CommonLibSSE EXCLUDE_FROM_ALL)
include("${COMMONLIBSSE_NG_PATH}/cmake/CommonLibSSE.cmake")
find_package(xbyak CONFIG REQUIRED)
find_path(SIMPLEINI_INCLUDE_DIR SimpleIni.h REQUIRED)

configure_file(cmake/Plugin.h.in cmake/Plugin.h @ONLY)
configure_file(cmake/version.rc.in cmake/version.rc @ONLY)
add_commonlibsse_plugin(${PROJECT_NAME}
	AUTHOR "Maxsu and SkyHorizon"
	MINIMUM_SKSE_VERSION 2.0.20
	COMPATIBLE_RUNTIMES 1.5.97.0 1.6.1170.0
	STRUCT_DEPENDENT
	SOURCES
		src/main.cpp
		src/Hooks.cpp
		src/HookSites.cpp
		src/Settings.cpp
		"${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc"
)
target_include_directories(${PROJECT_NAME} PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/cmake" "${SIMPLEINI_INCLUDE_DIR}")
target_link_libraries(${PROJECT_NAME} PRIVATE xbyak::xbyak)
target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)
if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
endif()
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION SKSE/Plugins)
install(FILES AnimationMotionFix.ini DESTINATION SKSE/Plugins)
