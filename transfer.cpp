/*
 *  transfer.cpp
 *  some transfer-scope code
 *
 *  Created by Victor Grishchenko on 10/6/09.
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "swift.h"
#include <errno.h>
#include <string>
#include <sstream>

#include "ext/seq_picker.cpp" // FIXME FIXME FIXME FIXME
#include "ext/vod_picker.cpp"
#include "ext/rf_picker.cpp"

using namespace swift;

#define BINHASHSIZE (sizeof(bin64_t)+sizeof(Sha1Hash))

// FIXME: separate Bootstrap() and Download(), then Size(), Progress(), SeqProgress()

FileTransfer::FileTransfer(int td, std::string filename, const Sha1Hash& root_hash, bool force_check_diskvshash, popt_cont_int_prot_t cipm, uint32_t chunk_size, bool zerostate) :
    ContentTransfer(FILE_TRANSFER), availability_(NULL), zerostate_(zerostate)
{
    td_ = td;

    Handshake hs;
    hs.cont_int_prot_ = cipm;
    SetDefaultHandshake(hs);

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
    storage_ = new Storage(filename,destdir,td_,0);

    std::string hash_filename;
    hash_filename.assign(filename);
    hash_filename.append(".mhash");

    std::string binmap_filename;
    binmap_filename.assign(filename);
    binmap_filename.append(".mbinmap");

    // Arno, 2013-02-25: Create HashTree even when PROT_NONE to enable
    // automatic size determination via peak hashes.
    if (!zerostate_)
    {
        hashtree_ = (HashTree *)new MmapHashTree(storage_,root_hash,chunk_size,hash_filename,force_check_diskvshash,binmap_filename);
        availability_ = new Availability(SWIFT_MAX_OUTGOING_CONNECTIONS);

        if (ENABLE_VOD_PIECEPICKER) 
            picker_ = new VodPiecePicker(this);
        else 
            picker_ = new SeqPiecePicker(this);
			//picker_ = new RFPiecePicker(this);
        picker_->Randomize(rand()&63);
    }
    else
    {
	// ZEROHASH
	hashtree_ = (HashTree *)new ZeroHashTree(storage_,root_hash,chunk_size,hash_filename,binmap_filename);
    }

    UpdateOperational();
}


void FileTransfer::UpdateOperational()
{
    if (!hashtree_->IsOperational() || !storage_->IsOperational())
	SetBroken();

    if (zerostate_ && !hashtree_->is_complete())
	SetBroken();
}


FileTransfer::~FileTransfer ()
{
    delete hashtree_;
    hashtree_ = NULL;
    if (!IsZeroState())
    {
        delete picker_;
        picker_ = NULL;
        delete availability_;
        // ~ContentTransfer calls CloseChannels which calls Channel::Close which tries to unregister
        // the availability of that peer from availability_, which has been deallocated here already :-(
        availability_ = NULL;
    }
}

