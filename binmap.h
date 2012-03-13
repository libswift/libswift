#ifndef __binmap_h__
#define __binmap_h__

#include <cstddef>
#include "bin.h"
#include "compat.h"
#include "serialize.h"

namespace swift {

/**
 * Binmap class
 */
class binmap_t : Serializable {
public:
    /** Type of bitmap */
    typedef int32_t bitmap_t;
    /** Type of reference */
    typedef uint32_t ref_t;


    /**
     * Constructor
     */
    binmap_t();


    /**
     * Destructor
     */
    ~binmap_t();


    /**
     * Set the bin
     */
    void set(const bin_t& bin);


    /**
     * Reset the bin
     */
    void reset(const bin_t& bin);


    /**
     * Empty all bins
     */
    void clear();


    /**
     * Ric: Fill all bins, size is given by the source's root
     */
    void fill(const binmap_t& source);


    /**
     * Whether binmap is empty
     */
    bool is_empty() const;


    /**
     * Whether binmap is filled
     */
    bool is_filled() const;


    /**
     * Whether range/bin is empty
     */
    bool is_empty(const bin_t& bin) const;


    /**
     * Whether range/bin is filled
     */
    bool is_filled(const bin_t& bin) const;


    /**
     * Return the topmost solid bin which covers the specified bin
     */
    bin_t cover(const bin_t& bin) const;


    /**
     * Find first empty bin
     */
    bin_t find_empty() const;


    /**
     * Find first filled bin
     */
    bin_t find_filled() const;


    /**
     * Get number of allocated cells
     */
    size_t cells_number() const;


    /**
     * Get total size of the binmap (Arno: =number of bytes it occupies in memory)
     */
    size_t total_size() const;


    /**
     * Echo the binmap status to stdout
     */
    void status() const;


    /**
     * Find first additional bin in source
     */
    static bin_t find_complement(const binmap_t& destination, const binmap_t& source, const bin_t::uint_t twist);


    /**
     * Find first additional bin of the source inside specified range
     */
    static bin_t find_complement(const binmap_t& destination, const binmap_t& source, bin_t range, const bin_t::uint_t twist);


    /**
     * Copy one binmap to another
     */
    static void copy(binmap_t& destination, const binmap_t& source);


    /**
     * Copy a range from one binmap to another binmap
     */
    static void copy(binmap_t& destination, const binmap_t& source, const bin_t& range);


    // Arno, 2011-10-20: Persistent storage
    int serialize(FILE *fp);
    int deserialize(FILE *fp);
private:
    #pragma pack(push, 1)

    /**
     * Structure of cell halves
     */
    typedef struct {
        union {
            bitmap_t bitmap_;
            ref_t ref_;
        };
    } half_t;

    /**
     * Structure of cells
     */
    typedef union {
        struct {
            half_t left_;
            half_t right_;
            bool is_left_ref_ : 1;
            bool is_right_ref_ : 1;
            bool is_free_ : 1;
        };
        ref_t free_next_;
    } cell_t;

    #pragma pack(pop)

private:

    /** Allocates one cell (dirty allocation) */
    ref_t _alloc_cell();

    /** Allocates one cell */
    ref_t alloc_cell();

    /** Reserve cells allocation capacity */
    bool reserve_cells(size_t count);

    /** Releases the cell */
    void free_cell(ref_t cell);

    /** Extend root */
    bool extend_root();

    /** Pack a trace of cells */
    void pack_cells(ref_t* cells);


    /** Pointer to the list of blocks */
    cell_t* cell_;

    /** Number of available cells */
    size_t cells_number_;

    /** Number of allocated cells */
    size_t allocated_cells_number_;

    /** Front of the free cell list */
    ref_t free_top_;

    /** The root bin */
    bin_t root_bin_;


    /** Trace the bin */
    void trace(ref_t* ref, bin_t* bin, const bin_t& target) const;

    /** Trace the bin */
    void trace(ref_t* ref, bin_t* bin, ref_t** history, const bin_t& target) const;


    /** Sets low layer bitmap */
    void _set__low_layer_bitmap(const bin_t& bin, const bitmap_t bitmap);

    /** Sets high layer bitmap */
    void _set__high_layer_bitmap(const bin_t& bin, const bitmap_t bitmap);


    /** Clone binmap cells to another binmap */
    static void copy(binmap_t& destination, const ref_t dref, const binmap_t& source, const ref_t sref);

    static void _copy__range(binmap_t& destination, const binmap_t& source, const ref_t sref, const bin_t sbin);


    /** Find first additional bin in source */
    static bin_t _find_complement(const bin_t& bin, const ref_t dref, const binmap_t& destination, const ref_t sref, const binmap_t& source, const bin_t::uint_t twist);
    static bin_t _find_complement(const bin_t& bin, const bitmap_t dbitmap, const ref_t sref, const binmap_t& source, const bin_t::uint_t twist);
    static bin_t _find_complement(const bin_t& bin, const ref_t dref, const binmap_t& destination, const bitmap_t sbitmap, const bin_t::uint_t twist);
    static bin_t _find_complement(const bin_t& bin, const bitmap_t dbitmap, const bitmap_t sbitmap, const bin_t::uint_t twist);


    /* Disabled */
    binmap_t& operator = (const binmap_t&);

    /* Disabled */
    binmap_t(const binmap_t&);

    // Arno, 2011-10-20: Persistent storage
    int write_cell(FILE *fp,cell_t c);
    int read_cell(FILE *fp,cell_t *c);
};

} // namespace end

#endif /*_binmap_h__*/
