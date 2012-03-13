/*
 *  bin.h
 *  swift
 *
 *  Created by Victor Grishchenko on 10/10/09.
 *  Reimplemented by Alexander G. Pronchenkov on 05/05/10
 *
 *  Copyright 2010 Delft University of Technology. All rights reserved.
 *
 */

#ifndef __bin_h__
#define __bin_h__

#include <iosfwd>


/**
 * Numbering for (aligned) logarithmic bins.
 *
 * Each number stands for an interval
 *   [layer_offset * 2^layer, (layer_offset + 1) * 2^layer).
 *
 * The following value is called as base_offset:
 *   layer_offset * 2^layer -- is called
 *
 * Bin numbers in the tail111 encoding: meaningless bits in
 * the tail are set to 0111...11, while the head denotes the offset.
 * bin = 2 ^ (layer + 1) * layer_offset + 2 ^ layer - 1
 *
 * Thus, 1101 is the bin at layer 1, offset 3 (i.e. fourth).
 */

/**
 *
 *                  +-----------------00111-----------------+
 *                  |                                       |
 *        +-------00011-------+                   +-------01011-------+
 *        |                   |                   |                   |
 *   +--00001--+         +--00101--+         +--01001--+         +--01101--+
 *   |         |         |         |         |         |         |         |
 * 00000     00010     00100     00110     01000     01010     01100     1110
 *
 *
 *
 *               7
 *           /       \
 *       3              11
 *     /   \           /  \
 *   1       5       9     13
 *  / \     / \     / \    / \
 * 0   2   4   6   8  10  12 14
 *
 * Once we have peak hashes, this structure is more natural than bin-v1
 *
 */

class bin_t {
public:
    /**
     * Basic integer type
     */
    typedef unsigned long long uint_t;


    /**
     * Constants
     */
    static const bin_t NONE;
    static const bin_t ALL;


    /**
     * Constructor
     */
    bin_t(void);


    /**
     * Constructor
     */
    explicit bin_t(uint_t val);


    /**
     * Constructor
     */
    bin_t(int layer, uint_t layer_offset);


    /**
     * Gets the bin value
     */
    uint_t toUInt(void) const;


    /**
     * Operator equal
     */
    bool operator == (const bin_t& bin) const;


    /**
     * Operator non-equal
     */
    bool operator != (const bin_t& bin) const;


    /**
     * Operator less than
     */
    bool operator < (const bin_t& bin) const;


    /**
     * Operator greater than
     */
    bool operator > (const bin_t& bin) const;


    /**
     * Operator less than or equal
     */
    bool operator <= (const bin_t& bin) const;


    /**
     * Operator greater than or equal
     */
    bool operator >= (const bin_t& bin) const;

    /**
     * Decompose the bin
     */
    void decompose(int* layer, uint_t* layer_offset) const;


    /**
     * Gets the beginning of the bin(ary interval)
     */
    uint_t base_offset(void) const;


    /**
     * Gets the length of the bin interval
     */
    uint_t base_length(void) const;


    /**
     * Gets the bin's layer, i.e. log2(base_length)
     */
    int layer(void) const;


    /**
     * Gets the bin layer bits
     */
    uint_t layer_bits(void) const;


    /**
     * Gets the bin layer offset
     */
    uint_t layer_offset(void) const;


    /**
     * Whether the bin is none
     */
    bool is_none(void) const;


    /**
     * Whether the bin is all
     */
    bool is_all(void) const;


    /**
     * Whether the bin is base (layer == 0)
     */
    bool is_base(void) const;


    /**
     * Checks whether is bin is a left child
     */
    bool is_left(void) const;


    /**
     * Checks whether is bin is a left child
     */
    bool is_right(void) const;


    /**
     * Sets this object to the parent
     */
    bin_t& to_parent(void);


    /**
     * Sets this object to the left child
     */
    bin_t& to_left(void);


    /**
     * Sets this object to the right child
     */
    bin_t& to_right(void);


    /**
     * Sets this object to the sibling
     */
    bin_t& to_sibling(void);


    /**
     * Sets this object to the leftmost base sub-bin
     */
    bin_t& to_base_left(void);


    /**
     * Sets this object to the rightmost base sub-bin
     */
    bin_t& to_base_right(void);


    /**
     * Sets this object to the permutated state
     */
    bin_t& to_twisted(uint_t mask);


    /**
     * Sets this object to a layer shifted state
     */
    bin_t& to_layer_shifted(int zlayer);


    /**
     * Gets the parent bin
     */
    bin_t parent(void) const;


