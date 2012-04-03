/*
 *  storage.cpp
 *  swift
 *
 *  Created by Arno Bakker.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 * TODO:
 * - mkdir for META-INF
 * - Unicode?
 * - If multi-file spec, then exact size known after 1st few chunks. Use for swift::Size()?
 */

#include "swift.h"
#include "compat.h"

#include <vector>
#include <utility>

using namespace swift;

#define MULTIFILE_PATHNAME	"META-INF-multifile.txt"  // TODO: META-INF/
#define MULTIFILE_MAX_PATH	2048
#define MULTIFILE_MAX_LINE	MULTIFILE_MAX_PATH+1+32+1


Storage::Storage(std::string pathname) : state_(STOR_STATE_INIT), spec_size_(0), single_fd_(-1)
{
	pathname_utf8_str_ = pathname;

#ifdef WIN32
	struct _stat statbuf;
#else
	struct stat statbuf;
#endif
	int ret = stat( pathname.c_str(), &statbuf );
	if( ret < 0 && errno == ENOENT)
	{
		return;
	}

	// File exists

	std::string ps = pathname;
	unsigned int last = ps.rfind(MULTIFILE_PATHNAME); // Find the last occurrence of ending
	if (last != std::string::npos)
	{
		// Pathname points to a multi-file spec, assume we're seeding
		StorageFile *sf = new StorageFile(MULTIFILE_PATHNAME,0,statbuf.st_size);
		if (ParseSpec(sf) < 0)
			print_error("storage: error parsing multi-file spec");
	}
	else
	{
		// Normal swarm
		(void)OpenSingleFile();
	}
}


Storage::~Storage()
{
	if (single_fd_ != -1)
		close(single_fd_);

	storage_files_t::iterator iter;
	for (iter = sfs_.begin(); iter < sfs_.end(); iter++)
	{
		StorageFile *sf = *iter;
		delete sf;
	}
	sfs_.clear();
}


ssize_t  Storage::Write(const void *buf, size_t nbyte, int64_t offset)
{
	fprintf(stderr,"storage: Write: nbyte %d off %lli\n", nbyte,offset);

	if (state_ == STOR_STATE_SINGLE_FILE)
	{
		return pwrite(single_fd_, buf, nbyte, offset);
	}
	// MULTIFILE
	if (state_ == STOR_STATE_INIT)
	{
		if (offset != 0)
		{
			errno = EINVAL;
			return -1;
		}

		fprintf(stderr,"storage: Write: chunk 0\n");

		// Check for multifile spec. If present, multifile, otherwise single
		if (!strncmp((const char *)buf,MULTIFILE_PATHNAME,strlen(MULTIFILE_PATHNAME)))
		{
			fprintf(stderr,"storage: Write: Is multifile\n");

			// multifile entry will fit into first chunk
			const char *bufstr = (const char *)buf;
			int n = sscanf((const char *)&bufstr[strlen(MULTIFILE_PATHNAME)+1],"%lli",&spec_size_);
			if (n != 1)
			{
				errno = EINVAL;
				return -1;
			}

			fprintf(stderr,"storage: Write: multifile: specsize %lli\n", spec_size_ );

			// Create StorageFile for multi-file spec.
			StorageFile *sf = new StorageFile(MULTIFILE_PATHNAME,0,spec_size_);
			sfs_.push_back(sf);

			// Write all, or part of spec and set state_
			return WriteSpecPart(sf,buf,nbyte,offset);
		}
		else
		{
			// Is a single file swarm.
			return OpenSingleFile();
		}
	}
	else if (state_ == STOR_STATE_MFSPEC_SIZE_KNOWN)
	{
		StorageFile *sf = sfs_[0];

		fprintf(stderr,"storage: Write: mf spec size known\n");

		return WriteSpecPart(sf,buf,nbyte,offset);
	}
	else
	{
		// state_ == STOR_STATE_MFSPEC_COMPLETE;
		fprintf(stderr,"storage: Write: complete\n");

		StorageFile *sf = FindStorageFile(offset);
		if (sf == NULL)
		{
			fprintf(stderr,"storage: Write: File not found!\n");
			errno = EINVAL;
			return -1;
		}

		std::pair<int64_t,int64_t> ht = WriteBuffer(sf,buf,nbyte,offset);
		if (ht.first == -1)
		{
			errno = EINVAL;
			return -1;
		}

		fprintf(stderr,"storage: Write: complete: first %lli second %lli\n", ht.first, ht.second);

		if (ht.second > 0)
		{
			// Write tail to next StorageFile(s) using recursion
			const char *bufstr = (const char *)buf;
			int ret = Write(&bufstr[ht.first], ht.second, offset+ht.first );
			if (ret < 0)
				return ret;
			else
				return ht.first+ret;
		}
		else
			return ht.first;
	}
}


