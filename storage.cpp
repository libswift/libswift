//#include "swift.h"
#include "compat.h"

#include <vector>
#include <utility>

using namespace swift;

#define MULTIFILE_PATHNAME	"META-INF-multifile.txt"  // TODO: META-INF/
#define MULTIFILE_MAX_PATH	2048
#define MULTIFILE_MAX_LINE	MULTIFILE_MAX_PATH+1+32+1


class HashTree
{
	uint64_t        size_;

  public:
	HashTree(uint64_t size)
	{
		size_ = size;
	}

	uint64_t        size () const { return size_; }
};

class FileTransfer
{
	HashTree hashtree_;

  public:
	FileTransfer(uint64_t size) : hashtree_(size)
	{
	}


	HashTree &file() { return hashtree_; }
};


#ifdef _WIN32
#define OPENFLAGS         O_RDWR|O_CREAT|_O_BINARY
#else
#define OPENFLAGS         O_RDWR|O_CREAT
#endif


class StorageFile
{
   public:
	 StorageFile(char *utf8path, int64_t start, int64_t size) : fd_(-1)
	 {
		 utf8path_ = strdup(utf8path);
		 start_ = start;
		 end_ = start+size-1;

		 fd_ = open(utf8path,OPENFLAGS,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		 if (fd_<0) {
			 fprintf(stderr,"storage: file: Could not open %s\n", utf8path );
 	         return;
		 }
	 }

	 ~StorageFile()
	 {
		 if (fd_ < 0)
			 close(fd_);
		 free(utf8path_);
	 }
	 int64_t GetStart() { return start_; }
	 int64_t GetEnd() { return end_; }
	 int64_t GetSize() { return end_+1-start_; }
	 char *GetUTF8Path() { return utf8path_; }

	 ssize_t  Write(const void *buf, size_t nbyte, int64_t offset)
	 {
		 return pwrite(fd_,buf,nbyte,offset);
	 }
	 ssize_t  Read(void *buf, size_t nbyte, int64_t offset)
	 {
		 return pread(fd_,buf,nbyte,offset);
	 }

   protected:
	 char *	 	utf8path_;
	 int64_t	start_;
	 int64_t	end_;

	 int		fd_;
};


typedef std::vector<StorageFile *>	storage_files_t;



    class Storage {

      public:

        typedef enum {
            STOR_STATE_INIT,
            STOR_STATE_MFSPEC_SIZE_KNOWN,
            STOR_STATE_MFSPEC_COMPLETE,
            STOR_STATE_SINGLE_FILE
        } storage_state_t;

        typedef std::vector<StorageFile *>	storage_files_t;

    	Storage(FileTransfer *ft);

    	/** UNIX pread approximation. Does change file pointer. Is not thread-safe */
    	ssize_t  Read(void *buf, size_t nbyte, int64_t offset); // off_t not 64-bit dynamically on Win32

    	/** UNIX pwrite approximation. Does change file pointer. Is not thread-safe */
    	ssize_t  Write(const void *buf, size_t nbyte, int64_t offset);

      protected:
    		/** FileTransfer this Storage belongs to */
    	    FileTransfer *ft_;

    	    storage_state_t	state_;

    	    int64_t spec_size_;

    	    storage_files_t	sfs_;
    	    int single_fd_;

    	    int WriteSpecPart(StorageFile *sf, const void *buf, size_t nbyte, int64_t offset);
    	    std::pair<int64_t,int64_t> WriteBuffer(StorageFile *sf, const void *buf, size_t nbyte, int64_t offset);
    	    StorageFile * FindStorageFile(int64_t offset);
    	    int ParseSpec(StorageFile *sf);

    };



Storage::Storage(FileTransfer *ft) : ft_(ft), state_(STOR_STATE_INIT), spec_size_(0), single_fd_(-1)
{
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
			state_ = STOR_STATE_SINGLE_FILE;
			// TODO: single_fd_ =
			return -481;
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
	fprintf(stderr,"storage: WriteSpecPart: %s %d %lli\n", sf->GetUTF8Path(), nbyte, offset );

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
	fprintf(stderr,"storage: WriteBuffer: %s %d %lli\n", sf->GetUTF8Path(), nbyte, offset );

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
	FILE *fp = fopen(sf->GetUTF8Path(),"rb"); // FAXME decode UTF-8


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
		fprintf(stderr,"storage: ParseSpec: Got %s start %lli size %lli\n", sf->GetUTF8Path(), sf->GetStart(), sf->GetSize() );
	}


	fclose(fp);
	return 0;
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

		fprintf(stderr,"storage: Read: found file %s for off %lli\n", sf->GetUTF8Path(), offset );

		ssize_t ret = sf->Read(buf,nbyte,offset - sf->GetStart());
		if (ret < 0)
			return ret;

