#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include <iostream>


#include "binmap.h"

using namespace swift;

namespace swift {

inline size_t _max_(const size_t x, const size_t y)
{
    return x < y ? y : x;
}

typedef binmap_t::ref_t ref_t;
typedef binmap_t::bitmap_t bitmap_t;

/* Bitmap constants */
const bitmap_t BITMAP_EMPTY  = static_cast<bitmap_t>(0);
const bitmap_t BITMAP_FILLED = static_cast<bitmap_t>(-1);

const bin_t::uint_t BITMAP_LAYER_BITS = 2 * 8 * sizeof(bitmap_t) - 1;

const ref_t ROOT_REF = 0;

#ifdef _MSC_VER
#  pragma warning (push)
#  pragma warning ( disable:4309 )
#endif

const bitmap_t BITMAP[] = {
    static_cast<bitmap_t>(0x00000001), static_cast<bitmap_t>(0x00000003),
    static_cast<bitmap_t>(0x00000002), static_cast<bitmap_t>(0x0000000f),
    static_cast<bitmap_t>(0x00000004), static_cast<bitmap_t>(0x0000000c),
    static_cast<bitmap_t>(0x00000008), static_cast<bitmap_t>(0x000000ff),
    static_cast<bitmap_t>(0x00000010), static_cast<bitmap_t>(0x00000030),
    static_cast<bitmap_t>(0x00000020), static_cast<bitmap_t>(0x000000f0),
    static_cast<bitmap_t>(0x00000040), static_cast<bitmap_t>(0x000000c0),
    static_cast<bitmap_t>(0x00000080), static_cast<bitmap_t>(0x0000ffff),
    static_cast<bitmap_t>(0x00000100), static_cast<bitmap_t>(0x00000300),
    static_cast<bitmap_t>(0x00000200), static_cast<bitmap_t>(0x00000f00),
    static_cast<bitmap_t>(0x00000400), static_cast<bitmap_t>(0x00000c00),
    static_cast<bitmap_t>(0x00000800), static_cast<bitmap_t>(0x0000ff00),
    static_cast<bitmap_t>(0x00001000), static_cast<bitmap_t>(0x00003000),
    static_cast<bitmap_t>(0x00002000), static_cast<bitmap_t>(0x0000f000),
    static_cast<bitmap_t>(0x00004000), static_cast<bitmap_t>(0x0000c000),
    static_cast<bitmap_t>(0x00008000), static_cast<bitmap_t>(0xffffffff),
    static_cast<bitmap_t>(0x00010000), static_cast<bitmap_t>(0x00030000),
    static_cast<bitmap_t>(0x00020000), static_cast<bitmap_t>(0x000f0000),
    static_cast<bitmap_t>(0x00040000), static_cast<bitmap_t>(0x000c0000),
    static_cast<bitmap_t>(0x00080000), static_cast<bitmap_t>(0x00ff0000),
    static_cast<bitmap_t>(0x00100000), static_cast<bitmap_t>(0x00300000),
    static_cast<bitmap_t>(0x00200000), static_cast<bitmap_t>(0x00f00000),
    static_cast<bitmap_t>(0x00400000), static_cast<bitmap_t>(0x00c00000),
    static_cast<bitmap_t>(0x00800000), static_cast<bitmap_t>(0xffff0000),
    static_cast<bitmap_t>(0x01000000), static_cast<bitmap_t>(0x03000000),
    static_cast<bitmap_t>(0x02000000), static_cast<bitmap_t>(0x0f000000),
    static_cast<bitmap_t>(0x04000000), static_cast<bitmap_t>(0x0c000000),
    static_cast<bitmap_t>(0x08000000), static_cast<bitmap_t>(0xff000000),
    static_cast<bitmap_t>(0x10000000), static_cast<bitmap_t>(0x30000000),
    static_cast<bitmap_t>(0x20000000), static_cast<bitmap_t>(0xf0000000),
    static_cast<bitmap_t>(0x40000000), static_cast<bitmap_t>(0xc0000000),
    static_cast<bitmap_t>(0x80000000), /* special */ static_cast<bitmap_t>(0xffffffff) /* special */
};

#ifdef _MSC_VER
#pragma warning (pop)
#endif


/**
 * Get the leftmost bin that corresponded to bitmap (the bin is filled in bitmap)
 */
bin_t::uint_t bitmap_to_bin(register bitmap_t b)
{
    static const unsigned char BITMAP_TO_BIN[] = {
      0xff, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
         8, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
        10, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
         9, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
        12, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
         8, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
        10, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
         9, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
        14, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
         8, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
        10, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
         9, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
        13, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
         8, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
        10, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 3,
        11, 0, 2, 1, 4, 0, 2, 1, 6, 0, 2, 1, 5, 0, 2, 7
    };

    assert (sizeof(bitmap_t) <= 4);
    assert (b != BITMAP_EMPTY);

    unsigned char t;

    t = BITMAP_TO_BIN[ b & 0xff ];
    if (t < 16) {
        if (t != 7) {
            return static_cast<bin_t::uint_t>(t);
        }

        b += 1;
        b &= -b;
        if (0 == b) {
            return BITMAP_LAYER_BITS / 2;
        }
        if (0 == (b & 0xffff)) {
            return 15;
        }
        return 7;
    }

    b >>= 8;
    t = BITMAP_TO_BIN[ b & 0xff ];
    if (t <= 15) {
        return 16 + t;
    }

    /* Recursion */
    // return 32 + bitmap_to_bin( b >> 8 );

    assert (sizeof(bitmap_t) == 4);

    b >>= 8;
    t = BITMAP_TO_BIN[ b & 0xff ];
    if (t < 16) {
        if (t != 7) {
            return 32 + static_cast<bin_t::uint_t>(t);
        }

        b += 1;
        b &= -b;
        if (0 == (b & 0xffff)) {
            return 47;
        }
        return 39;
    }

    b >>= 8;
    return 48 + BITMAP_TO_BIN[ b & 0xff ];
}


/**
 * Get the leftmost bin that corresponded to bitmap (the bin is filled in bitmap)
 */
bin_t bitmap_to_bin(const bin_t& bin, const bitmap_t bitmap)
{
    assert (bitmap != BITMAP_EMPTY);

    if (bitmap == BITMAP_FILLED) {
        return bin;
    }

    return bin_t(bin.base_left().toUInt() + bitmap_to_bin(bitmap));
}

} /* namespace */


/* Methods */


/**
 * Constructor
 */
binmap_t::binmap_t()
    : root_bin_(63)
{
    assert (sizeof(bitmap_t) <= 4);

    cell_ = NULL;
    cells_number_ = 0;
    allocated_cells_number_ = 0;
    free_top_ = ROOT_REF;

    const ref_t root_ref = alloc_cell();

    assert (root_ref == ROOT_REF && cells_number_ > 0);
}


/**
 * Destructor
 */
binmap_t::~binmap_t()
{
    if (cell_) {
        free(cell_);
    }
}


/**
 * Allocates one cell (dirty allocation)
 */
ref_t binmap_t::_alloc_cell()
{
    assert (allocated_cells_number_ < cells_number_);

    /* Pop an element from the free cell list */
    const ref_t ref = free_top_;
    assert (cell_[ref].is_free_);

    free_top_ = cell_[ ref ].free_next_;

    assert (!(cell_[ ref ].is_free_ = false));   /* Reset flag in DEBUG */

    ++allocated_cells_number_;

    return ref;
}


/**
 * Allocates one cell
 */
ref_t binmap_t::alloc_cell()
{
    if (!reserve_cells(1)) {
        return ROOT_REF /* MEMORY ERROR or OVERFLOW ERROR */;
    }

    const ref_t ref = _alloc_cell();

    /* Cleans cell */
    memset(&cell_[ref], 0, sizeof(cell_[0]));

    return ref;
}


/**
 * Reserve cells allocation capacity
 */
