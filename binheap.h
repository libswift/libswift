/*
 *  sbit.cpp
 *  binmap, a hybrid of bitmap and binary tree
 *
 *  Created by Victor Grishchenko on 3/28/09.
 *  Copyright 2009 Delft University of Technology. All rights reserved.
 *
 */
#ifndef BINS_H
#define BINS_H

#include "bin.h"
#include "compat.h"

class binheap {
    bin_t       *heap_;
    uint32_t    filled_;
    uint32_t    size_;
public:
    binheap();
    bin_t   pop();
    void    push(bin_t);
    bool    empty() const { return !filled_; }
    void    extend();
    ~binheap();
};

#endif