int Storage::WriteSpecPart(StorageFile *sf, const void *buf, size_t nbyte, int64_t offset)
{
	fprintf(stderr,"storage: WriteSpecPart: %s %d %lli\n", sf->GetPathNameUTF8String().c_str(), nbyte, offset );

	std::pair<int64_t,int64_t> ht = WriteBuffer(sf,buf,nbyte,offset);
	if (ht.first == -1)
	{
		errno = EINVAL;
		return -1;
	}

	if (offset+ht.first == sf->GetEnd()+1)
	{
		// Wrote last part of spec
		state_ = STOR_STATE_MFSPEC_COMPLETE;

		if (ParseSpec(sf) < 0)
		{
			errno = EINVAL;
			return -1;
		}

		// Write tail to next StorageFile(s) using recursion
		const char *bufstr = (const char *)buf;
		int ret = Write(&bufstr[ht.first], ht.second, offset+ht.first );
		if (ret < 0)
			return ret;
		else
			return ht.first+ret;
	}
	else
	{
		state_ = STOR_STATE_MFSPEC_SIZE_KNOWN;
		return ht.first;
	}
}



std::pair<int64_t,int64_t> Storage::WriteBuffer(StorageFile *sf, const void *buf, size_t nbyte, int64_t offset)
{
	fprintf(stderr,"storage: WriteBuffer: %s %d %lli\n", sf->GetPathNameUTF8String().c_str(), nbyte, offset );

	int ret = -1;
	if (offset+nbyte <= sf->GetEnd()+1)
	{
		// Chunk belongs completely in sf
		ret = sf->Write(buf,nbyte,offset - sf->GetStart());

		fprintf(stderr,"storage: WriteBuffer: Write: covered ret %d\n", ret );

		if (ret < 0)
			return std::make_pair(-1,-1);
		else
			return std::make_pair(nbyte,0);

	}
	else
	{
		int64_t head = sf->GetEnd()+1 - offset;
		int64_t tail = nbyte - head;

		// Write last part of file
		ret = sf->Write(buf,head,offset - sf->GetStart() );

		fprintf(stderr,"storage: WriteBuffer: Write: partial ret %d\n", ret );

		if (ret < 0)
			return std::make_pair(-1,-1);
		else
			return std::make_pair(head,tail);
	}
}




StorageFile * Storage::FindStorageFile(int64_t offset)
{
	// Binary search for StorageFile that manages the given offset
	int imin = 0, imax=sfs_.size()-1;
	while (imax >= imin)
	{
		int imid = (imin + imax) / 2;
		if (offset >= sfs_[imid]->GetEnd()+1)
			imin = imid + 1;
		else if (offset < sfs_[imid]->GetStart())
			imax = imid - 1;
		else
			return sfs_[imid];
	}
	// Should find it.
	return NULL;
}


int Storage::ParseSpec(StorageFile *sf)
{
	char *ret = NULL,line[MULTIFILE_MAX_LINE];
	FILE *fp = fopen(sf->GetPathNameUTF8String().c_str(),"rb"); // FAXME decode UTF-8


	int64_t offset=0;
	while(1)
	{
		ret = fgets(line,MULTIFILE_MAX_LINE,fp);
		if (ret == NULL)
			break;

		char *savetok = NULL;
		int64_t fsize=0;

		char * token = strtok_r(line," ",&savetok); // filename
        if (token == NULL)
        	return -1;
        char *utf8path = token;
        token = strtok_r(NULL," ",&savetok);       // size
        if (token == NULL)
        	return -1;
        char *sizestr = token;
        int n = sscanf(sizestr,"%lli",&fsize);
        if (n == 0)
			return -1;

		if (offset == 0)
			// sf already created for multifile-spec entry
			offset += spec_size_;
		else
		{
			StorageFile *sf = new StorageFile(utf8path,offset,fsize);
			sfs_.push_back(sf);
			offset += fsize;
		}
	}

	// Assume: Multi-file spec sorted, so vector already sorted on offset
	storage_files_t::iterator iter;
	for (iter = sfs_.begin(); iter < sfs_.end(); iter++)
	{
		StorageFile *sf = *iter;
		fprintf(stderr,"storage: ParseSpec: Got %s start %lli size %lli\n", sf->GetPathNameUTF8String().c_str(), sf->GetStart(), sf->GetSize() );
	}


	fclose(fp);
	return 0;
}