bool binmap_t::reserve_cells(size_t count)
{
    if (cells_number_ - allocated_cells_number_ < count) {
        /* Finding new sizeof of the buffer */
        const size_t old_cells_number = cells_number_;
        const size_t new_cells_number = _max_(16U, _max_(2 * old_cells_number, allocated_cells_number_ + count));

        /* Check for reference capacity */
        if (static_cast<ref_t>(new_cells_number) < old_cells_number) {
            fprintf(stderr, "Warning: binmap_t::reserve_cells: REFERENCE LIMIT ERROR\n");
            return false /* REFERENCE LIMIT ERROR */;
        }

        /* Check for integer overflow */
        static const size_t MAX_NUMBER = (static_cast<size_t>(-1) / sizeof(cell_[0]));
        if (MAX_NUMBER < new_cells_number) {
            fprintf(stderr, "Warning: binmap_t::reserve_cells: INTEGER OVERFLOW\n");
            return false /* INTEGER OVERFLOW */;
        }

        /* Reallocate memory */
        cell_t* const cell = static_cast<cell_t*>(realloc(cell_, new_cells_number * sizeof(cell_[0])));
        if (cell == NULL) {
            fprintf(stderr, "Warning: binmap_t::reserve_cells: MEMORY ERROR\n");
            return false /* MEMORY ERROR */;
        }

        cell_ = cell;
        cells_number_ = new_cells_number;

        /* Insert new cells to the free cell list */
        const size_t stop_idx = old_cells_number - 1;
        size_t idx = new_cells_number - 1;

        cell_[ idx ].is_free_ = true;
        cell_[ idx ].free_next_ = free_top_;

        for (--idx; idx != stop_idx; --idx) {
            cell_[ idx ].is_free_ = true;
            cell_[ idx ].free_next_ = static_cast<ref_t>(idx + 1);
        }

        free_top_ = static_cast<ref_t>(old_cells_number);
    }

    return true;
}


/**
 * Releases the cell
 */
void binmap_t::free_cell(ref_t ref)
{
    assert (ref > 0);
    assert (!cell_[ref].is_free_);

    if (cell_[ref].is_left_ref_) {
        free_cell(cell_[ref].left_.ref_);
    }
    if (cell_[ref].is_right_ref_) {
        free_cell(cell_[ref].right_.ref_);
    }
    assert ((cell_[ref].is_free_ = true)); /* Set flag in DEBUG */
    cell_[ref].free_next_ = free_top_;

    free_top_ = ref;

    --allocated_cells_number_;
}


/**
 * Extend root
 */
bool binmap_t::extend_root()
{
    assert (!root_bin_.is_all());

    if (!cell_[ROOT_REF].is_left_ref_ && !cell_[ROOT_REF].is_right_ref_ && cell_[ROOT_REF].left_.bitmap_ == cell_[ROOT_REF].right_.bitmap_) {
        /* Setup the root cell */
        cell_[ROOT_REF].right_.bitmap_ = BITMAP_EMPTY;

    } else {
        /* Allocate new cell */
        const ref_t ref = alloc_cell();
        if (ref == ROOT_REF) {
            return false /* ALLOC ERROR */;
        }

        /* Move old root to the cell */
        cell_[ref] = cell_[ROOT_REF];

        /* Setup new root */
        cell_[ROOT_REF].is_left_ref_ = true;
        cell_[ROOT_REF].is_right_ref_ = false;

        cell_[ROOT_REF].left_.ref_ = ref;
        cell_[ROOT_REF].right_.bitmap_ = BITMAP_EMPTY;
    }

    /* Reset bin */
    root_bin_.to_parent();
    return true;
}


/**
 * Pack a trace of cells
 */
void binmap_t::pack_cells(ref_t* href)
{
    ref_t ref = *href--;
    if (ref == ROOT_REF) {
        return;
    }

    if (cell_[ref].is_left_ref_ || cell_[ref].is_right_ref_ ||
        cell_[ref].left_.bitmap_ != cell_[ref].right_.bitmap_) {
        return;
    }

    const bitmap_t bitmap = cell_[ref].left_.bitmap_;

    do {
        ref = *href--;

        if (!cell_[ref].is_left_ref_) {
            if (cell_[ref].left_.bitmap_ != bitmap) {
                break;
            }

        } else if (!cell_[ref].is_right_ref_) {
            if (cell_[ref].right_.bitmap_ != bitmap) {
                break;
            }

        } else {
            break;
        }

    } while (ref != ROOT_REF);

    const ref_t par_ref = href[2];

    if (cell_[ref].is_left_ref_ && cell_[ref].left_.ref_ == par_ref) {
        cell_[ref].is_left_ref_ = false;
        cell_[ref].left_.bitmap_ = bitmap;
    } else {
        cell_[ref].is_right_ref_ = false;
        cell_[ref].right_.bitmap_ = bitmap;
    }

    free_cell(par_ref);
}


/**
 * Whether binmap is empty
 */
bool binmap_t::is_empty() const
{
    const cell_t& cell = cell_[ROOT_REF];

    return !cell.is_left_ref_ && !cell.is_right_ref_ &&
           cell.left_.bitmap_ == BITMAP_EMPTY && cell.right_.bitmap_ == BITMAP_EMPTY;
}


/**
 * Whether binmap is filled
 */
bool binmap_t::is_filled() const
{
    const cell_t& cell = cell_[ROOT_REF];

    return root_bin_.is_all() && !cell.is_left_ref_ && !cell.is_right_ref_ &&
           cell.left_.bitmap_ == BITMAP_FILLED && cell.right_.bitmap_ == BITMAP_FILLED;
}


/**
 * Whether range/bin is empty
 */
bool binmap_t::is_empty(const bin_t& bin) const
{
    /* Process hi-layers case */
    if (!root_bin_.contains(bin)) {
        return !bin.contains(root_bin_) || is_empty();
    }

    /* Trace the bin */
    ref_t cur_ref;
    bin_t cur_bin;

    trace(&cur_ref, &cur_bin, bin);

    assert (cur_bin.layer_bits() > BITMAP_LAYER_BITS);

    /* Process common case */
    const cell_t& cell = cell_[cur_ref];

    if (bin.layer_bits() > BITMAP_LAYER_BITS) {
        if (bin < cur_bin) {
            return cell.left_.bitmap_ == BITMAP_EMPTY;
        }
        if (cur_bin < bin) {
            return cell.right_.bitmap_ == BITMAP_EMPTY;
        }
        return !cell.is_left_ref_ && !cell.is_right_ref_ &&
                cell.left_.bitmap_ == BITMAP_EMPTY && cell.right_.bitmap_ == BITMAP_EMPTY;
    }

    /* Process low-layers case */
    assert (bin != cur_bin);

    const bitmap_t bm1 = (bin < cur_bin) ? cell.left_.bitmap_ : cell.right_.bitmap_;
    const bitmap_t bm2 = BITMAP[ BITMAP_LAYER_BITS & bin.toUInt() ];

    return (bm1 & bm2) == BITMAP_EMPTY;
}


/**
 * Whether range/bin is filled
 */
bool binmap_t::is_filled(const bin_t& bin) const
{
    /* Process hi-layers case */
    if (!root_bin_.contains(bin)) {
        return false;
    }

    /* Trace the bin */
    ref_t cur_ref;
    bin_t cur_bin;

    trace(&cur_ref, &cur_bin, bin);

    assert (cur_bin.layer_bits() > BITMAP_LAYER_BITS);

    /* Process common case */
    const cell_t& cell = cell_[cur_ref];

    if (bin.layer_bits() > BITMAP_LAYER_BITS) {
        if (bin < cur_bin) {
            return cell.left_.bitmap_ == BITMAP_FILLED;
        }
        if (cur_bin < bin) {
            return cell.right_.bitmap_ == BITMAP_FILLED;
        }
        return !cell.is_left_ref_ && !cell.is_right_ref_ &&
               cell.left_.bitmap_ == BITMAP_FILLED && cell.right_.bitmap_ == BITMAP_FILLED;
    }

    /* Process low-layers case */
    assert (bin != cur_bin);

    const bitmap_t bm1 = (bin < cur_bin) ? cell.left_.bitmap_ : cell.right_.bitmap_;
    const bitmap_t bm2 = BITMAP[ BITMAP_LAYER_BITS & bin.toUInt() ];

    return (bm1 & bm2) == bm2;
}


/**
 * Return the topmost solid bin which covers the specified bin
 */
