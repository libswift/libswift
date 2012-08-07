/*
 *  transfer.cpp
 *  some transfer-scope code
 *
 *  Created by Victor Grishchenko on 10/6/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include <errno.h>
#include <string>
#include <sstream>
#include "swift.h"

#include "ext/seq_picker.cpp" // FIXME FIXME FIXME FIXME
#include "ext/vod_picker.cpp"

using namespace swift;

#define BINHASHSIZE (sizeof(bin64_t)+sizeof(Sha1Hash))

// FIXME: separate Bootstrap() and Download(), then Size(), Progress(), SeqProgress()

FileTransfer::FileTransfer(std::string filename, const Sha1Hash& root_hash, bool check_hashes, uint32_t chunk_size, bool zerostate) :
    ContentTransfer(FILE_TRANSFER), zerostate_(zerostate)
{
    std::string destdir;
    int ret = file_exists_utf8(filename);
    if (ret == 2 && root_hash != Sha1Hash::ZERO) {
        // Filename is a directory, download root_hash there
        destdir = filename;
        filename = destdir+FILE_SEP+root_hash.hex();
    } else {
        destdir = dirname_utf8(filename);
        if (destdir == "")
            destdir = ".";
    }

    // MULTIFILE
    storage_ = new Storage(filename,destdir,fd());

    std::string hash_filename;
    hash_filename.assign(filename);
    hash_filename.append(".mhash");

    std::string binmap_filename;
    binmap_filename.assign(filename);
    binmap_filename.append(".mbinmap");

    if (!zerostate_)
    {
        hashtree_ = (HashTree *)new MmapHashTree(storage_,root_hash,chunk_size,hash_filename,check_hashes,binmap_filename);

        if (ENABLE_VOD_PIECEPICKER) {
            // Ric: init availability
            availability_ = new Availability();
            // Ric: TODO assign picker based on input params...
            picker_ = new VodPiecePicker(this);
        }
        else
            picker_ = new SeqPiecePicker(this);
        picker_->Randomize(rand()&63);
    }
    else
    {
        // ZEROHASH
        hashtree_ = (HashTree *)new ZeroHashTree(storage_,root_hash,chunk_size,hash_filename,check_hashes,binmap_filename);
    }

    init_time_ = Channel::Time();
}


FileTransfer::~FileTransfer ()
{
    delete hashtree_;
    if (!IsZeroState())
    {
        delete picker_;
        delete availability_;
    }
}


