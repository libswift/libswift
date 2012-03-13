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

namespace {

char* append(char* buf, int x)
{
    char* l = buf;
    char* r = buf;

    if (x < 0) {
        *r++ = '-';
        x = -x;
    }

    do {
        *r++ = '0' + x % 10;
        x /= 10;
    } while (x);

    char* e = r--;

    while (l < r) {
        const char t = *l;
        *l++ = *r;
        *r-- = t;
    }

    *e = '\0';

    return e;
}

char* append(char* buf, bin_t::uint_t x)
{
    char* l = buf;
    char* r = buf;

    do {
        *r++ = '0' + x % 10;
        x /= 10;
    } while (x);

    char* e = r--;

    while (l < r) {
        const char t = *l;
        *l++ = *r;
        *r-- = t;
    }

    *e = '\0';

    return e;
}

char* append(char* buf, const char* s)
{
    char* e = buf;

    while (*s) {
        *e++ = *s++;
    }

    *e = '\0';

    return e;
}

char* append(char* buf, char c)
{
    char* e = buf;

    *e++ = c;
    *e = '\0';

    return e;
}

} /* namespace */


/**
 * Get the standard-form of this bin, e.g. "(2,1)".
 * (buffer should have enough of space)
 */
const char* bin_t::str(char* buf) const
{
    char* e = buf;

    if (is_all()) {
        e = append(e, "(ALL)");
    } else if (is_none()) {
        e = append(e, "(NONE)");
    } else {
        e = append(e, '(');
        e = append(e, layer());
        e = append(e, ',');
        e = append(e, layer_offset());
        e = append(e, ')');
    }

    return buf;
}


/**
 * Output operator
 */
std::ostream & operator << (std::ostream & ostream, const bin_t & bin)
{
    char bin_name_buf[64];
    return ostream << bin.str(bin_name_buf);
}
