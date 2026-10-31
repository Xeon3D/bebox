/*
 * BeBox emulator - the POSIX file calls the core uses, on every host.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "Platform.h"

#include <cstdio>

#ifdef _WIN32

#include <cerrno>
#include <sys/time.h>
#include <sys/utime.h>
#include <windows.h>

namespace {

// Disk images and shared files are binary: no CR/LF translation and no
// end of file at ^Z for the descriptors open() returns.
struct BinaryFileMode {
	BinaryFileMode() { _set_fmode(_O_BINARY); }
} sBinaryFileMode;


ssize_t
PositionedIo(int fd, void* buffer, size_t length, off_t offset, bool write)
{
	HANDLE handle = (HANDLE)_get_osfhandle(fd);
	if (handle == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}
	OVERLAPPED overlapped = {};
	overlapped.Offset = (DWORD)((unsigned long long)offset & 0xffffffff);
	overlapped.OffsetHigh = (DWORD)((unsigned long long)offset >> 32);
	DWORD count = length > 0x40000000 ? 0x40000000 : (DWORD)length;
	DWORD done = 0;
	BOOL ok = write ? WriteFile(handle, buffer, count, &done, &overlapped)
		: ReadFile(handle, buffer, count, &done, &overlapped);
	if (!ok) {
		DWORD error = GetLastError();
		if (!write && error == ERROR_HANDLE_EOF)
			return 0;
		errno = error == ERROR_DISK_FULL ? ENOSPC
			: error == ERROR_ACCESS_DENIED ? EACCES : EIO;
		return -1;
	}
	return (ssize_t)done;
}

}	// namespace


ssize_t
pread(int fd, void* buffer, size_t length, off_t offset)
{
	return PositionedIo(fd, buffer, length, offset, false);
}


ssize_t
pwrite(int fd, const void* buffer, size_t length, off_t offset)
{
	return PositionedIo(fd, (void*)buffer, length, offset, true);
}


int
fsync(int fd)
{
	return _commit(fd);
}


int
utimes(const char* path, const struct timeval times[2])
{
	struct __utimbuf64 buffer;
	buffer.actime = times[0].tv_sec;
	buffer.modtime = times[1].tv_sec;
	return _utime64(path, &buffer);
}

#endif	// _WIN32


namespace bebox {

int
RenameReplacing(const char* from, const char* to)
{
#ifdef _WIN32
	if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING
			| MOVEFILE_WRITE_THROUGH))
		return 0;
	errno = GetLastError() == ERROR_ACCESS_DENIED ? EACCES : EIO;
	return -1;
#else
	return rename(from, to);
#endif
}

}	// namespace bebox
