/*
 *  hashtree.cpp
 *  serp++
 *
 *  Created by Victor Grishchenko on 3/6/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#include "hashtree.h"
#include "bin_utils.h"
//#include <openssl/sha.h>
#include "sha1.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include "compat.h"
#include "swift.h"

#include <iostream>


using namespace swift;

const Sha1Hash Sha1Hash::ZERO = Sha1Hash();

void SHA1 (const void *data, size_t length, unsigned char *hash) {
    blk_SHA_CTX ctx;
    blk_SHA1_Init(&ctx);
    blk_SHA1_Update(&ctx, data, length);
    blk_SHA1_Final(hash, &ctx);
}

Sha1Hash::Sha1Hash(const Sha1Hash& left, const Sha1Hash& right) {
    blk_SHA_CTX ctx;
    blk_SHA1_Init(&ctx);
    blk_SHA1_Update(&ctx, left.bits,SIZE);
    blk_SHA1_Update(&ctx, right.bits,SIZE);
    blk_SHA1_Final(bits, &ctx);
}

Sha1Hash::Sha1Hash(const char* data, size_t length) {
    if (length==-1)
        length = strlen(data);
    SHA1((unsigned char*)data,length,bits);
}

Sha1Hash::Sha1Hash(const uint8_t* data, size_t length) {
    SHA1(data,length,bits);
}

Sha1Hash::Sha1Hash(bool hex, const char* hash) {
    if (hex) {
        int val;
        for(int i=0; i<SIZE; i++) {
            if (sscanf(hash+i*2, "%2x", &val)!=1) {
                memset(bits,0,20);
                return;
            }
            bits[i] = val;
        }
        assert(this->hex()==std::string(hash));
    } else
        memcpy(bits,hash,SIZE);
}

std::string    Sha1Hash::hex() const {
    char hex[HASHSZ*2+1];
    for(int i=0; i<HASHSZ; i++)
        sprintf(hex+i*2, "%02x", (int)(unsigned char)bits[i]);
    return std::string(hex,HASHSZ*2);
}



/**     H a s h   t r e e       */


MmapHashTree::MmapHashTree (Storage *storage, const Sha1Hash& root_hash, uint32_t chunk_size, std::string hash_filename, bool check_hashes, std::string binmap_filename) :
storage_(storage), root_hash_(root_hash), hashes_(NULL), peak_count_(0), hash_fd_(0),
 size_(0), sizec_(0), complete_(0), completec_(0),
