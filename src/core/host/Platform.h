/*
 * BeBox emulator - the POSIX file calls the core uses, on every host.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * On Haiku and Linux this only includes the POSIX headers. On Windows
 * (MinGW-w64, built with _FILE_OFFSET_BITS=64) it adds what the C runtime
 * lacks: positioned reads and writes, fsync(), lstat() (Windows has no
 * symbolic links the core cares about), mkdir() with a mode, gmtime_r()
 * and utimes(). File descriptors are binary there (Platform.cpp sets the
 * default mode), and paths are UTF-8 (the application manifest's active
 * code page), so the core passes paths unchanged everywhere.
 */
#ifndef BEBOX_PLATFORM_H
#define BEBOX_PLATFORM_H

#include <cstddef>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _WIN32

#include <direct.h>
#include <io.h>

struct timeval;

ssize_t pread(int fd, void* buffer, size_t length, off_t offset);
ssize_t pwrite(int fd, const void* buffer, size_t length, off_t offset);
int fsync(int fd);
int utimes(const char* path, const struct timeval times[2]);

inline int
lstat(const char* path, struct stat* st)
{
	return stat(path, st);
}


inline int
mkdir(const char* path, int mode)
{
	return _mkdir(path);
}


#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
inline struct tm*
gmtime_r(const time_t* value, struct tm* parts)
{
	return gmtime_s(parts, value) == 0 ? parts : nullptr;
}
#endif

#ifndef O_NOCTTY
#	define O_NOCTTY 0
#endif
#ifndef O_NONBLOCK
#	define O_NONBLOCK 0
#endif

#endif	// _WIN32

namespace bebox {

// rename(), replacing an existing file at the new path as POSIX's does
// (Windows' own refuses).
int RenameReplacing(const char* from, const char* to);

// The modification time of a stat() result in nanoseconds (in seconds
// on hosts without the nanoseconds).
inline long long
StatModifiedNs(const struct stat& st)
{
#ifdef _WIN32
	return (long long)st.st_mtime * 1000000000;
#else
	return (long long)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
}

}	// namespace bebox

#endif	// BEBOX_PLATFORM_H