bin_t binmap_t::cover(const bin_t& bin) const
{
    /* Process hi-layers case */
    if (!root_bin_.contains(bin)) {
        if (!bin.contains(root_bin_)) {
            return root_bin_.sibling();
        }
        if (is_empty()) {
            return bin_t::ALL;
        }
        return bin_t::NONE;
    }

    /* Trace the bin */
    ref_t cur_ref;
    bin_t cur_bin;

    trace(&cur_ref, &cur_bin, bin);

    assert (cur_bin.layer_bits() > BITMAP_LAYER_BITS);

    /* Process common case */
    const cell_t& cell = cell_[cur_ref];

    if (bin.layer_bits() > BITMAP_LAYER_BITS) {
        if (bin < cur_bin) {
            if (cell.left_.bitmap_ == BITMAP_EMPTY || cell.left_.bitmap_ == BITMAP_FILLED) {
                return cur_bin.left();
            }
            return bin_t::NONE;
        }
        if (cur_bin < bin) {
            if (cell.right_.bitmap_ == BITMAP_EMPTY || cell.right_.bitmap_ == BITMAP_FILLED) {
                return cur_bin.right();
            }
            return bin_t::NONE;
        }
        if (cell.is_left_ref_ || cell.is_right_ref_) {
            return bin_t::NONE;
        }
        if (cell.left_.bitmap_ != cell.right_.bitmap_) {
            return bin_t::NONE;
        }
        assert (cur_bin == root_bin_);
        if (cell.left_.bitmap_ == BITMAP_EMPTY) {
            return bin_t::ALL;
        }
        if (cell.left_.bitmap_ == BITMAP_FILLED) {
            return cur_bin;
        }
        return bin_t::NONE;
    }

    /* Process low-layers case */
    assert (bin != cur_bin);

    bitmap_t bm1;
    if (bin < cur_bin) {
        bm1 = cell.left_.bitmap_;
        cur_bin.to_left();
    } else {
        bm1 = cell.right_.bitmap_;
        cur_bin.to_right();
    }

    if (bm1 == BITMAP_EMPTY) {
        if (is_empty()) {
            return bin_t::ALL;
        }
        return cur_bin;
    }
    if (bm1 == BITMAP_FILLED) {
        if (is_filled()) {
            return bin_t::ALL;
        }
        return cur_bin;
    }

    /* Trace the bitmap */
    bin_t b = bin;
    bitmap_t bm2 = BITMAP[ BITMAP_LAYER_BITS & b.toUInt() ];

    if ((bm1 & bm2) == BITMAP_EMPTY) {
        do {
            cur_bin = b;
            b.to_parent();
            bm2 = BITMAP[ BITMAP_LAYER_BITS & b.toUInt() ];
        } while ((bm1 & bm2) == BITMAP_EMPTY);

        return cur_bin;

    } else if ((bm1 & bm2) == bm2) {
        do {
            cur_bin = b;
            b.to_parent();
            bm2 = BITMAP[ BITMAP_LAYER_BITS & b.toUInt() ];
        } while ((bm1 & bm2) == bm2);

        return cur_bin;
    }

    return bin_t::NONE;
}


/**
 * Find first empty bin
 */
bin_t binmap_t::find_empty() const
{
    /* Trace the bin */
    bitmap_t bitmap = BITMAP_FILLED;

    ref_t cur_ref;
    bin_t cur_bin;

    do {
        /* Processing the root */
        if (cell_[ROOT_REF].is_left_ref_) {
            cur_ref = cell_[ROOT_REF].left_.ref_;
            cur_bin = root_bin_.left();
        } else if (cell_[ROOT_REF].left_.bitmap_ != BITMAP_FILLED) {
            if (cell_[ ROOT_REF].left_.bitmap_ == BITMAP_EMPTY) {
                if (!cell_[ ROOT_REF].is_right_ref_ && cell_[ ROOT_REF ].right_.bitmap_ == BITMAP_EMPTY) {
                    return bin_t::ALL;
                }
                return root_bin_.left();
            }
            bitmap = cell_[ROOT_REF].left_.bitmap_;
            cur_bin = root_bin_.left();
            break;
        } else if (cell_[ROOT_REF].is_right_ref_) {
            cur_ref = cell_[ROOT_REF].right_.ref_;
            cur_bin = root_bin_.right();
        } else {
            if (cell_[ROOT_REF].right_.bitmap_ == BITMAP_FILLED) {
                if (root_bin_.is_all()) {
                    return bin_t::NONE;
                }
                return root_bin_.sibling();
            }
            bitmap = cell_[ROOT_REF].right_.bitmap_;
            cur_bin = root_bin_.right();
            break;
        }

        /* Processing middle layers */
        for ( ;;) {
            if (cell_[cur_ref].is_left_ref_) {
                cur_ref = cell_[cur_ref].left_.ref_;
                cur_bin.to_left();
            } else if (cell_[cur_ref].left_.bitmap_ != BITMAP_FILLED) {
                bitmap = cell_[cur_ref].left_.bitmap_;
                cur_bin.to_left();
                break;
            } else if (cell_[cur_ref].is_right_ref_) {
                cur_ref = cell_[cur_ref].right_.ref_;
                cur_bin.to_right();
            } else {
                assert (cell_[cur_ref].right_.bitmap_ != BITMAP_FILLED);
                bitmap = cell_[cur_ref].right_.bitmap_;
                cur_bin.to_right();
                break;
            }
        }

    } while (false);

    /* Getting result */
    assert (bitmap != BITMAP_FILLED);

    return bitmap_to_bin(cur_bin, ~bitmap);
}


/**
 * Find first filled bin
 */
bin_t binmap_t::find_filled() const
{
    /* Trace the bin */
    bitmap_t bitmap = BITMAP_EMPTY;

    ref_t cur_ref;
    bin_t cur_bin;

    do {
        /* Processing the root */
        if (cell_[ROOT_REF].is_left_ref_) {
            cur_ref = cell_[ROOT_REF].left_.ref_;
            cur_bin = root_bin_.left();
        } else if (cell_[ROOT_REF].left_.bitmap_ != BITMAP_EMPTY) {
            if (cell_[ ROOT_REF].left_.bitmap_ == BITMAP_FILLED) {
                if (!cell_[ ROOT_REF].is_right_ref_ && cell_[ ROOT_REF ].right_.bitmap_ == BITMAP_FILLED) {
                    return root_bin_;
                }
                return root_bin_.left();
            }
            bitmap = cell_[ROOT_REF].left_.bitmap_;
            cur_bin = root_bin_.left();
            break;
        } else if (cell_[ROOT_REF].is_right_ref_) {
            cur_ref = cell_[ROOT_REF].right_.ref_;
            cur_bin = root_bin_.right();
        } else {
            if (cell_[ROOT_REF].right_.bitmap_ == BITMAP_EMPTY) {
                return bin_t::NONE;
            }
            bitmap = cell_[ROOT_REF].right_.bitmap_;
            cur_bin = root_bin_.right();
            break;
        }

        /* Processing middle layers */
        for ( ;;) {
            if (cell_[cur_ref].is_left_ref_) {
                cur_ref = cell_[cur_ref].left_.ref_;
                cur_bin.to_left();
            } else if (cell_[cur_ref].left_.bitmap_ != BITMAP_EMPTY) {
                bitmap = cell_[cur_ref].left_.bitmap_;
                cur_bin.to_left();
                break;
            } else if (cell_[cur_ref].is_right_ref_) {
                cur_ref = cell_[cur_ref].right_.ref_;
                cur_bin.to_right();
            } else {
                assert (cell_[cur_ref].right_.bitmap_ != BITMAP_EMPTY);
                bitmap = cell_[cur_ref].right_.bitmap_;
                cur_bin.to_right();
                break;
            }
        }

    } while (false);

    /* Getting result */
    assert (bitmap != BITMAP_EMPTY);

    return bitmap_to_bin(cur_bin, bitmap);
}


/**
 * Arno: Find first empty bin right of start (start inclusive)
 */
bin_t binmap_t::find_empty(bin_t start) const
{
	bin_t cur_bin = start;

	if (is_empty(cur_bin))
		return cur_bin;
	do
	{
		// Move up till we find ancestor that is not filled.
		cur_bin = cur_bin.parent();
		if (!is_filled(cur_bin))
		{
			// Ancestor is not filled
			break;
		}
		if (cur_bin == root_bin_)
		{
			// Hit top, full tree, sort of. For some reason root_bin_ not
			// set to real top (but to ALL), so we may actually return a
			// bin that is outside the size of the content here.
			return bin_t::NONE;
		}
	}
	while (true);

	// Move down
	do
	{
		if (!is_filled(cur_bin.left()))
		{
			cur_bin.to_left();
		}
		else if (!is_filled(cur_bin.right()))
		{
			cur_bin.to_right();
		}
		if (cur_bin.is_base())
		{
			// Found empty bin
			return cur_bin;
		}
	} while(!cur_bin.is_base()); // safety catch

	return bin_t::NONE;
}



#define LR_LEFT   (0x00)
#define RL_RIGHT  (0x01)
#define RL_LEFT   (0x02)
#define LR_RIGHT  (0x03)


#define SSTACK()                                    \
    int _top_ = 0;                                  \
    bin_t _bin_[64];                                \
    ref_t _sref_[64];                               \
    char _dir_[64];

#define DSTACK()                                    \
    int _top_ = 0;                                  \
    bin_t _bin_[64];                                \
    ref_t _dref_[64];                               \
    char _dir_[64];

#define SDSTACK()                                   \
    int _top_ = 0;                                  \
    bin_t _bin_[64];                                \
    ref_t _sref_[64];                               \
    ref_t _dref_[64];                               \
    char _dir_[64];


#define SPUSH(b, sr, twist)                         \
    do {                                            \
        _bin_[_top_] = b;                           \
        _sref_[_top_] = sr;                         \
        _dir_[_top_] = (0 != (twist & (b.base_length() >> 1))); \
        ++_top_;                                    \
    } while (false)