    /**
     * Gets the left child
     */
    bin_t left(void) const;


    /**
     * Gets the right child
     */
    bin_t right(void) const;


    /**
     * Gets the sibling bin
     */
    bin_t sibling(void) const;


    /**
     * Gets the leftmost base sub-bin
     */
    bin_t base_left(void) const;


    /**
     * Gets the rightmost base sub-bin
     */
    bin_t base_right(void) const;


    /**
     * Performs a permutation
     */
    bin_t twisted(uint_t mask) const;


    /**
     * Gets the bin after a layer shifting
     */
    bin_t layer_shifted(int zlayer) const;


    /**
     * Checks for contains
     */
    bool contains(const bin_t& bin) const;


    /**
     * Get the standard-form of this bin, e.g. "(2,1)".
     * (buffer should have enough of space)
     */
    const char* str(char* buf) const;


private:

    /** Bin value */
    uint_t v_;
};


/**
 * Output operator
 */
std::ostream & operator << (std::ostream & ostream, const bin_t & bin);


/**
 * Constructor
 */
inline bin_t::bin_t(void)
{ }


/**
 * Constructor
 */
inline bin_t::bin_t(uint_t val)
    : v_(val)
{ }


/**
 * Constructor
 */
inline bin_t::bin_t(int layer, uint_t offset)
{
    if (static_cast<unsigned int>(layer) < 8 * sizeof(uint_t)) {
        v_ = ((2 * offset + 1) << layer) - 1;
    } else {
        v_ = static_cast<uint_t>(-1); // Definition of the NONE bin
    }
}


/**
 * Gets the bin value
 */
inline bin_t::uint_t bin_t::toUInt(void) const
{
    return v_;
}


/**
 * Operator equal
 */
inline bool bin_t::operator == (const bin_t& bin) const
{
    return v_ == bin.v_;
}


/**
 * Operator non-equal
 */
inline bool bin_t::operator != (const bin_t& bin) const
{
    return v_ != bin.v_;
}


/**
 * Operator less than
 */
inline bool bin_t::operator < (const bin_t& bin) const
{
    return v_ < bin.v_;
}


/**
 * Operator great than
 */
inline bool bin_t::operator > (const bin_t& bin) const
{
    return v_ > bin.v_;
}


/**
 * Operator less than or equal
 */
inline bool bin_t::operator <= (const bin_t& bin) const
{
    return v_ <= bin.v_;
}


/**
 * Operator great than or equal
 */
inline bool bin_t::operator >= (const bin_t& bin) const
{
    return v_ >= bin.v_;
}


/**
 * Decompose the bin
 */
inline void bin_t::decompose(int* layer, uint_t* layer_offset) const
{
    const int l = this->layer();
    if (layer) {
        *layer = l;
    }
    if (layer_offset) {
        *layer_offset = v_ >> (l + 1);
    }
}


/**
 * Gets a beginning of the bin interval
 */
inline bin_t::uint_t bin_t::base_offset(void) const
{
    return (v_ & (v_ + 1)) >> 1;
}


/**
 * Gets the length of the bin interval
 */
inline bin_t::uint_t bin_t::base_length(void) const
{
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable:4146)
#endif
    const uint_t t = v_ + 1;
    return t & -t;
#ifdef _MSC_VER
#pragma warning (pop)
#endif
}


/**
 * Gets the layer bits
 */
inline bin_t::uint_t bin_t::layer_bits(void) const
{
    return v_ ^ (v_ + 1);
}


/**
 * Gets the offset value of a bin
 */
inline bin_t::uint_t bin_t::layer_offset(void) const
{
    return v_ >> (layer() + 1);
}


/**
 * Does the bin is none
 */
inline bool bin_t::is_none(void) const
{
    return *this == NONE;
}


/**
 * Does the bin is all
 */
inline bool bin_t::is_all(void) const
{
    return *this == ALL;
}


/**
 * Checks is bin is base (layer == 0)
 */
inline bool bin_t::is_base(void) const
{
    return !(v_ & 1);
}


/**
 * Checks is bin is a left child
 */
inline bool bin_t::is_left(void) const
{
    return !(v_ & (layer_bits() + 1));
}


/**
 * Checks whether is bin is a left child
 */
inline bool bin_t::is_right(void) const
{
    return !is_left();
}


/**
 * Sets this object to the parent
 */
inline bin_t& bin_t::to_parent(void)
{
    const uint_t lbs = layer_bits();
    const uint_t nlbs = -2 - lbs; /* ~(lbs + 1) */

    v_ = (v_ | lbs) & nlbs;

    return *this;
}


