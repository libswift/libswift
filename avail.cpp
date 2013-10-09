/*
 *  availability.h
 *  Tree keeping track of the  availability of each bin in a swarm
 *
 *  Created by Riccardo Petrocco
 *  Copyright 2009-2012 Delft University of Technology. All rights reserved.
 *
 */
#include "swift.h"
#include <cassert>

#define DEBUG

using namespace swift;

#define DEBUGAVAILABILITY 	0


void Availability::setParams(int connections)
{
	if (DEBUGAVAILABILITY)
		fprintf(stderr, "Availability: setting parameters => connections:%d\n", connections);
	connections_=connections;
	initRarity();
}


void Availability::setBin(bin_t bin, int idx)
{
	assert(idx>=0 && idx < connections_);
	assert (!bin.is_none());

	if (DEBUGAVAILABILITY) {
		fprintf(stderr, "Availability: search bin %s [%llu] on rarity %d [", bin.str().c_str(), bin.toUInt(), idx);
		if(rarity_[idx]->is_empty())
			fprintf(stderr, "empty] ");
		else if (rarity_[idx]->is_filled())
			fprintf(stderr, "full] ");
		else
			fprintf(stderr, "partial] ");
	}

	// if the bin is full at this rarity level, set it to empty
	// and propagate the info to higher idx of the array
	// (idx = availability of content)
	if (rarity_[idx]->is_empty(bin)) {
		// if we reached index 0 and the bin is empty
		// it means it's out of our binmap => extend the root bin
		if (idx==0) {
			if (DEBUGAVAILABILITY)
				fprintf(stderr, "empty => set the bin in the binmap with rarity 1.\n");
			rarity_[1]->set(bin);
			assert(rarity_[1]->is_filled(bin));
			return;
		}
		else {
			if (DEBUGAVAILABILITY)
				fprintf(stderr, "empty => search further..\n");
			setBin(bin, idx-1);
		}
	}
	else if (rarity_[idx]->is_filled(bin))
	{
		if (DEBUGAVAILABILITY)
			fprintf(stderr, "full => set bin in the binmap with rarity %d\n", idx+1);
		if (idx==connections_-1)
			return;

		assert(rarity_[idx+1]->is_empty(bin));

		rarity_[idx]->reset(bin);
		rarity_[idx+1]->set(bin);
	}
	else
	{
		assert (!rarity_[idx]->is_empty());
		if (DEBUGAVAILABILITY)
			fprintf(stderr, "partially full!\n");
		setBin(bin.left(), idx);
		setBin(bin.right(), idx);
	}


	return;
}


void Availability::addBinmap(binmap_t * binmap)
{
	if (DEBUGAVAILABILITY)
		dprintf("%s Availability adding binmap ",tintstr());

	if (binmap->is_filled()) {
		bool complete = true;
		for (int idx=0; idx<connections_; idx++) {
			bin_t b = binmap_t::find_complement(*binmap, *rarity_[idx], 0);
			if (!b.is_none()) {
				complete = false;
				break;
			}
		}
		if (complete) {
			if (DEBUGAVAILABILITY)
				dprintf("of a seeder.\n");
			// merge binmaps of max availability
			bin_t b;
			b = binmap_t::find_complement(*rarity_[connections_-1],*rarity_[connections_-2],0);
			while (b!=bin_t::NONE)
			{
				rarity_[connections_-1]->set(b);
				b = binmap_t::find_complement(*rarity_[connections_-1],*rarity_[connections_-2],0);
			}

			for (int i=connections_-2;i>0;i--)
			{
				rarity_[i] = rarity_[i-1];
			}
			return;
		}
	}


	if (!binmap->is_empty())
	{
		if (DEBUGAVAILABILITY)
			dprintf("of a leecher.\n");

		bin_t tmp_b;
		binmap_t tmp_bm;
		tmp_b = binmap_t::find_complement(tmp_bm, *binmap, 0);

		while (tmp_b != bin_t::NONE)
		{
			setBin(tmp_b,connections_-1);
			//binmap_t::copy(tmp_bm, *binmap, tmp_b);
			tmp_bm.set(tmp_b);
			tmp_b = binmap_t::find_complement(tmp_bm, *binmap, 0);
		}
		//status();
	}

	return;
}


void Availability::set(uint32_t channel_id, binmap_t& binmap, bin_t target)
{
	if (DEBUGAVAILABILITY)
		fprintf(stderr, "%s #%u Availability -> setting %s (%llu)\n",tintstr(),channel_id,target.str().c_str(),target.toUInt());

	if (false)
		for (int i=0; i<connections_; i++)
			fprintf(stderr, "%d\t%p\t%s\n", i, rarity_[i], rarity_[i]->is_empty()?"empty":"something there");

	// this functin is called BEFORE the target bin is set in the channel's binmap
	if (!binmap.is_filled(target)) {

	    binmap_t tmp;
	    bin_t bin;
	    //binmap_t::copy(tmp, binmap, target);
	    tmp.set(target);

	    // find newly acked bins
	    do {
	        bin = binmap_t::find_complement(binmap, tmp, target, 0);
	        if (!bin.is_none()) {
	            if (DEBUGAVAILABILITY)
	                fprintf(stderr, "Availability: new bin = %s [%llu]\n", bin.str().c_str(), bin.toUInt());
	            setBin(bin, connections_-1);
	            tmp.reset(bin);
	        }
	    } while (!bin.is_none());

	}
	return;
}

