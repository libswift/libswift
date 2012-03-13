/*
 *  sbit.cpp
 *  serp++
 *
 *  Created by Victor Grishchenko on 4/1/09.
 *  Copyright 2009 Delft University of Technology. All rights reserved.
 *
 */

#include <algorithm>
#include <cstdlib>

#include "binheap.h"


binheap::binheap() {
    size_ = 32;
    heap_ = (bin_t*) malloc(size_*sizeof(bin_t));
    filled_ = 0;
}

bool bincomp (const bin_t& a, const bin_t& b) {
    register uint64_t ab = a.base_offset(), bb = b.base_offset();
    if (ab==bb)
        return a.layer_bits() < b.layer_bits();
    else
        return ab > bb;
}

bool bincomp_rev (const bin_t& a, const bin_t& b) {
    register uint64_t ab = a.base_offset(), bb = b.base_offset();
    if (ab==bb)
        return a.layer_bits() > b.layer_bits();
    else
        return ab < bb;
}

bin_t binheap::pop() {
    if (!filled_)
        return bin_t::NONE;
    bin_t ret = heap_[0];
    std::pop_heap(heap_, heap_+filled_--,bincomp);
    while (filled_ && ret.contains(heap_[0]))
        std::pop_heap(heap_, heap_+filled_--,bincomp);
    return ret;
}

void    binheap::extend() {
    std::sort(heap_,heap_+filled_,bincomp_rev);
    int solid = 0;
    for(int i=1; i<filled_; i++)
        if (!heap_[solid].contains(heap_[i]))
            heap_[++solid] = heap_[i];
    filled_ = solid+1;
    if (2*filled_>size_) {
        size_ <<= 1;
        heap_ = (bin_t*) realloc(heap_,size_*sizeof(bin_t));
    }
}

void    binheap::push(bin_t val) {
    if (filled_==size_)
        extend();
    heap_[filled_++] = val;
    std::push_heap(heap_, heap_+filled_,bincomp);
}

binheap::~binheap() {
    free(heap_);
}

