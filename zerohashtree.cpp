/*
 *  zerohashtree.cpp
 *  swift
 *
 *  Created by Victor Grishchenko, Arno Bakker
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


/**     0  H a s h   t r e e       */


ZeroHashTree::ZeroHashTree (Storage *storage, const Sha1Hash& root_hash, uint32_t chunk_size, std::string hash_filename, bool check_hashes, std::string binmap_filename) :
storage_(storage), root_hash_(root_hash), peak_count_(0), hash_fd_(0),
 size_(0), sizec_(0), complete_(0), completec_(0),
chunk_size_(chunk_size)
{
	// MULTIFILE
	storage_->SetHashTree(this);

    hash_fd_ = open_utf8(hash_filename.c_str(),OPENFLAGS,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (hash_fd_<0) {
        hash_fd_ = 0;
        print_error("cannot open hash file");
        return;
    }

    if (!RecoverPeakHashes())
    	dprintf("%s zero hashtree could not recover peak hashes, fatal\n",tintstr() );

	complete_ = size_;
	completec_ = sizec_;
}

/** Precondition: root hash known */
bool ZeroHashTree::RecoverPeakHashes()
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
        Sha1Hash peak_hash = hash(peaks[i]);
        if (peak_hash == Sha1Hash::ZERO)
            return false;
        OfferPeakHash(peaks[i], peak_hash);
    }
    if (!this->size())
        return false; // if no valid peak hashes found

    return true;
}


bool            ZeroHashTree::OfferPeakHash (bin_t pos, const Sha1Hash& hash) {
	char bin_name_buf[32];
	dprintf("%s zero hashtree offer peak %s\n",tintstr(),pos.str(bin_name_buf));

    //assert(!size_);
    if (peak_count_) {
        bin_t last_peak = peaks_[peak_count_-1];
        if ( pos.layer()>=last_peak.layer() ||
            pos.base_offset()!=last_peak.base_offset()+last_peak.base_length() )
            peak_count_ = 0;
    }
    peaks_[peak_count_] = pos;
    //peak_hashes_[peak_count_] = hash;
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
    // completec_ = complete_ = 0;
    sizec_ = (size_ + chunk_size_-1) / chunk_size_;

    return true;
}


Sha1Hash        ZeroHashTree::DeriveRoot () {

	dprintf("%s zero hashtree deriving root\n",tintstr() );

    int c = peak_count_-1;
    bin_t p = peaks_[c];
    Sha1Hash hash = peak_hash(c);
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
            hash = Sha1Hash(peak_hash(c),hash);
            p = p.parent();
            c--;
        }
    }
    // fprintf(stderr,"derive: root bin is %lli covers %lli\n", p.toUInt(), p.base_length() );
    return hash;
}

const Sha1Hash& ZeroHashTree::peak_hash (int i) const {
	 // switch to peak_hashes_ when caching enabled
	return hash(peak(i));
}


const Sha1Hash& ZeroHashTree::hash (bin_t pos) const
{
		// RISKY BUSINESS
		static Sha1Hash hash;
        int ret = file_seek(hash_fd_,pos.toUInt()*sizeof(Sha1Hash));
        if (ret < 0)
        {
        	print_error("reading zero hashtree");
        	return Sha1Hash::ZERO;
        }
        ret = read(hash_fd_,&hash,sizeof(Sha1Hash));
        if (ret < 0 || ret !=sizeof(Sha1Hash))
            return Sha1Hash::ZERO;
        else
		{
			//fprintf(stderr,"read hash %llu %s\n", pos.toUInt(), hash.hex().c_str() );
        	return hash;
		}
}


bin_t         ZeroHashTree::peak_for (bin_t pos) const
{
    int pi=0;
    while (pi<peak_count_ && !peaks_[pi].contains(pos))
        pi++;
    return pi==peak_count_ ? bin_t(bin_t::NONE) : peaks_[pi];
}

bool            ZeroHashTree::OfferHash (bin_t pos, const Sha1Hash& hash)
{
	return false;
}


bool            ZeroHashTree::OfferData (bin_t pos, const char* data, size_t length)
{
	return false;
}


uint64_t      ZeroHashTree::seq_complete (int64_t offset)
{
	fprintf(stderr,"ZeroHashTree: seq_complete returns %llu\n", size_ ); 
	return size_;
}


ZeroHashTree::~ZeroHashTree () {
    if (hash_fd_)
        close(hash_fd_);
}