#define DPUSH(b, dr, twist)                         \
    do {                                            \
        _bin_[_top_] = b;                           \
        _dref_[_top_] = dr;                         \
        _dir_[_top_] = (0 != (twist & (b.base_length() >> 1))); \
        ++_top_;                                    \
    } while (false)

#define SDPUSH(b, sr, dr, twist)                    \
    do {                                            \
        _bin_[_top_] = b;                           \
        _sref_[_top_] = sr;                         \
        _dref_[_top_] = dr;                         \
        _dir_[_top_] = (0 != (twist & (b.base_length() >> 1))); \
        ++_top_;                                    \
    } while (false)


#define SPOP()                                      \
    assert (_top_ < 65);                            \
    --_top_;                                        \
    const bin_t b = _bin_[_top_];                   \
    const cell_t& sc = source.cell_[_sref_[_top_]]; \
    const bool is_left = !(_dir_[_top_] & 0x01);    \
    if (0 == (_dir_[_top_] & 0x02)) {               \
        _dir_[_top_++] ^= 0x03;                     \
    }

#define DPOP()                                      \
    assert (_top_ < 65);                            \
    --_top_;                                        \
    const bin_t b = _bin_[_top_];                   \
    const cell_t& dc = destination.cell_[_dref_[_top_]]; \
    const bool is_left = !(_dir_[_top_] & 0x01);    \
    if (0 == (_dir_[_top_] & 0x02)) {               \
        _dir_[_top_++] ^= 0x03;                     \
    }

#define SDPOP()                                     \
    assert (_top_ < 65);                            \
    --_top_;                                        \
    const bin_t b = _bin_[_top_];                   \
    const cell_t& sc = source.cell_[_sref_[_top_]]; \
    const cell_t& dc = destination.cell_[_dref_[_top_]]; \
    const bool is_left = !(_dir_[_top_] & 0x01);    \
    if (0 == (_dir_[_top_] & 0x02)) {               \
        _dir_[_top_++] ^= 0x03;                     \
    }


/**
 * Find first additional bin in source
 *
 * @param destination
 *             the destination binmap
 * @param source
 *             the source binmap
 */
bin_t binmap_t::find_complement(const binmap_t& destination, const binmap_t& source, const bin_t::uint_t twist)
{
    return find_complement(destination, source, bin_t::ALL, twist);

    // Arno, 2012-01-09: Code unused?

    if (destination.is_empty()) {
        const cell_t& cell = source.cell_[ROOT_REF];
        if (!cell.is_left_ref_ && !cell.is_right_ref_ && cell.left_.bitmap_ == BITMAP_FILLED && cell.right_.bitmap_ == BITMAP_FILLED) {
            return source.root_bin_;
        }
        return _find_complement(source.root_bin_, BITMAP_EMPTY, ROOT_REF, source, twist);
    }

    if (destination.root_bin_.contains(source.root_bin_)) {
        ref_t dref;
        bin_t dbin;

        destination.trace(&dref, &dbin, source.root_bin_);

        if (dbin == source.root_bin_) {
            return binmap_t::_find_complement(dbin, dref, destination, ROOT_REF, source, twist);
        }

        assert (source.root_bin_ < dbin);

        if (destination.cell_[dref].left_.bitmap_ != BITMAP_FILLED) {
            if (destination.cell_[dref].left_.bitmap_ == BITMAP_EMPTY) {
                const cell_t& cell = source.cell_[ROOT_REF];
                if (!cell.is_left_ref_ && !cell.is_right_ref_ && cell.left_.bitmap_ == BITMAP_FILLED && cell.right_.bitmap_ == BITMAP_FILLED) {
                    return source.root_bin_;
                }
            }
            return binmap_t::_find_complement(source.root_bin_, destination.cell_[dref].left_.bitmap_, ROOT_REF, source, twist);
        }

        return bin_t::NONE;

    } else {
        SSTACK();

        /* Initialization */
        SPUSH(source.root_bin_, ROOT_REF, twist);

        /* Main loop */
        do {
            SPOP();

            if (is_left) {
                if (b.left() == destination.root_bin_) {
                    if (sc.is_left_ref_) {
                        const bin_t res = binmap_t::_find_complement(destination.root_bin_, ROOT_REF, destination, sc.left_.ref_, source, twist);
                        if (!res.is_none()) {
                            return res;
                        }
                    } else if (sc.left_.bitmap_ != BITMAP_EMPTY) {
                        const bin_t res = binmap_t::_find_complement(destination.root_bin_, ROOT_REF, destination, sc.left_.bitmap_, twist);
                        if (!res.is_none()) {
                            return res;
                        }
                    }
                    continue;
                }

                if (sc.is_left_ref_) {
                    SPUSH(b.left(), sc.left_.ref_, twist);
                    continue;

                } else if (sc.left_.bitmap_ != BITMAP_EMPTY) {
                    if (0 == (twist & (b.left().base_length() - 1) & ~(destination.root_bin_.base_length() - 1))) {
                        const bin_t res = binmap_t::_find_complement(destination.root_bin_, ROOT_REF, destination, sc.left_.bitmap_, twist);
                        if (!res.is_none()) {
                            return res;
                        }
                        return binmap_t::_find_complement(destination.root_bin_.sibling(), BITMAP_EMPTY, sc.left_.bitmap_, twist);

                    } else if (sc.left_.bitmap_ != BITMAP_FILLED) {
                        return binmap_t::_find_complement(b.left(), BITMAP_EMPTY, sc.left_.bitmap_, twist);

                    } else {
                        bin_t::uint_t s = twist & (b.left().base_length() - 1);
                        /* Sorry for the following hardcode hack: Flow the highest bit of s */
                        s |= s >> 1; s |= s >> 2;
                        s |= s >> 4; s |= s >> 8;
                        s |= s >> 16;
                        s |= (s >> 16) >> 16;   // FIXME: hide warning
                        return bin_t(s + 1 + (s >> 1)); /* bin_t(s >> 1).sibling(); */
                    }
                }

            } else {
                if (sc.is_right_ref_) {
                    return binmap_t::_find_complement(b.right(), BITMAP_EMPTY, sc.right_.ref_, source, twist);
                } else if (sc.right_.bitmap_ != BITMAP_EMPTY) {
                    return binmap_t::_find_complement(b.right(), BITMAP_EMPTY, sc.right_.bitmap_, twist);
                }
                continue;
            }
        } while (_top_ > 0);

        return bin_t::NONE;
    }
}


