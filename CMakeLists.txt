# BeBox emulator: CMake build for Windows (MSYS2 UCRT64 / MinGW-w64) and
# Linux. Haiku builds with the Makefile, Linux tests with tests/linux.
#
#   cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# On Windows everything links statically (no MSYS2 DLLs needed): BeBox.exe
# (the application), bebox-headless.exe, bebox-img.exe and the tests.

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

# -ffp-contract=off: the PowerPC FPU rounds each multiply and add itself.
add_compile_options(-ffp-contract=off -Wall -Wextra -Wno-unused-parameter)
add_compile_definitions(_FILE_OFFSET_BITS=64)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(bebox_core STATIC ${CORE_SOURCES})
target_include_directories(bebox_core PUBLIC src)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
target_link_libraries(bebox_core PUBLIC Threads::Threads)

if(WIN32)
	# libslirp and GLib as static libraries, with their system libraries.
	target_link_options(bebox_core PUBLIC -static -static-libgcc
		-static-libstdc++)
	target_link_libraries(bebox_core PUBLIC ${ZLIB_STATIC_LIBRARIES})
	target_link_directories(bebox_core PUBLIC ${ZLIB_STATIC_LIBRARY_DIRS})
	target_link_libraries(bebox_core PUBLIC ws2_32)
else()
	target_link_libraries(bebox_core PUBLIC PkgConfig::ZLIB)
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

add_executable(bebox-headless src/headless/main.cpp)
target_link_libraries(bebox-headless PRIVATE bebox_core)

add_executable(bebox-img tools/bebox-img/main.cpp)
target_link_libraries(bebox-img PRIVATE bebox_core)

if(WIN32)
	enable_language(RC)
	add_subdirectory(src/win32)
endif()

if(BEBOX_TESTS)
	foreach(test ppc-exec cpu-unit-tests cpu-bench qcow2-tests device-tests
			bfs-tests hobbit-tests)
		add_executable(${test} tests/linux/${test}.cpp)
		target_link_libraries(${test} PRIVATE bebox_core)
	endforeach()
endif()
