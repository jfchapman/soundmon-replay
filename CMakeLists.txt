cmake_minimum_required(VERSION 3.16)
project(SoundMonReplay VERSION 0.1.0 LANGUAGES C CXX)

option(SMON_BUILD_SHARED "Build a shared library (DLL / .so / .dylib) instead of a static one" OFF)
option(SMON_BUILD_TOOLS "Build smonrender and smontrace" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

set(SMON_SOURCES
	src/module.cpp
	src/paula.cpp
	src/replayer.cpp
	src/soundmonreplay.cpp
)

if(SMON_BUILD_SHARED)
	add_library(soundmonreplay SHARED ${SMON_SOURCES})
	target_compile_definitions(soundmonreplay PUBLIC SMON_SHARED PRIVATE SMON_BUILDING)
	set_target_properties(soundmonreplay PROPERTIES CXX_VISIBILITY_PRESET hidden VERSION ${PROJECT_VERSION} SOVERSION 0)
else()
	add_library(soundmonreplay STATIC ${SMON_SOURCES})
endif()
add_library(SoundMonReplay::soundmonreplay ALIAS soundmonreplay)

target_include_directories(soundmonreplay
	PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
	PRIVATE src)
set_target_properties(soundmonreplay PROPERTIES PUBLIC_HEADER include/soundmonreplay.h)

if(MSVC)
	target_compile_options(soundmonreplay PRIVATE /W4)
	set_property(TARGET soundmonreplay PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
	target_compile_options(soundmonreplay PRIVATE -Wall -Wextra)
endif()

if(SMON_BUILD_TOOLS)
	add_executable(smonrender tools/smonrender.c)
	target_link_libraries(smonrender PRIVATE soundmonreplay)
	# smontrace uses the library's internal classes, which a shared library doesn't export, so
	# it builds the sources itself in that case.
	if(SMON_BUILD_SHARED)
		add_executable(smontrace tools/smontrace.cpp ${SMON_SOURCES})
		target_include_directories(smontrace PRIVATE include src)
	else()
		add_executable(smontrace tools/smontrace.cpp)
		target_link_libraries(smontrace PRIVATE soundmonreplay)
		target_include_directories(smontrace PRIVATE src)
	endif()
	if(MSVC)
		set_property(TARGET smonrender smontrace PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		target_compile_definitions(smonrender PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_definitions(smontrace PRIVATE _CRT_SECURE_NO_WARNINGS)
	endif()
endif()

include(GNUInstallDirs)
install(TARGETS soundmonreplay EXPORT SoundMonReplayTargets
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT SoundMonReplayTargets NAMESPACE SoundMonReplay:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SoundMonReplay)
include(CMakePackageConfigHelpers)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SoundMonReplayConfig.cmake
	"include(\"\${CMAKE_CURRENT_LIST_DIR}/SoundMonReplayTargets.cmake\")\n")
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/SoundMonReplayConfigVersion.cmake COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SoundMonReplayConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/SoundMonReplayConfigVersion.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SoundMonReplay)
if(SMON_BUILD_TOOLS)
	install(TARGETS smonrender RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
