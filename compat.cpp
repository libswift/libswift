/*
 *  compat.cpp
 *  swift
 *
 *  Created by Arno Bakker, Victor Grishchenko
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#include "compat.h"
#include <sys/stat.h>
#include <stdio.h>
#include <assert.h>
#ifdef _WIN32
#include <tchar.h>
#include <io.h>
#include <sys/timeb.h>
#include <vector>
#include <stdexcept>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

namespace swift {

#ifdef _WIN32
static HANDLE map_handles[1024];
#endif

size_t file_size (int fd) {
    struct stat st;
    fstat(fd, &st);
    return st.st_size;
}

int     file_seek (int fd, size_t offset) {
#ifndef _WIN32
    return lseek(fd,offset,SEEK_SET);
#else
    return _lseek(fd,offset,SEEK_SET);
#endif
}

int     file_resize (int fd, size_t new_size) {
#ifndef _WIN32
    return ftruncate(fd, new_size);
#else
    // Arno, 2011-10-27: Use 64-bit version
    if (_chsize_s(fd,new_size) != 0)
    	return -1;
    else
    	return 0;
#endif
}

void print_error(const char* msg) {
    perror(msg);
#ifdef _WIN32
    int e = WSAGetLastError();
    if (e)
        fprintf(stderr,"windows error #%u\n",e);
#endif
}

void*   memory_map (int fd, size_t size) {
    if (!size)
        size = file_size(fd);
    void *mapping;
#ifndef _WIN32
    mapping = mmap (NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping==MAP_FAILED)
        return NULL;
    return mapping;
#else
    HANDLE fhandle = (HANDLE)_get_osfhandle(fd);
    assert(fd<1024);
    HANDLE maphandle = CreateFileMapping(     fhandle,
                                       NULL,
                                       PAGE_READWRITE,
                                       0,
                                       0,
                                       NULL    );
    if (maphandle == NULL)
        return NULL;
    map_handles[fd] = maphandle;

    mapping = MapViewOfFile         (  maphandle,
                                       FILE_MAP_WRITE,
                                       0,
                                       0,
                                       0  );

    return mapping;
#endif
}

void    memory_unmap (int fd, void* mapping, size_t size) {
#ifndef _WIN32
    munmap(mapping,size);
    close(fd);
#else
    UnmapViewOfFile(mapping);
    CloseHandle(map_handles[fd]);
#endif
}

#ifdef _WIN32

size_t pread(int fildes, void *buf, size_t nbyte, long offset)
{
    _lseek(fildes,offset,SEEK_SET);
    return read(fildes,buf,nbyte);
}

size_t pwrite(int fildes, const void *buf, size_t nbyte, long offset)
{
    _lseek(fildes,offset,SEEK_SET);
    return write(fildes,buf,nbyte);
}


int inet_aton(const char *cp, struct in_addr *inp)
{
    inp->S_un.S_addr = inet_addr(cp);
    return 1;
}

#endif

#ifdef _WIN32

LARGE_INTEGER get_freq() {
    LARGE_INTEGER proc_freq;
    if (!::QueryPerformanceFrequency(&proc_freq))
    	print_error("HiResTimeOfDay: QueryPerformanceFrequency() failed");
    return proc_freq;
}

tint usec_time(void)
{
	static LARGE_INTEGER last_time;
	LARGE_INTEGER cur_time;
	QueryPerformanceCounter(&cur_time);
	if (cur_time.QuadPart<last_time.QuadPart)
		print_error("QueryPerformanceCounter wrapped"); // does this happen?
	last_time = cur_time;
	static float freq = 1000000.0/get_freq().QuadPart;
	tint usec = cur_time.QuadPart * freq;
	return usec;
}


#else

tint usec_time(void)
{
    struct timeval t;
    gettimeofday(&t,NULL);
    tint ret;
    ret = t.tv_sec;
    ret *= 1000000;
    ret += t.tv_usec;
    return ret;
}

#endif

void LibraryInit(void)
{
#ifdef _WIN32
	static WSADATA _WSAData;
	// win32 requires you to initialize the Winsock DLL with the desired
	// specification version
	WORD wVersionRequested;
    wVersionRequested = MAKEWORD(2, 2);
	WSAStartup(wVersionRequested, &_WSAData);
#endif
}


std::string gettmpdir(void)
{
#ifdef _WIN32
  DWORD result = ::GetTempPath(0, _T(""));
  if (result == 0)
	throw std::runtime_error("Could not get system temp path");

  std::vector<TCHAR> tempPath(result + 1);
  result = ::GetTempPath(static_cast<DWORD>(tempPath.size()), &tempPath[0]);
  if((result == 0) || (result >= tempPath.size()))
	throw std::runtime_error("Could not get system temp path");

  return std::string(tempPath.begin(), tempPath.begin() + static_cast<std::size_t>(result));
#else
	  return std::string("/tmp/");
#endif
}

bool    make_socket_nonblocking(evutil_socket_t fd) {
#ifdef _WIN32
    u_long enable = 1;
    return 0==ioctlsocket(fd, FIONBIO, &enable);
#else
    return 0==fcntl(fd, F_SETFL, O_NONBLOCK);
#endif
}

bool    close_socket (evutil_socket_t sock) {
#ifdef _WIN32
    return 0==closesocket(sock);
#else
    return 0==::close(sock);
#endif
}

    
// Arno: not thread safe!
struct timeval* tint2tv (tint t) {
    static struct timeval tv;
    tv.tv_usec = t%TINT_SEC;
    tv.tv_sec = t/TINT_SEC;
    return &tv;
}


}
