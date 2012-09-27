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
#include <atlbase.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif
#include <iostream>

namespace swift {

#ifdef _WIN32
static HANDLE map_handles[1024];
#endif

int64_t file_size (int fd) {

#ifdef WIN32
    struct _stat32i64 st;
    _fstat32i64(fd, &st);
#else
    struct stat st;
    st.st_size = 0;
    fstat(fd, &st);
#endif
    return st.st_size;
}

int     file_seek (int fd, int64_t offset) {
#ifndef _WIN32
    return lseek(fd,offset,SEEK_SET);
#else
    return _lseeki64(fd,offset,SEEK_SET);
#endif
}

int     file_resize (int fd, int64_t new_size) {
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

size_t pread(int fildes, void *buf, size_t nbyte, __int64 offset)
{
    int64_t ret = _lseeki64(fildes,offset,SEEK_SET);
    if (ret == -1L)
        return -1;
    else
        return read(fildes,buf,nbyte);
}

size_t pwrite(int fildes, const void *buf, size_t nbyte, __int64 offset)
{
    int64_t ret = _lseeki64(fildes,offset,SEEK_SET);
    if (ret == -1L)
        return -1;
    else
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


/*
 * UNICODE
 */


wchar_t* utf8to16(std::string utf8str)
{
#ifdef _WIN32
    CA2W utf16obj(utf8str.c_str(), CP_UTF8);

    size_t utf16bytelen = (wcslen(utf16obj.m_psz)+1) * sizeof(wchar_t);
    wchar_t *utf16str = (wchar_t *)malloc(utf16bytelen);
    wcscpy(utf16str,utf16obj.m_psz);
    
    //std::wcerr << "utf8to16: return <" << utf16str << ">" << std::endl;

    return utf16str;
#else
    return NULL;
#endif
}

std::string utf16to8(wchar_t* utf16str)
{
#ifdef _WIN32
    //std::wcerr << "utf16to8: in " << utf16str << std::endl;
    CW2A utf8obj(utf16str, CP_UTF8);
    return std::string(utf8obj.m_psz);
#else
    return "(nul)";
#endif
}



int open_utf8(const char *filename, int flags, mode_t mode)
{
#ifdef _WIN32
    wchar_t *utf16fn = utf8to16(filename);
    int ret = _wopen(utf16fn,flags,mode);
    free(utf16fn);
    return ret;
#else
    return open(filename,flags,mode); // TODO: UNIX with locale != UTF-8
#endif
}
  

FILE *fopen_utf8(const char *filename, const char *mode)
{
#ifdef _WIN32
    wchar_t *utf16fn = utf8to16(filename);
    wchar_t *utf16mode = utf8to16(mode);
    FILE *fp = _wfopen(utf16fn,utf16mode);
    free(utf16fn);
    free(utf16mode);
    return fp;
#else
    return fopen(filename,mode);    // TODO: UNIX with locale != UTF-8
#endif
}
  



int64_t file_size_by_path_utf8(std::string pathname) {
    int ret = 0;
#ifdef WIN32
    struct __stat64 st;
    wchar_t *utf16c = utf8to16(pathname);
    ret = _wstat64(utf16c, &st);
    free(utf16c);
#else
    struct stat st;
    ret = stat(pathname.c_str(), &st);  // TODO: UNIX with locale != UTF-8
#endif
    if (ret < 0)
        return ret;
    else
        return st.st_size;
}

int file_exists_utf8(std::string pathname)
{
    int ret = 0;
#ifdef WIN32
    struct __stat64 st;
    wchar_t *utf16c = utf8to16(pathname);
    ret = _wstat64(utf16c, &st);
    free(utf16c);
#else
    struct stat st;
    ret = stat(pathname.c_str(), &st); // TODO: UNIX with locale != UTF-8
#endif
    if (ret < 0)
    {
        if (errno == ENOENT)
            return 0;
        else
            return ret;
    }
    else if (st.st_mode & S_IFDIR)
        return 2;
    else
        return 1;
}


int mkdir_utf8(std::string dirname)
{
#ifdef WIN32
    wchar_t *utf16c = utf8to16(dirname);
    int ret = _wmkdir(utf16c);
    free(utf16c);
#else
    int ret = mkdir(dirname.c_str(),S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH); // TODO: UNIX with locale != UTF-8
#endif
    return ret;
}


int remove_utf8(std::string pathname)
{
#ifdef WIN32
    wchar_t *utf16c = utf8to16(pathname);
    int ret = _wremove(utf16c);
    free(utf16c);
#else
    int ret = remove(pathname.c_str()); // TODO: UNIX with locale != UTF-8
#endif
    return ret;
}


#if _DIR_ENT_HAVE_D_TYPE
#define TEST_IS_DIR(unixde, st) ((bool)(unixde->d_type & DT_DIR))
#else
#define TEST_IS_DIR(unixde, st) ((bool)(S_ISDIR(st.st_mode)))
#endif

DirEntry *opendir_utf8(std::string pathname)
{
#ifdef _WIN32
    HANDLE hFind;
    WIN32_FIND_DATAW ffd;

    std::string pathsearch = pathname + "\\*.*";
    wchar_t *pathsearch_utf16 = utf8to16(pathsearch);
    hFind = FindFirstFileW(pathsearch_utf16, &ffd);
    free(pathsearch_utf16);
    if (hFind != INVALID_HANDLE_VALUE)
    {
	std::string utf8fn = utf16to8(ffd.cFileName);
	DirEntry *de = new DirEntry(utf8fn,(bool)((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0));
	de->hFind_ = hFind;
	return de;
    }
    else
	return NULL;
#else
    DIR *dirp = opendir( pathname.c_str() ); // TODO: UNIX with locale != UTF-8
    if (dirp == NULL)
        return NULL;
    struct dirent *unixde = readdir(dirp);
    if (unixde == NULL)
        return NULL;
    else
    {
#if _DIR_ENT_HAVE_D_TYPE
        if( unixde->d_type == DT_UNKNOWN ) {
#endif
	    std::string fullpath = pathname + FILE_SEP;
	    struct stat st;
	    st.st_mode = 0;
	    int ret = stat(fullpath.append(unixde->d_name).c_str(), &st);
	    if (ret < 0)
	    {
	        print_error("failed to stat file in directory");
	        return NULL;
	    }
#if _DIR_ENT_HAVE_D_TYPE
	    if( S_ISDIR(st.st_mode) )
		unixde->d_type = DT_DIR;
	}
#endif
	DirEntry *de = new DirEntry(unixde->d_name,TEST_IS_DIR(unixde, st));
	de->dirp_ = dirp;
	de->basename_ = pathname;
	return de;
    }
#endif
}


DirEntry *readdir_utf8(DirEntry *prevde)
{
#ifdef _WIN32
    WIN32_FIND_DATAW ffd;
    BOOL ret = FindNextFileW(prevde->hFind_, &ffd);
    if (!ret)
    {
	FindClose(prevde->hFind_);
	return NULL;
    }
    else
    {
  	std::string utf8fn = utf16to8(ffd.cFileName);
	DirEntry *de = new DirEntry(utf8fn,(bool)((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0));
	de->hFind_ = prevde->hFind_;
	return de;
    }
#else
    struct dirent *unixde = readdir(prevde->dirp_);
    if (unixde == NULL)
    {
	closedir(prevde->dirp_);
	return NULL;
    }
    else
    {
#if _DIR_ENT_HAVE_D_TYPE
       if( unixde->d_type == DT_UNKNOWN ) {
#endif
            std::string fullpath = prevde->basename_ + FILE_SEP;
	    struct stat st;
	    st.st_mode = 0;
	    int ret = stat(fullpath.append(unixde->d_name).c_str(), &st);
            if (ret < 0)
	    {
	        print_error("failed to stat file in directory");
	        return NULL;
	    }
#if _DIR_ENT_HAVE_D_TYPE
	    if( S_ISDIR(st.st_mode) )
	        unixde->d_type = DT_DIR;
        }
#endif
        DirEntry *de = new DirEntry(unixde->d_name,TEST_IS_DIR(unixde, st));
        de->dirp_ = prevde->dirp_;
        de->basename_ = prevde->basename_;
        return de;
    }
#endif
}





std::string gettmpdir_utf8(void)
{
#ifdef _WIN32
    DWORD ret = 0;
    wchar_t utf16c[MAX_PATH];
    ret = GetTempPathW(MAX_PATH,utf16c);
    if (ret == 0 || ret > MAX_PATH) {
        return "./";
    }
    else {
        return utf16to8(utf16c);
    }
#else
    return "/tmp/";
#endif
}

int chdir_utf8(std::string dirname)
{
#ifdef _WIN32
    wchar_t *utf16c = utf8to16(dirname);
    int ret = !::SetCurrentDirectoryW(utf16c);
    free(utf16c);
    return ret;
#else
    return chdir(dirname.c_str()); // TODO: UNIX with locale != UTF-8
#endif
}


std::string getcwd_utf8(void)
{
#ifdef _WIN32
    wchar_t szDirectory[MAX_PATH];
    !::GetCurrentDirectoryW(sizeof(szDirectory) - 1, szDirectory);
    return utf16to8(szDirectory);
#else
    char *cwd = getcwd(NULL,0);
    std::string cwdstr(cwd);
    free(cwd);
    return cwdstr;
#endif
}


std::string dirname_utf8(std::string pathname)
{
    int idx = pathname.rfind(FILE_SEP);
    if (idx != std::string::npos)
    {
        return pathname.substr(0,idx);
    }
    else
        return "";
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


std::string hex2bin(std::string input)
{
    std::string res;
    res.reserve(input.size() / 2);
    for (int i = 0; i < input.size(); i += 2)
    {
        std::istringstream iss(input.substr(i, 2));
        int temp;
        iss >> std::hex >> temp;
        res += static_cast<char>(temp);
    }
    return res;
}
