#ifndef __bin_utils_h__
#define __bin_utils_h__

#include "bin.h"
#include "compat.h"


/**
 * Generating a list of peak bins for corresponding length
 */
inline int gen_peaks(uint64_t length, bin_t * peaks) {
    int pp = 0;
    uint8_t layer = 0;

    while (length) {
        if (length & 1)
            peaks[pp++] = bin_t(((2 * length - 1) << layer) - 1);
        length >>= 1;
        layer++;
    }

    for(int i = 0; i < (pp >> 1); ++i) {
        bin_t memo = peaks[pp - 1 - i];
        peaks[pp - 1 - i] = peaks[i];
        peaks[i] = memo;
    }

    peaks[pp] = bin_t::NONE;
    return pp;
}


/**
 * Checking for that the bin value is fit to uint32_t
 */
inline bool bin_isUInt32(const bin_t & bin) {
    if( bin.is_all() )
        return true;
    if( bin.is_none() )
        return true;

    const uint64_t v = bin.toUInt();

    return static_cast<uint32_t>(v) == v && v != 0xffffffff && v != 0x7fffffff;
}


/**
 * Convert the bin value to uint32_t
 */
inline uint32_t bin_toUInt32(const bin_t & bin) {
    if( bin.is_all() )
        return 0x7fffffff;
    if( bin.is_none() )
        return 0xffffffff;
    return static_cast<uint32_t>(bin.toUInt());
}


/**
 * Convert the bin value to uint64_t
 */
inline uint64_t bin_toUInt64(const bin_t & bin) {
    return bin.toUInt();
}


/**
 * Restore the bin from an uint32_t value
 */
inline bin_t bin_fromUInt32(uint32_t v) {
    if( v == 0x7fffffff )
        return bin_t::ALL;
    if( v == 0xffffffff )
        return bin_t::NONE;
    return bin_t(static_cast<uint64_t>(v));
}


/**
 * Restore the bin from an uint64_t value
 */
inline bin_t bin_fromUInt64(uint64_t v) {
    return bin_t(static_cast<uint64_t>(v));
}


#endif /*_bin_utils_h__*/