/**
 * Sets this object to the left child
 */
inline bin_t& bin_t::to_left(void)
{
    register uint_t t;

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable:4146)
#endif
    t = v_ + 1;
    t &= -t;
    t >>= 1;
#ifdef _MSC_VER
#pragma warning (pop)
#endif

//    if (t == 0) {
//        return NONE;
//    }

    v_ ^= t;

    return *this;
}


/**
* Sets this object to the right child
*/
inline bin_t& bin_t::to_right(void)
{
    register uint_t t;

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable:4146)
#endif
    t = v_ + 1;
    t &= -t;
    t >>= 1;
#ifdef _MSC_VER
#pragma warning (pop)
#endif

//    if (t == 0) {
//        return NONE;
//    }

    v_ += t;

    return *this;
}


/**
 * Sets this object to the sibling
 */
inline bin_t& bin_t::to_sibling(void)
{
    v_ ^= (v_ ^ (v_ + 1)) + 1;

    return *this;
}


/**
 * Sets this object to the leftmost base sub-bin
 */
inline bin_t& bin_t::to_base_left(void)
{
    if (!is_none()) {
        v_ &= (v_ + 1);
    }

    return *this;
}


/**
 * Sets this object to the rightmost base sub-bin
 */
inline bin_t& bin_t::to_base_right(void)
{
    if (!is_none()) {
        v_ = (v_ | (v_ + 1)) - 1;
    }

    return *this;
}


/**
 * Performs a permutation
 */
inline bin_t& bin_t::to_twisted(uint_t mask)
{
    v_ ^= ((mask << 1) & ~layer_bits());

    return *this;
}


/**
 * Sets this object to a layer shifted state
 */
inline bin_t& bin_t::to_layer_shifted(int zlayer)
{
    if (layer_bits() >> zlayer) {
        v_ >>= zlayer;
    } else {
        v_ = (v_ >> zlayer) & ~static_cast<uint_t>(1);
    }

    return *this;
}


/**
 * Gets the parent bin
 */
inline bin_t bin_t::parent(void) const
{
    const uint_t lbs = layer_bits();
    const uint_t nlbs = -2 - lbs; /* ~(lbs + 1) */

    return bin_t((v_ | lbs) & nlbs);
}


/**
 * Gets the left child
 */
inline bin_t bin_t::left(void) const
{
    register uint_t t;

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable:4146)
#endif
    t = v_ + 1;
    t &= -t;
    t >>= 1;
#ifdef _MSC_VER
#pragma warning (pop)
#endif

//    if (t == 0) {
//        return NONE;
//    }

    return bin_t(v_ ^ t);
}


/**
 * Gets the right child
 */
inline bin_t bin_t::right(void) const
{
    register uint_t t;

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable:4146)
#endif
    t = v_ + 1;
    t &= -t;
    t >>= 1;
#ifdef _MSC_VER
#pragma warning (pop)
#endif

//    if (t == 0) {
//        return NONE;
//    }

    return bin_t(v_ + t);
}


/**
 * Gets the sibling bin
 */
inline bin_t bin_t::sibling(void) const
{
    return bin_t(v_ ^ (layer_bits() + 1));
}


/**
 * Gets the leftmost base sub-bin
 */
inline bin_t bin_t::base_left(void) const
{
    if (is_none()) {
        return NONE;
    }

    return bin_t(v_ & (v_ + 1));
}


/**
 * Gets the rightmost base sub-bin
 */
inline bin_t bin_t::base_right(void) const
{
    if (is_none()) {
        return NONE;
    }

    return bin_t((v_ | (v_ + 1)) - 1);
}


/**
 * Performs a permutation
 */
inline bin_t bin_t::twisted(uint_t mask) const
{
    return bin_t( v_ ^ ((mask << 1) & ~layer_bits()) );
}


/**
 * Gets the bin after a layer shifting
 */
inline bin_t bin_t::layer_shifted(int zlayer) const
{
    if (layer_bits() >> zlayer) {
        return bin_t( v_  >> zlayer );
    } else {
        return bin_t( (v_ >> zlayer) & ~static_cast<uint_t>(1) );
    }
}


/**
 * Checks for contains
 */
inline bool bin_t::contains(const bin_t& bin) const
{
    if (is_none()) {
        return false;
    }

    return (v_ & (v_ + 1)) <= bin.v_ && bin.v_ < (v_ | (v_ + 1));
}


#endif /*_bin_h__*/