chunk_size_(chunk_size)
{
	// MULTIFILE
	storage_->SetHashTree(this);
	// If multi-file spec we know the exact size even before getting peaks+last chunk
	int64_t sizefromspec = storage_->GetSizeFromSpec();
	if (sizefromspec != -1)
	{
		set_size(sizefromspec);
		// Resize all files
		(void)storage_->ResizeReserved(sizefromspec);
	}

	// Arno: if user doesn't want to check hashes but no .mhash, check hashes anyway
	bool actually_check_hashes = check_hashes;
    bool mhash_exists=true;
    int res = file_exists_utf8( hash_filename.c_str());
    if( res <= 0)
    	mhash_exists = false;
    if (!mhash_exists && !check_hashes)
    	actually_check_hashes = true;


    // Arno: if the remainder of the hashtree state is on disk we can
    // hashcheck very quickly
    bool binmap_exists=true;
    res = file_exists_utf8( binmap_filename.c_str() );
    if( res <= 0)
    	binmap_exists = false;

    fprintf(stderr,"hashtree: hashchecking want %s do %s binmap-on-disk %s\n", (check_hashes ? "yes" : "no"), (actually_check_hashes? "yes" : "no"), (binmap_exists? "yes" : "no") );

    hash_fd_ = open_utf8(hash_filename.c_str(),OPENFLAGS,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (hash_fd_<0) {
        hash_fd_ = 0;
        print_error("cannot open hash file");
        return;
    }

    // Arno: if user wants to or no .mhash, and if root hash unknown (new file) and no checkpoint, (re)calc root hash
    if (storage_->GetReservedSize() > storage_->GetMinimalReservedSize() && (actually_check_hashes || (root_hash_==Sha1Hash::ZERO && !binmap_exists) || !mhash_exists) ) {
    	// fresh submit, hash it
    	dprintf("%s hashtree full compute\n",tintstr());
        //assert(storage_->GetReservedSize());
        Submit();
    } else if (mhash_exists && binmap_exists && file_size(hash_fd_)) {
    	// Arno: recreate hash tree without rereading content
    	dprintf("%s hashtree read from checkpoint\n",tintstr());
    	FILE *fp = fopen_utf8(binmap_filename.c_str(),"rb");
    	if (!fp) {
    		 print_error("hashtree: cannot open .mbinmap file");
    		 return;
    	}
    	if (deserialize(fp) < 0) {
    		// Try to rebuild hashtree data
    		Submit();
    	}
    } else {
    	// Arno: no data on disk, or mhash on disk, but no binmap. In latter
    	// case recreate binmap by reading content again. Historic optimization
    	// of Submit.
    	dprintf("%s hashtree empty or partial recompute\n",tintstr());
        RecoverProgress();
    }
}


MmapHashTree::MmapHashTree(bool dummy, std::string binmap_filename) :
root_hash_(Sha1Hash::ZERO), hashes_(NULL), peak_count_(0), hash_fd_(0),
filename_(""), size_(0), sizec_(0), complete_(0), completec_(0),
chunk_size_(0)
{
	FILE *fp = fopen_utf8(binmap_filename.c_str(),"rb");
	if (!fp) {
		 return;
	}
	if (partial_deserialize(fp) < 0) {
	}
	fclose(fp);
}


// Reads complete file and constructs hash tree
void            MmapHashTree::Submit () {
    size_ = storage_->GetReservedSize();
    sizec_ = (size_ + chunk_size_-1) / chunk_size_;

    //fprintf(stderr,"hashtree: submit: cs %i\n", chunk_size_);

    peak_count_ = gen_peaks(sizec_,peaks_);
    int hashes_size = Sha1Hash::SIZE*sizec_*2;
    dprintf("%s hashtree submit resizing hash file to %d\n",tintstr(), hashes_size );
    file_resize(hash_fd_,hashes_size);
    hashes_ = (Sha1Hash*) memory_map(hash_fd_,hashes_size);
    if (!hashes_) {
        size_ = sizec_ = complete_ = completec_ = 0;
        print_error("mmap failed");
        return;
    }
    size_t last_piece_size = (sizec_ - 1) % (chunk_size_) + 1;
    char *chunk = new char[chunk_size_];
    for (uint64_t i=0; i<sizec_; i++) {

        ssize_t rd = storage_->Read(chunk,chunk_size_,i*chunk_size_);
        if (rd<(chunk_size_) && i!=sizec_-1) {
            free(hashes_);
            hashes_=NULL;
            return;
        }
        bin_t pos(0,i);
        hashes_[pos.toUInt()] = Sha1Hash(chunk,rd);
        ack_out_.set(pos);
        while (pos.is_right()){
            pos = pos.parent();
            hashes_[pos.toUInt()] = Sha1Hash(hashes_[pos.left().toUInt()],hashes_[pos.right().toUInt()]);
        }
        complete_+=rd;
        completec_++;
    }
    delete chunk;
    for (int p=0; p<peak_count_; p++) {
        peak_hashes_[p] = hashes_[peaks_[p].toUInt()];
    }

    root_hash_ = DeriveRoot();

}


/** Basically, simulated receiving every single chunk, except
 for some optimizations.
 Precondition: root hash known */
void            MmapHashTree::RecoverProgress () {

	//fprintf(stderr,"hashtree: recover: cs %i\n", chunk_size_);

	if (!RecoverPeakHashes())
		return;

    // at this point, we may use mmapd hashes already
    // so, lets verify hashes and the data we've got
    char *zero_chunk = new char[chunk_size_];
    memset(zero_chunk, 0, chunk_size_);
    Sha1Hash zero_hash(zero_chunk,chunk_size_);

    // Arno: loop over all pieces, read each from file
    // ARNOSMPTODO: problem is that we may have the complete hashtree, but
    // not have all pieces. So hash file gives too little information to
    // determine whether file is complete on disk.
    //
    char *buf = new char[chunk_size_];
    for(int p=0; p<size_in_chunks(); p++) {
        bin_t pos(0,p);
        if (hashes_[pos.toUInt()]==Sha1Hash::ZERO)
            continue;
        ssize_t rd = storage_->Read(buf,chunk_size_,p*chunk_size_);
        if (rd!=(chunk_size_) && p!=size_in_chunks()-1)
            break;
        if (rd==(chunk_size_) && !memcmp(buf, zero_chunk, rd) &&
                hashes_[pos.toUInt()]!=zero_hash) // FIXME // Arno == don't have piece yet?
            continue;
        if (!OfferHash(pos, Sha1Hash(buf,rd)) )
            continue;
        ack_out_.set(pos);
        completec_++;
        complete_+=rd;
        if (rd!=(chunk_size_) && p==size_in_chunks()-1) // set the exact file size
            size_ = ((sizec_-1)*chunk_size_) + rd;
    }
    delete buf;
    delete zero_chunk;
}

/** Precondition: root hash known */
bool MmapHashTree::RecoverPeakHashes()
{
	int64_t ret = storage_->GetReservedSize();
	if (ret < 0)
		return false;

    uint64_t size = ret;
    uint64_t sizek = (size + chunk_size_-1) / chunk_size_;

	// Arno: Calc location of peak hashes, read them from hash file and check if
	// they match to root hash. If so, load hashes into memory.
	bin_t peaks[64];
    int peak_count = gen_peaks(sizek,peaks);
    for(int i=0; i<peak_count; i++) {
        Sha1Hash peak_hash;
        file_seek(hash_fd_,peaks[i].toUInt()*sizeof(Sha1Hash));
        if (read(hash_fd_,&peak_hash,sizeof(Sha1Hash))!=sizeof(Sha1Hash))
            return false;
        OfferPeakHash(peaks[i], peak_hash);
    }
    if (!this->size())
        return false; // if no valid peak hashes found

    return true;
}

int MmapHashTree::serialize(FILE *fp)
{
	fprintf_retiffail(fp,"version %i\n", 1 );
	fprintf_retiffail(fp,"root hash %s\n", root_hash_.hex().c_str() );
	fprintf_retiffail(fp,"chunk size %lu\n", chunk_size_ );
	fprintf_retiffail(fp,"complete %llu\n", complete_ );
	fprintf_retiffail(fp,"completec %llu\n", completec_ );
	return ack_out_.serialize(fp);
}


/** Arno: recreate hash tree from .mbinmap file without rereading content.
 * Precondition: root hash known
 */
int MmapHashTree::deserialize(FILE *fp) {
	return internal_deserialize(fp,true);
}

int MmapHashTree::partial_deserialize(FILE *fp) {
	return internal_deserialize(fp,false);
}


int MmapHashTree::internal_deserialize(FILE *fp,bool contentavail) {

	char hexhashstr[256];
	uint64_t c,cc;
	size_t cs;
	int version;

	fscanf_retiffail(fp,"version %i\n", &version );
	fscanf_retiffail(fp,"root hash %s\n", hexhashstr);
	fscanf_retiffail(fp,"chunk size %lu\n", &cs);
	fscanf_retiffail(fp,"complete %llu\n", &c );
	fscanf_retiffail(fp,"completec %llu\n", &cc );

	if (ack_out_.deserialize(fp) < 0)
		return -1;
	root_hash_ = Sha1Hash(true, hexhashstr);
	chunk_size_ = cs;

	// Arno, 2012-01-03: Hack to just get root hash
	if (!contentavail)
		return 2;

	if (!RecoverPeakHashes()) {
		root_hash_ = Sha1Hash::ZERO;
		ack_out_.clear();
		return -1;
	}

	// Are reset by RecoverPeakHashes() for some reason.
	complete_ = c;
	completec_ = cc;
    size_ = storage_->GetReservedSize();
    sizec_ = (size_ + chunk_size_-1) / chunk_size_;

    return 0;
}


bool            MmapHashTree::OfferPeakHash (bin_t pos, const Sha1Hash& hash) {
	char bin_name_buf[32];
	dprintf("%s hashtree offer peak %s\n",tintstr(),pos.str(bin_name_buf));

    //assert(!size_);
    if (peak_count_) {
        bin_t last_peak = peaks_[peak_count_-1];
        if ( pos.layer()>=last_peak.layer() ||
            pos.base_offset()!=last_peak.base_offset()+last_peak.base_length() )
            peak_count_ = 0;
    }
    peaks_[peak_count_] = pos;
    peak_hashes_[peak_count_] = hash;
    peak_count_++;
    // check whether peak hash candidates add up to the root hash
    Sha1Hash mustbe_root = DeriveRoot();
    if (mustbe_root!=root_hash_)
        return false;
    for(int i=0; i<peak_count_; i++)
        sizec_ += peaks_[i].base_length();

    // bingo, we now know the file size (rounded up to a chunk_size() unit)

    if (!size_) // MULTIFILE: not known from spec
    	size_ = sizec_ * chunk_size_;
    completec_ = complete_ = 0;
    sizec_ = (size_ + chunk_size_-1) / chunk_size_;

    // ARNOTODO: win32: this is pretty slow for ~200 MB already. Perhaps do
    // on-demand sizing for Win32?
    uint64_t cur_size = storage_->GetReservedSize();
    if ( cur_size<=(sizec_-1)*chunk_size_  || cur_size>sizec_*chunk_size_ ) {
    	dprintf("%s hashtree offerpeak resizing file\n",tintstr() );
        if (storage_->ResizeReserved(size_)) {
            print_error("cannot set file size\n");
            size_=0; // remain in the 0-state
            return false;
        }
    }

    // mmap the hash file into memory
    uint64_t expected_size = sizeof(Sha1Hash)*sizec_*2;
    // Arno, 2011-10-18: on Windows we could optimize this away,
    //CreateFileMapping, see compat.cpp will resize the file for us with
    // the right params.
    //
    if ( file_size(hash_fd_) != expected_size ) {
    	dprintf("%s hashtree offerpeak resizing hash file\n",tintstr() );
        file_resize (hash_fd_, expected_size);
    }

    hashes_ = (Sha1Hash*) memory_map(hash_fd_,expected_size);
    if (!hashes_) {
        size_ = sizec_ = complete_ = completec_ = 0;
        print_error("mmap failed");
        return false;
    }

    for(int i=0; i<peak_count_; i++)
        hashes_[peaks_[i].toUInt()] = peak_hashes_[i];

    dprintf("%s hashtree memory mapped\n",tintstr() );

    return true;
}


Sha1Hash        MmapHashTree::DeriveRoot () {

	dprintf("%s hashtree deriving root\n",tintstr() );

    int c = peak_count_-1;
    bin_t p = peaks_[c];
    Sha1Hash hash = peak_hashes_[c];
    c--;
    // Arno, 2011-10-14: Root hash = top of smallest tree covering content IMHO.
    //while (!p.is_all()) {
    while (c >= 0) {
        if (p.is_left()) {
            p = p.parent();
            hash = Sha1Hash(hash,Sha1Hash::ZERO);
        } else {
            if (c<0 || peaks_[c]!=p.sibling())
                return Sha1Hash::ZERO;
            hash = Sha1Hash(peak_hashes_[c],hash);
            p = p.parent();
            c--;
        }
    }

    fprintf(stderr,"hashtree: derive: root hash is %s\n", hash.hex().c_str() );

    //fprintf(stderr,"root bin is %lli covers %lli\n", p.toUInt(), p.base_length() );
    return hash;
}


/** For live streaming: appends the data, adjusts the tree.
    @ return the number of fresh (tail) peak hashes */
int         MmapHashTree::AppendData (char* data, int length) {
    return 0;
}


bin_t         MmapHashTree::peak_for (bin_t pos) const {
    int pi=0;
    while (pi<peak_count_ && !peaks_[pi].contains(pos))
        pi++;
    return pi==peak_count_ ? bin_t(bin_t::NONE) : peaks_[pi];
}

bool            MmapHashTree::OfferHash (bin_t pos, const Sha1Hash& hash) {
    if (!size_)  // only peak hashes are accepted at this point
        return OfferPeakHash(pos,hash);
    bin_t peak = peak_for(pos);
    if (peak.is_none())
        return false;
    if (peak==pos)
        return hash == hashes_[pos.toUInt()];
    if (!ack_out_.is_empty(pos.parent()))
        return hash==hashes_[pos.toUInt()]; // have this hash already, even accptd data
    // LESSHASH
    // Arno: if we already verified this hash against the root, don't replace
    if (!is_hash_verified_.is_empty(bin_t(0,pos.toUInt())))
    	return hash == hashes_[pos.toUInt()];

    hashes_[pos.toUInt()] = hash;
    if (!pos.is_base())
        return false; // who cares?
    bin_t p = pos;
    Sha1Hash uphash = hash;
    // Arno: Note well: bin_t(0,p.toUInt()) is to abuse binmap as bitmap.
    while ( p!=peak && ack_out_.is_empty(p) && is_hash_verified_.is_empty(bin_t(0,p.toUInt())) ) {
        hashes_[p.toUInt()] = uphash;
        p = p.parent();
		// Arno: Prevent poisoning the tree with bad values:
		// Left hand hashes should never be zero, and right
		// hand hash is only zero for the last packet, i.e.,
		// layer 0. Higher layers will never have 0 hashes
		// as SHA1(zero+zero) != zero (but b80de5...)
		//
        if (hashes_[p.left().toUInt()] == Sha1Hash::ZERO || hashes_[p.right().toUInt()] == Sha1Hash::ZERO)
        	break;
        uphash = Sha1Hash(hashes_[p.left().toUInt()],hashes_[p.right().toUInt()]);
    }// walk to the nearest proven hash

    bool success = (uphash==hashes_[p.toUInt()]);
    // LESSHASH
    if (success) {
    	// Arno: The hash checks out. Mark all hashes on the uncle path as
    	// being verified, so we don't have to go higher than them on a next
    	// check.
    	p = pos;
    	// Arno: Note well: bin_t(0,p.toUInt()) is to abuse binmap as bitmap.
    	is_hash_verified_.set(bin_t(0,p.toUInt()));
        while (p.layer() != peak.layer()) {
            p = p.parent().sibling();
        	is_hash_verified_.set(bin_t(0,p.toUInt()));
        }
        // Also mark hashes on direct path to root as verified. Doesn't decrease
        // #checks, but does increase the number of verified hashes faster.
    	p = pos;
        while (p != peak) {
            p = p.parent();
        	is_hash_verified_.set(bin_t(0,p.toUInt()));
        }
    }

    return success;
}


bool            MmapHashTree::OfferData (bin_t pos, const char* data, size_t length) {
    if (!size())
        return false;
    if (!pos.is_base())
        return false;
    if (length<chunk_size_ && pos!=bin_t(0,sizec_-1))
        return false;
    if (ack_out_.is_filled(pos))
        return true; // to set data_in_
    bin_t peak = peak_for(pos);
    if (peak.is_none())
        return false;

    Sha1Hash data_hash(data,length);
    if (!OfferHash(pos, data_hash)) {
        char bin_name_buf[32];
//        printf("invalid hash for %s: %s\n",pos.str(bin_name_buf),data_hash.hex().c_str()); // paranoid
    	//fprintf(stderr,"INVALID HASH FOR %lli layer %d\n", pos.toUInt(), pos.layer() );
    	dprintf("%s hashtree check failed (bug TODO) %s\n",tintstr(),pos.str(bin_name_buf));
        return false;
    }

    //printf("g %lli %s\n",(uint64_t)pos,hash.hex().c_str());
    ack_out_.set(pos);
    // Arno,2011-10-03: appease g++
    if (storage_->Write(data,length,pos.base_offset()*chunk_size_) < 0)
    	print_error("pwrite failed");
    complete_ += length;
    completec_++;
    if (pos.base_offset()==sizec_-1) {
        size_ = ((sizec_-1)*chunk_size_) + length;
        if (storage_->GetReservedSize()!=size_)
        	storage_->ResizeReserved(size_);
    }
    return true;
}


uint64_t      MmapHashTree::seq_complete (int64_t offset) {

	uint64_t seqc = 0;
	if (offset == 0)
	{
	    uint64_t seqc = ack_out_.find_empty().base_offset();
	    if (seqc==sizec_)
	        return size_;
	    else
	        return seqc*chunk_size_;
	}
	else
	{
		// SEEK: Calc sequentially complete bytes from an offset
		bin_t binoff = bin_t(0,(offset - (offset % chunk_size_)) / chunk_size_);
		bin_t nextempty = ack_out_.find_empty(binoff);
		if (nextempty == bin_t::NONE || nextempty.base_offset() * chunk_size_ > size_)
			return size_-offset; // All filled from offset

		bin_t::uint_t diffc = nextempty.layer_offset() - binoff.layer_offset();
		uint64_t diffb = diffc * chunk_size_;
		if (diffb > 0)
			diffb -= (offset % chunk_size_);

		return diffb;
	}
}


MmapHashTree::~MmapHashTree () {
    if (hashes_)
        memory_unmap(hash_fd_, hashes_, sizec_*2*sizeof(Sha1Hash));
    if (hash_fd_)
        close(hash_fd_);
}