bin_t binmap_t::find_complement(const binmap_t& destination, const binmap_t& source, bin_t range, const bin_t::uint_t twist)
{
    ref_t sref = ROOT_REF;
    bitmap_t sbitmap = BITMAP_EMPTY;
    bool is_sref = true;

    if (range.contains(source.root_bin_)) {
        range = source.root_bin_;
        is_sref = true;
        sref = ROOT_REF;

    } else if (source.root_bin_.contains(range)) {
        bin_t sbin;
        source.trace(&sref, &sbin, range);

        if (range == sbin) {
            is_sref = true;
        } else {
            is_sref = false;

            if (range < sbin) {
                sbitmap = source.cell_[sref].left_.bitmap_;
            } else {
                sbitmap = source.cell_[sref].right_.bitmap_;
            }

            sbitmap &= BITMAP[ BITMAP_LAYER_BITS & range.toUInt() ];

            if (sbitmap == BITMAP_EMPTY) {
                return bin_t::NONE;
            }
        }

    } else {
        return bin_t::NONE;
    }

    assert (is_sref || sbitmap != BITMAP_EMPTY);

    if (destination.is_empty()) {
        if (is_sref) {
            const cell_t& cell = source.cell_[sref];
            if (!cell.is_left_ref_ && !cell.is_right_ref_ && cell.left_.bitmap_ == BITMAP_FILLED && cell.right_.bitmap_ == BITMAP_FILLED) {
                return range;
            } else {
                return _find_complement(range, BITMAP_EMPTY, sref, source, twist);
            }
        } else {
            return _find_complement(range, BITMAP_EMPTY, sbitmap, twist);
        }
    }

    if (destination.root_bin_.contains(range)) {
        ref_t dref;
        bin_t dbin;
        destination.trace(&dref, &dbin, range);

        if (range == dbin) {
            if (is_sref) {
                return _find_complement(range, dref, destination, sref, source, twist);
            } else {
                return _find_complement(range, dref, destination, sbitmap, twist);
            }

        } else {
            bitmap_t dbitmap;

            if (range < dbin) {
                dbitmap = destination.cell_[dref].left_.bitmap_;
            } else {
                dbitmap = destination.cell_[dref].right_.bitmap_;
            }

            if (dbitmap == BITMAP_FILLED) {
                return bin_t::NONE;

            } else if (is_sref) {
                if (dbitmap == BITMAP_EMPTY) {
                    const cell_t& cell = source.cell_[sref];
                    if (!cell.is_left_ref_ && !cell.is_right_ref_ && cell.left_.bitmap_ == BITMAP_FILLED && cell.right_.bitmap_ == BITMAP_FILLED) {
                        return range;
                    }
                }

                return _find_complement(range, dbitmap, sref, source, twist);

            } else {
                if ((sbitmap & ~dbitmap) != BITMAP_EMPTY) {
                    return _find_complement(range, dbitmap, sbitmap, twist);
                } else {
                    return bin_t::NONE;
                }
            }
        }

    } else if (!range.contains(destination.root_bin_)) {
        if (is_sref) {
            return _find_complement(range, BITMAP_EMPTY, sref, source, twist);
        } else {
            return _find_complement(range, BITMAP_EMPTY, sbitmap, twist);
        }

    } else { // range.contains(destination.m_root_bin)
        if (is_sref) {
            SSTACK();

            SPUSH(range, sref, twist);

            do {
                SPOP();

                if (is_left) {
                    if (b.left() == destination.root_bin_) {
                        if (sc.is_left_ref_) {
                            const bin_t res = binmap_t::_find_complement(destination.root_bin_, ROOT_REF, destination, sc.left_.ref_, source, twist);
                            if (!res.is_none()) {
                                return res;
                            }
                        } else if (sc.left_.bitmap_ != BITMAP_EMPTY) {
                            const bin_t res = binmap_t::_find_complement(destination.root_bin_, ROOT_REF, destination, sc.left_.bitmap_, twist);
                            if (!res.is_none()) {
                                return res;
                            }
                        }
                        continue;
                    }

                    if (sc.is_left_ref_) {
                        SPUSH(b.left(), sc.left_.ref_, twist);
                        continue;

                    } else if (sc.left_.bitmap_ != BITMAP_EMPTY) {
                        if (0 == (twist & (b.left().base_length() - 1) & ~(destination.root_bin_.base_length() - 1))) {
                            const bin_t res = binmap_t::_find_complement(destination.root_bin_, ROOT_REF, destination, sc.left_.bitmap_, twist);
                            if (!res.is_none()) {
                                return res;
                            }
                            return binmap_t::_find_complement(destination.root_bin_.sibling(), BITMAP_EMPTY, sc.left_.bitmap_, twist);

                        } else if (sc.left_.bitmap_ != BITMAP_FILLED) {
                            return binmap_t::_find_complement(b.left(), BITMAP_EMPTY, sc.left_.bitmap_, twist);

                        } else {
                            bin_t::uint_t s = twist & (b.left().base_length() - 1);
                            /* Sorry for the following hardcode hack: Flow the highest bit of s */
                            s |= s >> 1; s |= s >> 2;
                            s |= s >> 4; s |= s >> 8;
                            s |= s >> 16;
                            s |= (s >> 16) >> 16;   // FIXME: hide warning
                            return bin_t(s + 1 + (s >> 1)); /* bin_t(s >> 1).sibling(); */
                        }
                    }

                } else {
                    if (sc.is_right_ref_) {
                        return binmap_t::_find_complement(b.right(), BITMAP_EMPTY, sc.right_.ref_, source, twist);
                    } else if (sc.right_.bitmap_ != BITMAP_EMPTY) {
                        return binmap_t::_find_complement(b.right(), BITMAP_EMPTY, sc.right_.bitmap_, twist);
                    }
                    continue;
                }
            } while (_top_ > 0);

            return bin_t::NONE;

        } else {
            if (0 == (twist & (range.base_length() - 1) & ~(destination.root_bin_.base_length() - 1))) {
                const bin_t res = binmap_t::_find_complement(destination.root_bin_, ROOT_REF, destination, sbitmap, twist);
                if (!res.is_none()) {
                    return res;
                }
                return binmap_t::_find_complement(destination.root_bin_.sibling(), BITMAP_EMPTY, sbitmap, twist);

            } else if (sbitmap != BITMAP_FILLED) {
                return binmap_t::_find_complement(range, BITMAP_EMPTY, sbitmap, twist);

            } else {
                bin_t::uint_t s = twist & (range.base_length() - 1);
                /* Sorry for the following hardcode hack: Flow the highest bit of s */
                s |= s >> 1; s |= s >> 2;
                s |= s >> 4; s |= s >> 8;
                s |= s >> 16;
                s |= (s >> 16) >> 16;   // FIXME: hide warning
                return bin_t(s + 1 + (s >> 1)); /* bin_t(s >> 1).sibling(); */
            }
        }
    }
}


bin_t binmap_t::_find_complement(const bin_t& bin, const ref_t dref, const binmap_t& destination, const ref_t sref, const binmap_t& source, const bin_t::uint_t twist)
{
    /* Initialization */
    SDSTACK();
    SDPUSH(bin, sref, dref, twist);

    /* Main loop */
    do {
        SDPOP();

        if (is_left) {
            if (sc.is_left_ref_) {
                if (dc.is_left_ref_) {
                    SDPUSH(b.left(), sc.left_.ref_, dc.left_.ref_, twist);
                    continue;

                } else if (dc.left_.bitmap_ != BITMAP_FILLED) {
                    const bin_t res = binmap_t::_find_complement(b.left(), dc.left_.bitmap_, sc.left_.ref_, source, twist);
                    if (!res.is_none()) {
                        return res;
                    }
                    continue;
                }

            } else if (sc.left_.bitmap_ != BITMAP_EMPTY) {
                if (dc.is_left_ref_) {
                    const bin_t res = binmap_t::_find_complement(b.left(), dc.left_.ref_, destination, sc.left_.bitmap_, twist);
                    if (!res.is_none()) {
                        return res;
                    }
                    continue;

                } else if ((sc.left_.bitmap_ & ~dc.left_.bitmap_) != BITMAP_EMPTY) {
                    return binmap_t::_find_complement(b.left(), dc.left_.bitmap_, sc.left_.bitmap_, twist);
                }
            }

        } else {
            if (sc.is_right_ref_) {
                if (dc.is_right_ref_) {
                    SDPUSH(b.right(), sc.right_.ref_, dc.right_.ref_, twist);
                    continue;

                } else if (dc.right_.bitmap_ != BITMAP_FILLED) {
                    const bin_t res = binmap_t::_find_complement(b.right(), dc.right_.bitmap_, sc.right_.ref_, source, twist);
                    if (!res.is_none()) {
                        return res;
                    }
                    continue;
                }

            } else if (sc.right_.bitmap_ != BITMAP_EMPTY) {
                if (dc.is_right_ref_) {
                    const bin_t res = binmap_t::_find_complement(b.right(), dc.right_.ref_, destination, sc.right_.bitmap_, twist);
                    if (!res.is_none()) {
                        return res;
                    }
                    continue;

                } else if ((sc.right_.bitmap_ & ~dc.right_.bitmap_) != BITMAP_EMPTY) {
                    return binmap_t::_find_complement(b.right(), dc.right_.bitmap_, sc.right_.bitmap_, twist);
                }
            }
        }
    } while (_top_ > 0);

    return bin_t::NONE;
}


bin_t binmap_t::_find_complement(const bin_t& bin, const bitmap_t dbitmap, const ref_t sref, const binmap_t& source, const bin_t::uint_t twist)
{
    assert (dbitmap != BITMAP_EMPTY || sref != ROOT_REF ||
            source.cell_[ROOT_REF].is_left_ref_ ||
            source.cell_[ROOT_REF].is_right_ref_ ||
            source.cell_[ROOT_REF].left_.bitmap_ != BITMAP_FILLED ||
            source.cell_[ROOT_REF].right_.bitmap_ != BITMAP_FILLED);

    /* Initialization */
    SSTACK();
    SPUSH(bin, sref, twist);

    /* Main loop */
    do {
        SPOP();

        if (is_left) {
            if (sc.is_left_ref_) {
                SPUSH(b.left(), sc.left_.ref_, twist);
                continue;
            } else if ((sc.left_.bitmap_ & ~dbitmap) != BITMAP_EMPTY) {
                return binmap_t::_find_complement(b.left(), dbitmap, sc.left_.bitmap_, twist);
            }

        } else {
            if (sc.is_right_ref_) {
                SPUSH(b.right(), sc.right_.ref_, twist);
                continue;
            } else if ((sc.right_.bitmap_ & ~dbitmap) != BITMAP_EMPTY) {
                return binmap_t::_find_complement(b.right(), dbitmap, sc.right_.bitmap_, twist);
            }
        }
    } while (_top_ > 0);

    return bin_t::NONE;
}


