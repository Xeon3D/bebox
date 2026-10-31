# BeBox emulator: CMake build for Windows (MSYS2 UCRT64 / MinGW-w64),
# Linux, macOS and Haiku. (The native Haiku application builds with the
# Makefile, the Linux tests also with tests/linux.)
#
#   cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# Builds the Qt application (BeBox, BeBoxQt.exe on Windows) when Qt 6 is
# found, bebox-headless, bebox-img and the tests; on Windows also the Win32
# application (BeBox.exe). On Windows all but the Qt application link
# statically (no MSYS2 DLLs needed).

cmake_minimum_required(VERSION 3.20)
project(BeBox VERSION 1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

option(BEBOX_TESTS "Build the core's test programs" ON)
option(BEBOX_NETWORK "User mode networking with libslirp" ON)
option(BEBOX_QT "The Qt application (when Qt 6 is found)" ON)

# -ffp-contract=off: the PowerPC FPU rounds each multiply and add itself.
add_compile_options(-ffp-contract=off -Wall -Wextra -Wno-unused-parameter)
# 32-bit x86 (Haiku's x86 architecture, i586 by default): doubles in SSE2,
# as on x86-64, not in the x87's 80-bit registers, which round twice.
if(CMAKE_SIZEOF_VOID_P EQUAL 4
	AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|BePC)$")
	add_compile_options(-msse2 -mfpmath=sse)
endif()
add_compile_definitions(_FILE_OFFSET_BITS=64)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(bebox_core STATIC ${CORE_SOURCES})
target_include_directories(bebox_core PUBLIC src)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
target_link_libraries(bebox_core PUBLIC Threads::Threads)

# Windows: an executable that needs no MSYS2 DLLs.
function(bebox_link_statically target)
	if(WIN32)
		target_link_options(${target} PRIVATE -static -static-libgcc
			-static-libstdc++)
	endif()
endfunction()

if(WIN32)
	# libslirp and GLib as static libraries, with their system libraries.
	target_link_libraries(bebox_core PUBLIC ${ZLIB_STATIC_LIBRARIES})
	target_link_directories(bebox_core PUBLIC ${ZLIB_STATIC_LIBRARY_DIRS})
	target_link_libraries(bebox_core PUBLIC ws2_32)
else()
	target_link_libraries(bebox_core PUBLIC PkgConfig::ZLIB)
endif()
if(HAIKU)
	# sockets (the TCP serial ports, libslirp's addresses) are in libnetwork
	target_link_libraries(bebox_core PUBLIC network)
endif()

if(BEBOX_NETWORK)
	pkg_check_modules(SLIRP IMPORTED_TARGET slirp)
	if(SLIRP_FOUND)
		target_compile_definitions(bebox_core PUBLIC HAVE_SLIRP)
		if(WIN32)
			target_compile_definitions(bebox_core PUBLIC LIBSLIRP_STATIC
				GLIB_STATIC_COMPILATION)
			target_include_directories(bebox_core PUBLIC
				${SLIRP_STATIC_INCLUDE_DIRS})
			target_link_directories(bebox_core PUBLIC
				${SLIRP_STATIC_LIBRARY_DIRS})
			target_link_libraries(bebox_core PUBLIC ${SLIRP_STATIC_LIBRARIES})
		else()
			target_link_libraries(bebox_core PUBLIC PkgConfig::SLIRP)
		endif()
	else()
		message(STATUS "libslirp not found: no networking")
	endif()
endif()

# What the Windows and Qt front ends share: settings and profiles.
add_library(bebox_frontend STATIC src/frontend/CommandLine.cpp
	src/frontend/JobQueue.cpp src/frontend/Settings.cpp)
target_link_libraries(bebox_frontend PUBLIC bebox_core)

add_executable(bebox-headless src/headless/main.cpp)
target_link_libraries(bebox-headless PRIVATE bebox_core)
bebox_link_statically(bebox-headless)

add_executable(bebox-img tools/bebox-img/main.cpp)
target_link_libraries(bebox-img PRIVATE bebox_core)
bebox_link_statically(bebox-img)

if(WIN32)
	enable_language(RC)
	add_subdirectory(src/win32)
	bebox_link_statically(BeBox)
endif()

if(BEBOX_QT)
	find_package(Qt6 6.5 QUIET COMPONENTS Widgets)
	if(Qt6Widgets_FOUND)
		add_subdirectory(src/qt)
	else()
		message(STATUS "Qt 6 not found: no Qt application")
	endif()
endif()

if(BEBOX_TESTS)
	foreach(test ppc-exec cpu-unit-tests cpu-bench qcow2-tests device-tests
			bfs-tests hobbit-tests)
		add_executable(${test} tests/linux/${test}.cpp)
		target_link_libraries(${test} PRIVATE bebox_core)
		bebox_link_statically(${test})
	endforeach()
endif()
