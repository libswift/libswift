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

FileTransfer::FileTransfer (const char* filename, const Sha1Hash& _root_hash, bool check_hashes, size_t chunk_size) :
ContentTransfer(), hashtree_(filename,_root_hash,chunk_size,NULL,check_hashes)
{
	GlobalAdd();

	if (ENABLE_VOD_PIECEPICKER) {
    	// Ric: init availability
    	availability_ = new Availability();
    	// Ric: TODO assign picker based on input params...
    	picker_ = new VodPiecePicker(this);
    }
    else
    	picker_ = new SeqPiecePicker(this);
    picker_->Randomize(rand()&63);
    init_time_ = Channel::Time();
}


void    Channel::CloseTransfer (ContentTransfer* trans) {
    for(int i=0; i<Channel::channels.size(); i++)
        if (Channel::channels[i] && Channel::channels[i]->transfer_==trans)
        {
        	fprintf(stderr,"Channel::CloseTransfer: delete #%i\n", Channel::channels[i]->id());
        	Channel::channels[i]->Close(); // ARNO
            delete Channel::channels[i];
        }
}


FileTransfer::~FileTransfer ()
{
    Channel::CloseTransfer(this);
    ContentTransfer::swarms[fd()] = NULL;
    delete picker_;
}