bin_t binmap_t::_find_complement(const bin_t& bin, const ref_t dref, const binmap_t& destination, const bitmap_t sbitmap, const bin_t::uint_t twist)
{
    /* Initialization */
    DSTACK();
    DPUSH(bin, dref, twist);

    /* Main loop */
    do {
        DPOP();

        if (is_left) {
            if (dc.is_left_ref_) {
                DPUSH(b.left(), dc.left_.ref_, twist);
                continue;

            } else if ((sbitmap & ~dc.left_.bitmap_) != BITMAP_EMPTY) {
                return binmap_t::_find_complement(b.left(), dc.left_.bitmap_, sbitmap, twist);
            }

        } else {
            if (dc.is_right_ref_) {
                DPUSH(b.right(), dc.right_.ref_, twist);
                continue;

            } else if ((sbitmap & ~dc.right_.bitmap_) != BITMAP_EMPTY) {
                return binmap_t::_find_complement(b.right(), dc.right_.bitmap_, sbitmap, twist);
            }
        }
    } while (_top_ > 0);

    return bin_t::NONE;
}


bin_t binmap_t::_find_complement(const bin_t& bin, const bitmap_t dbitmap, const bitmap_t sbitmap, bin_t::uint_t twist)
{
    bitmap_t bitmap = sbitmap & ~dbitmap;

    assert (bitmap != BITMAP_EMPTY);

    if (bitmap == BITMAP_FILLED) {
        return bin;
    }

    twist &= bin.base_length() - 1;

    if (sizeof(bitmap_t) == 2) {
        if (twist & 1) {
            bitmap = ((bitmap & 0x5555) << 1)  | ((bitmap & 0xAAAA) >> 1);
        }
        if (twist & 2) {
            bitmap = ((bitmap & 0x3333) << 2)  | ((bitmap & 0xCCCC) >> 2);
        }
        if (twist & 4) {
            bitmap = ((bitmap & 0x0f0f) << 4)  | ((bitmap & 0xf0f0) >> 4);
        }
        if (twist & 8) {
            bitmap = ((bitmap & 0x00ff) << 8)  | ((bitmap & 0xff00) >> 8);
        }
        return bin_t(bin.base_left().twisted(twist & ~0x0f).toUInt() + bitmap_to_bin(bitmap)).to_twisted(twist & 0x0f);

    } else {
        if (twist & 1) {
            bitmap = ((bitmap & 0x55555555) << 1)  | ((bitmap & 0xAAAAAAAA) >> 1);
        }
        if (twist & 2) {
            bitmap = ((bitmap & 0x33333333) << 2)  | ((bitmap & 0xCCCCCCCC) >> 2);
        }
        if (twist & 4) {
            bitmap = ((bitmap & 0x0f0f0f0f) << 4)  | ((bitmap & 0xf0f0f0f0) >> 4);
        }
        if (twist & 8) {
            bitmap = ((bitmap & 0x00ff00ff) << 8)  | ((bitmap & 0xff00ff00) >> 8);
        }
        if (twist & 16) {
            bitmap = ((bitmap & 0x0000ffff) << 16)  | ((bitmap & 0xffff0000) >> 16);
        }
        return bin_t(bin.base_left().twisted(twist & ~0x1f).toUInt() + bitmap_to_bin(bitmap)).to_twisted(twist & 0x1f);
    }
}


/**
 * Sets bins
 *
 * @param bin
 *             the bin
 */
void binmap_t::set(const bin_t& bin)
{
    if (bin.is_none()) {
        return;
    }

    if (bin.layer_bits() > BITMAP_LAYER_BITS) {
        _set__high_layer_bitmap(bin, BITMAP_FILLED);
    } else {
        _set__low_layer_bitmap(bin, BITMAP_FILLED);
    }
}


/**
 * Resets bins
 *
 * @param bin
 *             the bin
 */
void binmap_t::reset(const bin_t& bin)
{
    if (bin.is_none()) {
        return;
    }

    if (bin.layer_bits() > BITMAP_LAYER_BITS) {
        _set__high_layer_bitmap(bin, BITMAP_EMPTY);
    } else {
        _set__low_layer_bitmap(bin, BITMAP_EMPTY);
    }
}


/**
 * Empty all bins
 */
void binmap_t::clear()
{
    cell_t& cell = cell_[ROOT_REF];

    if (cell.is_left_ref_) {
        free_cell(cell.left_.ref_);
    }
    if (cell.is_right_ref_) {
        free_cell(cell.right_.ref_);
    }

    cell.is_left_ref_ = false;
    cell.is_right_ref_ = false;
    cell.left_.bitmap_ = BITMAP_EMPTY;
    cell.right_.bitmap_ = BITMAP_EMPTY;
}


/**
 * Fill the binmap. Creates a new filled binmap. Size is given by the source root
 */
void binmap_t::fill(const binmap_t& source)
{
    root_bin_ = source.root_bin_;
    /* Extends root if needed */
    while (!root_bin_.contains(source.root_bin_)) {
        if (!extend_root()) {
            return /* ALLOC ERROR */;
        }
    }
    set(source.root_bin_);

    cell_t& cell = cell_[ROOT_REF];

    cell.is_left_ref_ = false;
    cell.is_right_ref_ = false;
    cell.left_.bitmap_ = BITMAP_FILLED;
    cell.right_.bitmap_ = BITMAP_FILLED;
}



/**
 * Get number of allocated cells
 */
size_t binmap_t::cells_number() const
{
    return allocated_cells_number_;
}


/**
 * Get total size of the binmap
 */
size_t binmap_t::total_size() const
{
    return sizeof(*this) + sizeof(cell_[0]) * cells_number_;
}



/**
 * Echo the binmap status to stdout
 */
void binmap_t::status() const
{
    printf("bitmap:\n");
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 64; ++j) {
            printf("%d", is_filled(bin_t(i * 64 + j)));
        }
        printf("\n");
    }

    printf("size: %u bytes\n", static_cast<unsigned int>(total_size()));
    printf("cells number: %u (of %u)\n", static_cast<unsigned int>(allocated_cells_number_), static_cast<unsigned int>(cells_number_));
    printf("root bin: %llu\n", static_cast<unsigned long long>(root_bin_.toUInt()));
}


/** Trace the bin */
inline void binmap_t::trace(ref_t* ref, bin_t* bin, const bin_t& target) const
{
    assert (root_bin_.contains(target));

    ref_t cur_ref = ROOT_REF;
    bin_t cur_bin = root_bin_;

    while (target != cur_bin) {
        if (target < cur_bin) {
            if (cell_[cur_ref].is_left_ref_) {
                cur_ref = cell_[cur_ref].left_.ref_;
                cur_bin.to_left();
            } else {
                break;
            }
        } else {
            if (cell_[cur_ref].is_right_ref_) {
                cur_ref = cell_[cur_ref].right_.ref_;
                cur_bin.to_right();
            } else {
                break;
            }
        }
    }

    assert (cur_bin.layer_bits() > BITMAP_LAYER_BITS);

    if (ref) {
        *ref = cur_ref;
    }
    if (bin) {
        *bin = cur_bin;
    }
}


/** Trace the bin */
inline void binmap_t::trace(ref_t* ref, bin_t* bin, ref_t** history, const bin_t& target) const
{
    assert (history);
    assert (root_bin_.contains(target));

    ref_t* href = *history;
    ref_t cur_ref = ROOT_REF;
    bin_t cur_bin = root_bin_;

    *href++ = ROOT_REF;
    while (target != cur_bin) {
        if (target < cur_bin) {
            if (cell_[cur_ref].is_left_ref_) {
                cur_ref = cell_[cur_ref].left_.ref_;
                cur_bin.to_left();
            } else {
                break;
            }
        } else {
            if (cell_[cur_ref].is_right_ref_) {
                cur_ref = cell_[cur_ref].right_.ref_;
                cur_bin.to_right();
            } else {
                break;
            }
        }

        *href++ = cur_ref;
    }

    assert (cur_bin.layer_bits() > BITMAP_LAYER_BITS);

    if (ref) {
        *ref = cur_ref;
    }
    if (bin) {
        *bin = cur_bin;
    }

    *history = href;
}


/**
 * Copy a binmap to another
 */
void binmap_t::copy(binmap_t& destination, const binmap_t& source)
{
    destination.root_bin_ = source.root_bin_;
    binmap_t::copy(destination, ROOT_REF, source, ROOT_REF);
}


/**
 * Copy a range from one binmap to another binmap
 */
