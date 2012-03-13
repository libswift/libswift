/*
 *  availability.h
 *  Tree keeping track of the  availability of each bin in a swarm
 *
 *  Created by Riccardo Petrocco
 *  Copyright 2009-2012 Delft University of Technology. All rights reserved.
 *
 */
#include "availability.h"

using namespace swift;

#define DEBUGAVAILABILITY 	0

// Arno, 2011-12-22: Riccardo to fix.
#ifdef _WIN32
typedef unsigned long long 			uint;
#endif


uint8_t Availability::get(const bin_t bin)
{
	if (bin.is_none())
		return 255;
	else if (size_)
		return avail_[bin.toUInt()];

	return 0;
}


void Availability::setBin(bin_t bin)
{
	if (bin != bin_t::NONE)
	{
		bin_t beg = bin.base_left();
		bin_t end = bin.base_right();

		for (int i = beg.toUInt(); i<=end.toUInt(); i++)
		{
			// for the moment keep a counter
			// TODO make it percentage
			avail_[i]++;
		}
	}

}


void Availability::removeBin(bin_t bin)
{
	bin_t beg = bin.base_left();
	bin_t end = bin.base_right();

	for (int i = beg.toUInt(); i<=end.toUInt(); i++)
	{
		avail_[i]--;
	}
}

void Availability::setBinmap(binmap_t * binmap)
{

	if (binmap->is_filled())
		for (int i=0; i<size_; i++)
			avail_[i]++;
	else
		if (!binmap->is_empty())
		{
			//status();
			bin_t tmp_b;
			binmap_t tmp_bm;
			tmp_b = binmap_t::find_complement(tmp_bm, *binmap, 0);

			while (tmp_b != bin_t::NONE)
			{
				setBin(tmp_b);
				//binmap_t::copy(tmp_bm, *binmap, tmp_b);
				tmp_bm.set(tmp_b);
				tmp_b = binmap_t::find_complement(tmp_bm, *binmap, 0);
			}
			//status();

		}

	return;
}

void Availability::removeBinmap(binmap_t &binmap)
{
	if (binmap.is_filled())
		for (int i=0; i<size_; i++)
			avail_[i]--;
	else
		if (!binmap.is_empty())
		{
			//status();
			bin_t tmp_b;
			binmap_t tmp_bm;
			tmp_b = binmap_t::find_complement(tmp_bm, binmap, 0);
			while (tmp_b != bin_t::NONE)
			{
				removeBin(tmp_b);
				tmp_bm.set(tmp_b);
				tmp_b = binmap_t::find_complement(tmp_bm, binmap, 0);
			}
			//status();
		}

	return;
}

void Availability::set(uint32_t channel_id, binmap_t& binmap, bin_t target)
{
	if (DEBUGAVAILABILITY)
	{
		char bin_name_buf[32];
		dprintf("%s #%u Availability -> setting %s (%llu)\n",tintstr(),channel_id,target.str(bin_name_buf),target.toUInt());
	}

	if (size_>0 && !binmap.is_filled(target))
	{
		bin_t beg = target.base_left();
		bin_t end = target.base_right();

		for (int i = beg.toUInt(); i<=end.toUInt(); i++)
		{
			// for the moment keep a counter
			// TODO make it percentage
			if (!binmap.is_filled(bin_t(i)))
				avail_[i]++;
			//TODO avoid running into sub-trees that r filled
		}
		//status();
	}
	// keep track of the incoming have msgs
	else
		waiting_[channel_id] = &binmap;
}


void Availability::remove(uint32_t channel_id, binmap_t& binmap)
{
	if (DEBUGAVAILABILITY)
	{
		char bin_name_buf[32];
		dprintf("%s #%u Availability -> removing peer\n",tintstr(),channel_id);
	}
	if (size_<=0)
		waiting_[channel_id] = NULL;
	else
		removeBinmap(binmap);
	// remove the binmap from the availability

	return;
}


void Availability::setSize(uint64_t size)
{
	if (size && !size_)
	{
		// TODO can be optimized (bithacks)
		uint r = 0;
		uint s = (int)size;

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
		avail_ = new uint8_t[s]();

		// Initialize with the binmaps we already received
		for (int i=0; i<20; i++)
		{
			if (waiting_[i])
				setBinmap(waiting_[i]);
		}

		if (DEBUGAVAILABILITY)
		{
			char bin_name_buf[32];
			dprintf("%s #1 Availability -> setting size in chunk %lu \t avail size %u\n",tintstr(), size, s);
		}
	}
}

bin_t Availability::getRarest(const bin_t range, int width)
{
	assert(range.toUInt()<size_);
	bin_t curr = range;
	uint idx = range.toUInt();

	while (curr.base_length()>width)
	{
		idx = curr.toUInt();
		if ( avail_[curr.left().toUInt()] <= avail_[curr.right().toUInt()] )
			curr.to_left();
		else
			curr.to_right();
	}
	return curr;
}

void Availability::status() const
{
    printf("availability:\n");

    if (size_ > 0)
    {
		for (int i = 0; i < size_; i++)
			printf("%d", avail_[i]);
    }

    printf("\n");
}



