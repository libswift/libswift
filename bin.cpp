/*
 *  bin.cpp
 *  swift
 *
 *  Created by Victor Grishchenko on 10/10/09.
 *  Reimplemented by Alexander G. Pronchenkov on 05/05/10
 *
 *  Copyright 2010 Delft University of Technology. All rights reserved.
 *
 */

#include "bin.h"
#include <ostream>
#include <sstream>

const bin_t bin_t::NONE(8 * sizeof(bin_t::uint_t), 0);
const bin_t bin_t::ALL(8 * sizeof(bin_t::uint_t) - 1, 0);


/* Methods */

/**
 * Gets the layer value of a bin
 */
int bin_t::layer(void) const
{
    if (is_none()) {
        return -1;
    }

    int r = 0;

#ifdef _MSC_VER
#  pragma warning (push)
#  pragma warning (disable:4146)
#endif
    register uint_t tail;
    tail = v_ + 1;
    tail = tail & (-tail);
#ifdef _MSC_VER
#  pragma warning (pop)
#endif

    if (tail > 0x80000000U) {
        r = 32;
        tail >>= 16;    // FIXME: hide warning
        tail >>= 16;
    }

    // courtesy of Sean Eron Anderson
    // http://graphics.stanford.edu/~seander/bithacks.html
    static const char DeBRUIJN[32] = { 0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8, 31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9 };

    return r + DeBRUIJN[ 0x1f & ((tail * 0x077CB531U) >> 27) ];
}

/* String operations */

/**
 * Get the standard-form of this bin, e.g. "(2,1)".
 */
std::string bin_t::str() const
{
    if (is_all()) {
        return "(ALL)";
    } else if (is_none()) {
        return "(NONE)";
    } else {
        std::ostringstream cross;
        cross << "(";
        cross << layer();
        cross << ",";
        cross << layer_offset();
        cross << ")";
        return cross.str();
    }
}


/**
 * Output operator
 */
std::ostream & operator << (std::ostream & ostream, const bin_t & bin)
{
    return ostream << bin.str();
}


bool bin_sort_on_layer_cmp(bin_t i, bin_t j)
{
    if (i.layer() == j.layer())
        return i.layer_offset() < j.layer_offset();
    else
        return i.layer() < j.layer();
}