void binmap_t::copy(binmap_t& destination, const binmap_t& source, const bin_t& range)
{
    ref_t int_ref;
    bin_t int_bin;

    if (range.contains(destination.root_bin_)) {
        if (source.root_bin_.contains(range)) {
            source.trace(&int_ref, &int_bin, range);
            destination.root_bin_ = range;
            binmap_t::copy(destination, ROOT_REF, source, int_ref);
        } else if (range.contains(source.root_bin_)) {
            destination.root_bin_ = source.root_bin_;
            binmap_t::copy(destination, ROOT_REF, source, ROOT_REF);
        } else {
            destination.reset(range);
        }

    } else {
        if (source.root_bin_.contains(range)) {
            source.trace(&int_ref, &int_bin, range);

            const cell_t& cell = source.cell_[int_ref];

            if (range.layer_bits() <= BITMAP_LAYER_BITS) {
                if (range < int_bin) {
                    destination._set__low_layer_bitmap(range, cell.left_.bitmap_);
                } else {
                    destination._set__low_layer_bitmap(range, cell.right_.bitmap_);
                }

            } else {
                if (range == int_bin) {
                    if (cell.is_left_ref_ || cell.is_right_ref_ || cell.left_.bitmap_ != cell.right_.bitmap_) {
                        binmap_t::_copy__range(destination, source, int_ref, range);
                    } else {
                        destination._set__high_layer_bitmap(range, cell.left_.bitmap_);
                    }
                } else if (range < int_bin) {
                    destination._set__high_layer_bitmap(range, cell.left_.bitmap_);
                } else {
                    destination._set__high_layer_bitmap(range, cell.right_.bitmap_);
                }
            }

        } else if (range.contains(source.root_bin_)) {
            destination.reset(range);   // Probably it could be optimized

            const cell_t& cell = source.cell_[ ROOT_REF ];

            if (cell.is_left_ref_ || cell.is_right_ref_ || cell.left_.bitmap_ != cell.right_.bitmap_) {
                binmap_t::_copy__range(destination, source, ROOT_REF, source.root_bin_);
            } else {
                destination._set__high_layer_bitmap(source.root_bin_, cell.left_.bitmap_);
            }

        } else {
            destination.reset(range);
        }
    }
}


inline void binmap_t::_set__low_layer_bitmap(const bin_t& bin, const bitmap_t _bitmap)
{
    assert (bin.layer_bits() <= BITMAP_LAYER_BITS);

    const bitmap_t bin_bitmap = BITMAP[ bin.toUInt() & BITMAP_LAYER_BITS ];
    const bitmap_t bitmap = _bitmap & bin_bitmap;

    /* Extends root if needed */
    if (!root_bin_.contains(bin)) {
        /* Trivial case */
        if (bitmap == BITMAP_EMPTY) {
            return;
        }
        do {
            if (!extend_root()) {
                return /* ALLOC ERROR */;
            }
        } while (!root_bin_.contains(bin));
    }

    /* Get the pre-range */
    const bin_t pre_bin( (bin.toUInt() & (~(BITMAP_LAYER_BITS + 1))) | BITMAP_LAYER_BITS );

    /* The trace the bin with history */
    ref_t _href[64];
    ref_t* href = _href;
    ref_t cur_ref;
    bin_t cur_bin;

    /* Process first stage -- do not touch existed tree */
    trace(&cur_ref, &cur_bin, &href, pre_bin);

    assert (cur_bin.layer_bits() > BITMAP_LAYER_BITS);

    /* Checking that we need to do anything */
    bitmap_t bm = BITMAP_EMPTY;
    {
        cell_t& cell = cell_[cur_ref];

        if (bin < cur_bin) {
            assert (!cell.is_left_ref_);
            bm = cell.left_.bitmap_;
            if ((bm & bin_bitmap) == bitmap) {
                return;
            }
            if (cur_bin == pre_bin) {
                cell.left_.bitmap_ = (cell.left_.bitmap_ & ~bin_bitmap) | bitmap;
                pack_cells(href - 1);
                return;
            }
        } else {
            assert (!cell.is_right_ref_);
            bm = cell.right_.bitmap_;
            if ((bm & bin_bitmap) == bitmap) {
                return;
            }
            if (cur_bin == pre_bin) {
                cell.right_.bitmap_ = (cell.right_.bitmap_ & ~bin_bitmap) | bitmap;
                pack_cells(href - 1);
                return;
            }
        }
    }

    /* Reserving proper number of cells */
    if (!reserve_cells( cur_bin.layer() - pre_bin.layer() )) {
        return /* MEMORY ERROR or OVERFLOW ERROR */;
    }

    /* Continue to trace */
    do {
        const ref_t ref = _alloc_cell();

        cell_[ref].is_left_ref_ = false;
        cell_[ref].is_right_ref_ = false;
        cell_[ref].left_.bitmap_ = bm;
        cell_[ref].right_.bitmap_ = bm;

        if (pre_bin < cur_bin) {
            cell_[cur_ref].is_left_ref_ = true;
            cell_[cur_ref].left_.ref_ = ref;
            cur_bin.to_left();
        } else {
            cell_[cur_ref].is_right_ref_ = true;
            cell_[cur_ref].right_.ref_ = ref;
            cur_bin.to_right();
        }

        cur_ref = ref;
    } while (cur_bin != pre_bin);

    assert (cur_bin == pre_bin);
    assert (cur_bin.layer_bits() > BITMAP_LAYER_BITS);

    /* Complete setting */
    if (bin < cur_bin) {
        cell_[cur_ref].left_.bitmap_ = (cell_[cur_ref].left_.bitmap_ & ~bin_bitmap) | bitmap;
    } else {
        cell_[cur_ref].right_.bitmap_ = (cell_[cur_ref].right_.bitmap_ & ~bin_bitmap) | bitmap;
    }
}


inline void binmap_t::_set__high_layer_bitmap(const bin_t& bin, const bitmap_t bitmap)
{
    assert (bin.layer_bits() > BITMAP_LAYER_BITS);

    /* First trivial case */
    if (bin.contains(root_bin_)) {
        cell_t& cell = cell_[ROOT_REF];
        if (cell.is_left_ref_) {
            free_cell(cell.left_.ref_);
        }
        if (cell.is_right_ref_) {
            free_cell(cell.right_.ref_);
        }

        root_bin_ = bin;
        cell.is_left_ref_ = false;
        cell.is_right_ref_ = false;
        cell.left_.bitmap_ = bitmap;
        cell.right_.bitmap_ = bitmap;

        return;
    }

    /* Get the pre-range */
    bin_t pre_bin = bin.parent();

    /* Extends root if needed */
    if (!root_bin_.contains(pre_bin)) {
        /* Second trivial case */
        if (bitmap == BITMAP_EMPTY) {
            return;
        }

        do {
            if (!extend_root()) {
                return /* ALLOC ERROR */;
            }
        } while (!root_bin_.contains(pre_bin));
    }

    /* The trace the bin with history */
    ref_t _href[64];
    ref_t* href = _href;
    ref_t cur_ref;
    bin_t cur_bin;

    /* Process first stage -- do not touch existed tree */
    trace(&cur_ref, &cur_bin, &href, pre_bin);

    /* Checking that we need to do anything */
    bitmap_t bm = BITMAP_EMPTY;
    {
        cell_t& cell = cell_[cur_ref];
        if (bin < cur_bin) {
            if (cell.is_left_ref_) {
                /* assert (cur_bin == pre_bin); */
                cell.is_left_ref_ = false;
                free_cell(cell.left_.ref_);
            } else {
                bm = cell.left_.bitmap_;
                if (bm == bitmap) {
                    return;
                }
            }
            if (cur_bin == pre_bin) {
                cell.left_.bitmap_ = bitmap;
                pack_cells(href - 1);
                return;
            }
        } else {
            if (cell.is_right_ref_) {
                /* assert (cur_bin == pre_bin); */
                cell.is_right_ref_ = false;
                free_cell(cell.right_.ref_);
            } else {
                bm = cell.right_.bitmap_;
                if (bm == bitmap) {
                    return;
                }
            }
            if (cur_bin == pre_bin) {
                cell.right_.bitmap_ = bitmap;
                pack_cells(href - 1);
                return;
            }
        }
    }

    /* Reserving proper number of cells */
    if (!reserve_cells( cur_bin.layer() - pre_bin.layer() )) {
        return /* MEMORY ERROR or OVERFLOW ERROR */;
    }

    /* Continue to trace */
    do {
        const ref_t ref = _alloc_cell();

        cell_[ref].is_left_ref_ = false;
        cell_[ref].is_right_ref_ = false;
        cell_[ref].left_.bitmap_ = bm;
        cell_[ref].right_.bitmap_ = bm;

        if (pre_bin < cur_bin) {
            cell_[cur_ref].is_left_ref_ = true;
            cell_[cur_ref].left_.ref_ = ref;
            cur_bin.to_left();
        } else {
            cell_[cur_ref].is_right_ref_ = true;
            cell_[cur_ref].right_.ref_ = ref;
            cur_bin.to_right();
        }

        cur_ref = ref;
    } while (cur_bin != pre_bin);

    assert (cur_bin == pre_bin);
    assert (cur_bin.layer_bits() > BITMAP_LAYER_BITS);

    /* Complete setting */
    if (bin < cur_bin) {
        cell_[cur_ref].left_.bitmap_ = bitmap;
    } else {
        cell_[cur_ref].right_.bitmap_ = bitmap;
    }
}