int Storage::OpenSingleFile()
{
	state_ = STOR_STATE_SINGLE_FILE;

	single_fd_ = open(pathname_utf8_str_.c_str(),OPENFLAGS,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (single_fd_<0) {
		single_fd_ = -1;
		print_error("storage: cannot open single file");
	}
	return single_fd_;
}




ssize_t  Storage::Read(void *buf, size_t nbyte, int64_t offset)
{
	fprintf(stderr,"storage: Read: nbyte " PRISIZET " off %lli\n", nbyte, offset );

	if (state_ == STOR_STATE_SINGLE_FILE)
	{
		return pread(single_fd_, buf, nbyte, offset);
	}

	// MULTIFILE
	if (state_ == STOR_STATE_INIT)
	{
		errno = EINVAL;
		return -1;
	}
	else
	{
		StorageFile *sf = FindStorageFile(offset);
		if (sf == NULL)
		{
			errno = EINVAL;
			return -1;
		}

		fprintf(stderr,"storage: Read: found file %s for off %lli\n", sf->GetPathNameUTF8String().c_str(), offset );

		ssize_t ret = sf->Read(buf,nbyte,offset - sf->GetStart());
		if (ret < 0)
			return ret;

		fprintf(stderr,"storage: Read: read %d\n", ret );

		if (ret < nbyte && offset+ret != ht_->size())
		{
			fprintf(stderr,"storage: Read: want %d more\n", nbyte-ret );

			// Not at end, and can fit more in buffer. Do recursion
			char *bufstr = (char *)buf;
			ssize_t newret = Read((void *)(bufstr+ret),nbyte-ret,offset+ret);
			if (newret < 0)
				return newret;
			else
				return ret + newret;
		}
		else
			return ret;
	}
}


int64_t Storage::GetReservedSize()
{
	if (state_ == STOR_STATE_SINGLE_FILE)
	{
		return file_size(single_fd_);
	}
	else if (state_ != STOR_STATE_MFSPEC_COMPLETE)
		return -1;

	// MULTIFILE
	storage_files_t::iterator iter;
	int64_t totaldisksize=0;
	for (iter = sfs_.begin(); iter < sfs_.end(); iter++)
	{
		StorageFile *sf = *iter;
#ifdef WIN32
		struct _stat statbuf;
#else
		struct stat statbuf;
#endif
		int ret = stat( sf->GetPathNameUTF8String().c_str(), &statbuf );
		if( ret < 0)
		{
			fprintf(stderr,"storage: getsize: cannot stat file %s\n", sf->GetPathNameUTF8String().c_str() );
			return ret;
		}
		else
			totaldisksize += statbuf.st_size;
	}

	return totaldisksize;
}


int Storage::ResizeReserved(int64_t size)
{
	if (state_ == STOR_STATE_SINGLE_FILE)
	{
		return file_resize(single_fd_,size);
	}
	else if (state_ != STOR_STATE_MFSPEC_COMPLETE)
		return -1;

	// MULTIFILE
	if (size > GetReservedSize())
	{
		// Resize files to wanted size, so pread() / pwrite() works for all offsets.
		storage_files_t::iterator iter;
		for (iter = sfs_.begin(); iter < sfs_.end(); iter++)
		{
			StorageFile *sf = *iter;
			int ret = sf->ResizeReserved();
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}




/*
 * StorageFile
 */



StorageFile::StorageFile(std::string utf8path, int64_t start, int64_t size) : fd_(-1)
{
	 pathname_utf8_str_ = utf8path;
	 start_ = start;
	 end_ = start+size-1;

	 fd_ = open(pathname_utf8_str_.c_str(),OPENFLAGS,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	 if (fd_<0) {
		 fprintf(stderr,"storage: file: Could not open %s\n", pathname_utf8_str_.c_str() );
         return;
	 }
}

StorageFile::~StorageFile()
{
	 if (fd_ < 0)
		 close(fd_);
}


