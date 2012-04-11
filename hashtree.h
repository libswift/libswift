/*
 *  hashtree.h
 *  hashing, Merkle hash trees and data integrity
 *
 *  Created by Victor Grishchenko on 3/6/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#ifndef SWIFT_SHA1_HASH_TREE_H
#define SWIFT_SHA1_HASH_TREE_H
#include <string.h>
#include <string>
#include "bin.h"
#include "binmap.h"

namespace swift {

#define HASHSZ 20

/** SHA-1 hash, 20 bytes of data */
struct Sha1Hash {
    uint8_t    bits[HASHSZ];

    Sha1Hash() { memset(bits,0,HASHSZ); }
    /** Make a hash of two hashes (for building Merkle hash trees). */
    Sha1Hash(const Sha1Hash& left, const Sha1Hash& right);
    /** Hash an old plain string. */
    Sha1Hash(const char* str, size_t length=-1);
    Sha1Hash(const uint8_t* data, size_t length);
    /** Either parse hash from hex representation of read in raw format. */
    Sha1Hash(bool hex, const char* hash);
    
    std::string    hex() const;
    bool    operator == (const Sha1Hash& b) const
        { return 0==memcmp(bits,b.bits,SIZE); }
    bool    operator != (const Sha1Hash& b) const { return !(*this==b); }
    const char* operator * () const { return (char*) bits; }
    
    const static Sha1Hash ZERO;
    const static size_t SIZE = HASHSZ;
};

// Arno: The chunk size parameter can now be configured via the constructor,
// for values up to 8192. Above that you'll have to edit the
// SWIFT_MAX_SEND_DGRAM_SIZE in swift.h
//
#define SWIFT_DEFAULT_CHUNK_SIZE 1024


class Storage;

/** This class controls data integrity of some file; hash tree is put to
    an auxilliary file next to it. The hash tree file is mmap'd for
    performance reasons. Actually, I'd like the data file itself to be
    mmap'd, but 32-bit platforms do not allow that for bigger files. 
 
    There are two variants of the general workflow: either a HashTree
    is initialized with a root hash and the rest of hashes and data is
    spoon-fed later, OR a HashTree is initialized with a data file, so
    the hash tree is derived, including the root hash.
 */
class HashTree : Serializable {
    /** Merkle hash tree: root */
    Sha1Hash        root_hash_;
    Sha1Hash        *hashes_;
    /** Merkle hash tree: peak hashes */
    Sha1Hash        peak_hashes_[64];
    bin_t           peaks_[64];
    int             peak_count_;
    /** File descriptor to put hashes to */
    int             hash_fd_;
    std::string		filename_; // for easy serialization
    /** Base size, as derived from the hashes. */
    uint64_t        size_;
    uint64_t        sizec_;
    /**    Part of the tree currently checked. */
    uint64_t        complete_;
    uint64_t        completec_;
    /**    Binmap of own chunk availability */
    binmap_t        ack_out_;

	// CHUNKSIZE
	/** Arno: configurable fixed chunk size in bytes */
    uint32_t			chunk_size_;

	// LESSHASH
	binmap_t		is_hash_verified_; // binmap being abused as bitmap, only layer 0 used
	// FAXME: make is_hash_verified_ part of persistent state?

	//MULTIFILE
	Storage *		storage_;

    int 			internal_deserialize(FILE *fp,bool contentavail=true);

protected:
    
    void            Submit();
    void            RecoverProgress();
    bool 			RecoverPeakHashes();
    Sha1Hash        DeriveRoot();
    bool            OfferPeakHash (bin_t pos, const Sha1Hash& hash);

    
public:
    
    HashTree (Storage *storage, const Sha1Hash& root=Sha1Hash::ZERO, uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE,
              std::string hash_filename=NULL, bool check_hashes=true, std::string binmap_filename=NULL);
    
    // Arno, 2012-01-03: Hack to quickly learn root hash from a checkpoint
    HashTree (bool dummy, std::string binmap_filename);

    /** Offer a hash; returns true if it verified; false otherwise.
     Once it cannot be verified (no sibling or parent), the hash
     is remembered, while returning false. */
    bool            OfferHash (bin_t pos, const Sha1Hash& hash);
    /** Offer data; the behavior is the same as with a hash:
     accept or remember or drop. Returns true => ACK is sent. */
    bool            OfferData (bin_t bin, const char* data, size_t length);
    /** For live streaming. Not implemented yet. */
    int             AppendData (char* data, int length) ;
    
    /** Returns the number of peaks (read on peak hashes). */
    int             peak_count () const { return peak_count_; }
    /** Returns the i-th peak's bin number. */
    bin_t           peak (int i) const { return peaks_[i]; }
    /** Returns peak hash #i. */
    const Sha1Hash& peak_hash (int i) const { return peak_hashes_[i]; }
    /** Return the peak bin the given bin belongs to. */
    bin_t           peak_for (bin_t pos) const;
    /** Return a (Merkle) hash for the given bin. */
    const Sha1Hash& hash (bin_t pos) const {return hashes_[pos.toUInt()];}
    /** Give the root hash, which is effectively an identifier of this file. */
    const Sha1Hash& root_hash () const { return root_hash_; }
    /** Get file size, in bytes. */
    uint64_t        size () const { return size_; }
    /** Get file size in chunks (in kilobytes, rounded up). */
    uint64_t        size_in_chunks () const { return sizec_; }
    /** Number of bytes retrieved and checked. */
    uint64_t        complete () const { return complete_; }
    /** Number of chunks retrieved and checked. */
    uint64_t        chunks_complete () const { return completec_; }
    /** The number of bytes completed sequentially, i.e. from the beginning of
        the file, uninterrupted. */
    uint64_t        seq_complete () ;
    /** Whether the file is complete. */
    bool            is_complete () 
        { return size_ && complete_==size_; }
    /** The binmap of complete chunks. */
    binmap_t&       ack_out () { return ack_out_; }
    uint32_t		chunk_size() { return chunk_size_; } // CHUNKSIZE
    ~HashTree ();

    // for transfertest.cpp
    Storage *       get_storage() { return storage_; }
    void            set_size(uint64_t size) { size_ = size; }

    // Arno: persistent storage for state other than hashes (which are in .mhash)
    int serialize(FILE *fp);
    int deserialize(FILE *fp);
    int partial_deserialize(FILE *fp);
};

}

#endif