void binmap_t::_copy__range(binmap_t& destination, const binmap_t& source, const ref_t sref, const bin_t sbin)
{
    assert (sbin.layer_bits() > BITMAP_LAYER_BITS);

    assert (sref == ROOT_REF ||
            source.cell_[ sref ].is_left_ref_ || source.cell_[ sref ].is_right_ref_ ||
            source.cell_[ sref ].left_.bitmap_ != source.cell_[ sref ].right_.bitmap_
    );

    /* Extends root if needed */
    while (!destination.root_bin_.contains(sbin)) {
        if (!destination.extend_root()) {
            return /* ALLOC ERROR */;
        }
    }

    /* The trace the bin */
    ref_t cur_ref;
    bin_t cur_bin;

    /* Process first stage -- do not touch existed tree */
    destination.trace(&cur_ref, &cur_bin, sbin);

    /* Continue unpacking if needed */
    if (cur_bin != sbin) {
        bitmap_t bm = BITMAP_EMPTY;

        if (sbin < cur_bin) {
            bm = destination.cell_[cur_ref].left_.bitmap_;
        } else {
            bm = destination.cell_[cur_ref].right_.bitmap_;
        }

        /* Reserving proper number of cells */
        if (!destination.reserve_cells( cur_bin.layer() - sbin.layer() )) {
            return /* MEMORY ERROR or OVERFLOW ERROR */;
        }

        /* Continue to trace */
        do {
            const ref_t ref = destination._alloc_cell();

            destination.cell_[ref].is_left_ref_ = false;
            destination.cell_[ref].is_right_ref_ = false;
            destination.cell_[ref].left_.bitmap_ = bm;
            destination.cell_[ref].right_.bitmap_ = bm;

            if (sbin < cur_bin) {
                destination.cell_[cur_ref].is_left_ref_ = true;
                destination.cell_[cur_ref].left_.ref_ = ref;
                cur_bin.to_left();
            } else {
                destination.cell_[cur_ref].is_right_ref_ = true;
                destination.cell_[cur_ref].right_.ref_ = ref;
                cur_bin.to_right();
            }

            cur_ref = ref;
        } while (cur_bin != sbin);
    }

    /* Make copying */
    copy(destination, cur_ref, source, sref);
}


/**
 * Clone binmap cells to another binmap
 */
void binmap_t::copy(binmap_t& destination, const ref_t dref, const binmap_t& source, const ref_t sref)
{
    assert (dref == ROOT_REF ||
            source.cell_[ sref ].is_left_ref_ || source.cell_[ sref ].is_right_ref_ ||
            source.cell_[ sref ].left_.bitmap_ != source.cell_[ sref ].right_.bitmap_
    );

    size_t sref_size = 0;
    size_t dref_size = 0;

    ref_t sstack[128];
    ref_t dstack[128];
    size_t top = 0;

    /* Get size of the source subtree */
    sstack[top++] = sref;
    do {
        assert (top < sizeof(sstack) / sizeof(sstack[0]));

        ++sref_size;

        const cell_t& scell = source.cell_[ sstack[--top] ];
        if (scell.is_left_ref_) {
            sstack[top++] = scell.left_.ref_;
        }
        if (scell.is_right_ref_) {
            sstack[top++] = scell.right_.ref_;
        }

    } while (top > 0);

    /* Get size of the destination subtree */
    dstack[top++] = dref;
    do {
        assert (top < sizeof(dstack) / sizeof(dstack[0]));

        ++dref_size;

        const cell_t& dcell = destination.cell_[ dstack[--top] ];
        if (dcell.is_left_ref_) {
            dstack[top++] = dcell.left_.ref_;
        }
        if (dcell.is_right_ref_) {
            dstack[top++] = dcell.right_.ref_;
        }

    } while (top > 0);

    /* Reserving proper number of cells */
    if (dref_size < sref_size) {
        if (!destination.reserve_cells( sref_size - dref_size)) {
            return /* MEMORY ERROR or OVERFLOW ERROR */;
        }
    }

    /* Release the destination subtree */
    if (destination.cell_[dref].is_left_ref_) {
        destination.free_cell(destination.cell_[dref].left_.ref_);
    }
    if (destination.cell_[dref].is_right_ref_) {
        destination.free_cell(destination.cell_[dref].right_.ref_);
    }

    /* Make cloning */
    sstack[top] = sref;
    dstack[top] = dref;
    ++top;

    do {
        --top;
        const cell_t& scell = source.cell_[ sstack[top] ];
        cell_t& dcell = destination.cell_[ dstack[top] ];

        /* Processing left ref */
        if (scell.is_left_ref_) {
            dcell.is_left_ref_ = true;
            dcell.left_.ref_ = destination._alloc_cell();

            sstack[top] = scell.left_.ref_;
            dstack[top] = dcell.left_.ref_;
            ++top;
        } else {
            dcell.is_left_ref_ = false;
            dcell.left_.bitmap_ = scell.left_.bitmap_;
        }

        /* Processing right ref */
        if (scell.is_right_ref_) {
            dcell.is_right_ref_ = true;
            dcell.right_.ref_ = destination._alloc_cell();

            sstack[top] = scell.right_.ref_;
            dstack[top] = dcell.right_.ref_;
            ++top;
        } else {
            dcell.is_right_ref_ = false;
            dcell.right_.bitmap_ = scell.right_.bitmap_;
        }
    } while (top > 0);
}

int binmap_t::write_cell(FILE *fp,cell_t c)
{
	fprintf_retiffail(fp,"leftb %d\n", c.left_.bitmap_);
	fprintf_retiffail(fp,"rightb %d\n", c.right_.bitmap_);
	fprintf_retiffail(fp,"is_left %d\n", c.is_left_ref_ ? 1 : 0 );
	fprintf_retiffail(fp,"is_right %d\n", c.is_right_ref_ ? 1 : 0 );
	fprintf_retiffail(fp,"is_free %d\n", c.is_free_ ? 1 : 0 );
	return 0;
}


int binmap_t::read_cell(FILE *fp,cell_t *c)
{
	bitmap_t left,right;
	int is_left,is_right,is_free;
	fscanf_retiffail(fp,"leftb %d\n", &left);
	fscanf_retiffail(fp,"rightb %d\n", &right);
	fscanf_retiffail(fp,"is_left %d\n", &is_left );
	fscanf_retiffail(fp,"is_right %d\n", &is_right );
	fscanf_retiffail(fp,"is_free %d\n", &is_free );

	//fprintf(stderr,"binmapread_cell: l%ld r%ld %d %d %d\n", left, right, is_left, is_right, is_free );

	c->left_.bitmap_ = left;
	c->right_.bitmap_ = right;
	c->is_left_ref_ = (bool)is_left;
	c->is_right_ref_ = (bool)is_right;
	c->is_free_ = (bool)is_free;

	return 0;
}

// Arno, 2011-10-20: Persistent storage
int binmap_t::serialize(FILE *fp)
{
	 fprintf_retiffail(fp,"root bin %lli\n",root_bin_.toUInt() );
	 fprintf_retiffail(fp,"free top %i\n",free_top_ );
	 fprintf_retiffail(fp,"alloc cells " PRISIZET"\n", allocated_cells_number_);
	 fprintf_retiffail(fp,"cells num " PRISIZET"\n", cells_number_);
	 for (size_t i=0; i<cells_number_; i++)
	 {
		if (write_cell(fp,cell_[i]) < 0)
			return -1;
	 }
	 return 0;
}




int binmap_t::deserialize(FILE *fp)
{
	 bin_t::uint_t rootbinval;
	 ref_t freetop;
	 size_t alloccells,cells;
	 fscanf_retiffail(fp,"root bin %lli\n", &rootbinval );
	 fscanf_retiffail(fp,"free top %i\n", &freetop );
	 fscanf_retiffail(fp,"alloc cells " PRISIZET"\n", &alloccells);
	 fscanf_retiffail(fp,"cells num " PRISIZET"\n", &cells);

	 //fprintf(stderr,"Filling BINMAP %p\n", this );
	 //fprintf(stderr,"Rootbin %lli freetop %li alloc %li num %li\n", rootbinval, freetop, alloccells, cells );

	 root_bin_ = bin_t(rootbinval);
	 free_top_ = freetop;
	 allocated_cells_number_ = alloccells;
	 cells_number_ = cells;
	 if (cell_ != NULL) {
		 free(cell_);
	 }
	 cell_ = (cell_t *)new cell_t[cells];
	 size_t i=0;
	 for (i=0; i<cells; i++)
	 {
		/* Cleans cell */
		memset(&cell_[i], 0, sizeof(cell_[0]));
		if (read_cell(fp,&cell_[i]) < 0)
			return -1;
	 }
	 return 0;
}