void Availability::find_empty(binmap_t& binmap, bin_t range)
{
	if (binmap.is_empty(range)) {
		setBin(range, connections_-1);
	}
	else {
		if (range.is_base())
			return;

		if (!binmap.is_filled(range.left())) {
			find_empty(binmap, range.to_left());
		}
		if (!binmap.is_filled(range.right()))
			find_empty(binmap, range.to_right());
	}
}

// remove the binmap from the rarity array
void Availability::removeBinmap(uint32_t channel_id, binmap_t& binmap)
{
	if (DEBUGAVAILABILITY)
		fprintf(stderr, "%s #%u Availability -> removing peer ",tintstr(),channel_id);

	// if it's a complete binmap of the file (or of our current knowledge)
	// just move the binmaps down 1 idx in the rarity array
	if (binmap.is_filled()) {
		bool complete = true;
		for (int idx=0; idx<connections_; idx++) {
			bin_t b = binmap_t::find_complement(binmap, *rarity_[idx], 0);
			if (!b.is_none()) {
				complete = false;
				break;
			}
		}
		// basically change index of the array with precautions for first and last element
		if (complete) {
			if (DEBUGAVAILABILITY)
			    fprintf(stderr, "(seeder).\n");
			// merge binmaps of availability 1 and 0
			bin_t b;
			b = binmap_t::find_complement(*rarity_[0],*rarity_[1],0);
			while (b!=bin_t::NONE)
			{
				rarity_[0]->set(b);
				b = binmap_t::find_complement(*rarity_[0],*rarity_[1],0);
			}

			for (int i=1;i<connections_-1;i++)
			{
				rarity_[i] = rarity_[i+1];
			}
			return;
		}
	}

	if (!binmap.is_empty())
	{
		if (DEBUGAVAILABILITY)
			fprintf(stderr, "(leecher).\n");
		bin_t tmp_b;
		binmap_t tmp_bm;
		tmp_b = binmap_t::find_complement(tmp_bm, binmap, 0);
		while (!tmp_b.is_none())
		{
			removeBin(tmp_b, connections_-1);
			tmp_bm.set(tmp_b);
			tmp_b = binmap_t::find_complement(tmp_bm, binmap, 0);
		}
	}

	return;
}

void Availability::removeBin(bin_t bin, int idx)
{
	if (idx==0) {
		if (DEBUGAVAILABILITY)
			fprintf(stderr, "Availability: already at idx 0\n");
		return;
	}

	if (DEBUGAVAILABILITY)
		fprintf(stderr, "Availability: search bin %s [%llu] on rarity %d [%s] ", bin.str().c_str(), bin.toUInt(), idx,  rarity_[idx]->is_empty()?"empty":"full");
	// if the bin is full at this rarity level, set it to empty
	// and propagate the info to higher idx of the array
	// (idx = availability of content)
	if (rarity_[idx]->is_empty(bin)) {
		// if we reached index 0 and the bin is empty
		// it means it's out of our binmap => extend the root bin
		if (idx==0) {
			if (DEBUGAVAILABILITY)
				fprintf(stderr, "empty => extend root of binmap with rarity 1.\n");
			//rarity_[0]->extend_if_needed(bin);
			rarity_[1]->set(bin);
			return;
		}
		else {
			if (DEBUGAVAILABILITY)
				fprintf(stderr, "empty => search further..\n");
			removeBin(bin, idx-1);
		}
	}
	else if (rarity_[idx]->is_filled(bin))
	{
		if (DEBUGAVAILABILITY)
			fprintf(stderr, "full!!\n");
		if (idx==connections_-1)
			return;

		assert(rarity_[idx-1]->is_empty(bin));

		rarity_[idx]->reset(bin);
		rarity_[idx-1]->set(bin);
	}
	else
	{
		assert (!rarity_[idx]->is_empty());
		if (DEBUGAVAILABILITY)
			fprintf(stderr, "partially full!\n");
		removeBin(bin.left(), idx);
		removeBin(bin.right(), idx);
	}
	return;
}

/*void Availability::setSize(uint64_t size)
{
	if (size && !size_)
	{
		// TODO can be optimized (bithacks)
		uint64_t r = 0;
		uint64_t s = size;

		// check if the binmap is not complete
		if (s & (s-1))
		{
			while (s >>= 1)
			{
				r++;
			}
			s = 1<<(r+1);
		}
		// consider higher layers
		s += s-1;
		size_ = s;

		// Initialize with the binmaps we already received
		for(WaitingPeers::iterator vpci = waiting_peers_.begin(); vpci != waiting_peers_.end(); ++vpci)
		{
		    setWaitingBinmap(vpci->second);
		}

		if (DEBUGAVAILABILITY)
		{
		    dprintf("%s #1 Availability -> setting size in chunk %lu \t avail size %lu\n",tintstr(), size, s);
		}
	}
}*/

bin_t Availability::getRarest(const bin_t range, int width)
{
	// TODO
	return bin_t::NONE;
}

void Availability::status() const
{
	for (int i=0; i<connections_; i++)
		fprintf(stderr, "%d\t%p\t%s\n", i, rarity_[i], rarity_[i]->is_empty()?"empty":"full");
	return;
}


void Availability::initRarity() {

	assert (connections_>0);

	rarity_ = new binmap_t*[connections_];

	for (int i=0; i<connections_; i++) {
		rarity_[i]=new binmap_t();
	}

	if (DEBUGAVAILABILITY)
		status();

	return;
}