		fprintf(stderr,"storage: Read: read %d\n", ret );

		if (ret < nbyte && offset+ret != ft_->file().size())
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


typedef std::vector<std::pair<std::string,int64_t> >	filelist_t;

#include <sstream>

#define BLOCKSIZE	200


int main()
{
	filelist_t	filelist;

	// 1. Make list of files
	std::string prefix="REALLYBIGSTRINGSOMEwecangetaspecthatislargerthanwhatiswrittenHERE-";
	filelist.push_back(std::make_pair(prefix+"a.tst",100));
	filelist.push_back(std::make_pair(prefix+"b.tst",200));
	filelist.push_back(std::make_pair(prefix+"c.tst",1024));
	filelist.push_back(std::make_pair(prefix+"d.tst",5*1024));

	// 2. Create spec body
	std::ostringstream specbody;

	filelist_t::iterator iter;
	for (iter = filelist.begin(); iter < filelist.end(); iter++)
	{
		specbody << (*iter).first << " " << (*iter).second << "\n";
	}


	fprintf(stderr,"specbody: <%s>\n", specbody.str().c_str() );

	// 2. Calc specsize
	int specsize = strlen(MULTIFILE_PATHNAME)+1+0+1+specbody.str().size();
	char numstr[100];
	sprintf(numstr,"%d",specsize);
	char numstr2[100];
	sprintf(numstr2,"%d",specsize+strlen(numstr));
	if (strlen(numstr) == strlen(numstr2))
		specsize += strlen(numstr);
	else
		specsize += strlen(numstr)+(strlen(numstr2)-strlen(numstr));

	// 3. Create spec as string
	std::ostringstream spec;
	spec << MULTIFILE_PATHNAME;
	spec << " ";
	spec << specsize;
	spec << "\n";
	spec << specbody.str();

	fprintf(stderr,"spec: <%s>\n", spec.str().c_str() );

	// 4. Create byte space
	std::ostringstream asset;
	asset << spec.str();
	char c = 'A';
	for (iter = filelist.begin(); iter < filelist.end(); iter++)
	{
		int64_t count = (*iter).second;
		fprintf(stderr,"count: %d\n", count );
		for (int64_t i=0; i<count; i++)
			asset << c;

		c++; // TODO: more than 256-ord('A') files
	}

	const char *assetbytes = asset.str().c_str();

	fprintf(stderr,"asset2: <%s> size %d\n", asset.str().c_str(), asset.str().size() );

	FileTransfer *ft = new FileTransfer(asset.str().size());

	Storage *s = new Storage(ft);
	int64_t off=0;
	int ret = 0;
	while (ret >=0 && off < asset.str().size())
	{
		int nbyte = min(asset.str().size()-off,BLOCKSIZE);

		ret = s->Write(asset.str().c_str()+off,nbyte,off);
		fprintf(stderr,"main: wrote %d\n", ret );
		off += ret;
	}


	// Read back via OS
	c = 'A';
	for (iter = filelist.begin(); iter < filelist.end(); iter++)
	{
		char *filename = strdup( (*iter).first.c_str() );
		int64_t fsize = (*iter).second;

#ifdef WIN32
		struct _stat statbuf;
#else
		struct stat statbuf;
#endif
		int res = stat( filename, &statbuf );
		if( res < 0)
		{
			fprintf(stderr,"main: read OS: stat %d\n", ret );
			continue;
		}

		if (statbuf.st_size != fsize)
			fprintf(stderr,"main: read OS: size wrong %s exp %lli got %lli\n", filename, fsize, statbuf.st_size );

		FILE *fp = fopen(filename,"rb");
		char buf[512];
		while(!feof(fp))
		{
			ret = fread(buf,sizeof(char),sizeof(buf), fp);
			if (ret < 0)
			{
				fprintf(stderr,"main: read OS: %d\n", ret );
				continue;
			}
			for (int i=0; i<ret; i++)
			{
				if (buf[i] != c)
				{
					fprintf(stderr,"main: read OS: wrong bytes %d\n", buf[i] );
					break;
				}

			}
		}
		fclose(fp);

		c++;
	}


	// Read back via Storage API
	off = 0;
	while(true)
	{
		char readbuf[512];

		fprintf(stderr,"main: read API: reading %lli\n", off );

		int ret = s->Read(readbuf,sizeof(readbuf),off);
		if (ret < 0)
		{
			fprintf(stderr,"main: read API: %d errno %s\n", ret, strerror(errno) );
			break;
		}

		fprintf(stderr,"main: read API: got %d\n", ret );

		if (memcmp(readbuf,asset.str().c_str()+off,ret))
		{
			fprintf(stderr,"main: read API: asset mismatch\n" );
		}

		off += ret;
	}
}

