/*
 *  livehashtree.h
 *
 *  Implementation of the Unified Merkle Tree approach for content
 *  integrity protection during live streaming
 *
 *  Created by Arno Bakker, Victor Grishchenko
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 *  TODO:
 *  - what if SIGNED_INTEGRITY gets lost on UDP?
 *      DONE: Resend signed peak + all uncles on Tdata
 *  - When new client joins half-way epoch, mustn't send HAVEs for new, or don't reply to REQUESTs?
 *      DONE: See ack_signed_out() for source.
 *  - purge subtree, source and client
 *      DONE
 *  - Storage layer that remembers just part.
 *      DONE
 *  - Add unittest that expands tree and see if all hashes are there and verified bits set correctly
 *  - Split signed peaks into multiple datagrams when too big
 *      DONE: also do partial send in AddSignedPeakHashes when required
 *  - Split uncle hashes into multiple datagrams when too big
 *      DONE
 *  - For non-source providing chunks: it won't have left side of tree
 *    (but will have all hashes to check all chunks)
 *      DONE
 *  - Replace Sha1Hash swarmid with generic SwarmID that allows roothash+pubkey
 *
 *  - Can't prune tree if it contains uncles?
 *      No: was caused by hint buildup, see dont-prune-uncles.log
 *
 *  - Verify sizec_ correct
 *
 *  Something to note when working with the Unified Merkle Tree scheme:
 *
 *  With Unified Merkle Trees say we have a tree of 4 chunks with peak (2,0).
 *  When the tree is expanded from 4 to 8 chunks, the new peak becomes (3,0).
 *  The uncle hash algorithm assumes that (2,1) the hash of the 4 new chunks
 *  was sent to the peer when chunks from (2,0) where sent (after all, (2,1)
 *  is their uncle). However, that part of the tree did not yet exist, so it
 *  wasn't sent. We call these unsent uncles "ridge hashes".
 */
#ifndef SWIFT_LIVE_HASH_TREE_H
#define SWIFT_LIVE_HASH_TREE_H


#include "swift.h"
#include "hashtree.h"
#include "livesig.h"


namespace swift {

/** States for the live hashtree */
typedef enum {
    LHT_STATE_SIGN_EMPTY,      // live source, no chunks yet
    LHT_STATE_SIGN_DATA,       // live source, some chunks, so peaks and transient root known
    LHT_STATE_VER_AWAIT_PEAK,  // live client, has pub key, needs peak
    LHT_STATE_VER_AWAIT_DATA,  // live client, has pub key, needs chunks
} lht_state_t;


struct SigTintTuple
{
    Signature	s_;
    tint	t_;
    SigTintTuple() : s_(Signature::NOSIG), t_(TINT_NEVER) {}
    SigTintTuple(const Signature &s, tint t) : s_(s), t_(t) {}
    SigTintTuple(const SigTintTuple &copy)
    {
	s_ = copy.s_;
	t_ = copy.t_;
    }
    Signature &sig() { return s_; }
    tint      time() { return t_; }

    const static SigTintTuple NOSIGTINT;
};


class Node;

class Node
{
    /** Class representing a node in a hashtree */
  public:
    Node();
    ~Node();
    void SetParent(Node *parent);
    Node *GetParent();
    void SetChildren(Node *leftc, Node *rightc);
    Node *GetLeft();
    Node *GetRight();
    Sha1Hash &GetHash();
    void SetHash(const Sha1Hash &hash);
    bin_t &GetBin();
    void SetBin(bin_t b);
    /** whether hash checked against signed peak (client) or calculated (source) */
    void SetVerified(bool val);
    bool GetVerified();
    void SetSigTint(SigTintTuple *stptr);
    SigTintTuple *GetSigTint();


  protected:
    Node *parent_;
    Node *leftc_;
    Node *rightc_;
    bin_t b_;
    Sha1Hash h_;
    SigTintTuple *stptr_; // use Tuple to same some memory
    bool  verified_;
};



/** Structure for holding a (bin,hash,signature+timestamp) tuple */
struct BinHashSigTuple
{
    bin_t	b_;
    Sha1Hash	h_;
    SigTintTuple st_;
    BinHashSigTuple(const bin_t &b, const Sha1Hash &h, const SigTintTuple &st) : b_(b), h_(h), st_(st) {}
    BinHashSigTuple(const BinHashSigTuple &copy)
    {
	b_ = copy.b_;
	h_ = copy.h_;
	st_ = copy.st_;
    }
    bin_t &bin() { return b_; }
    Sha1Hash &hash() { return h_; }
    SigTintTuple &sigtint() { return st_; }

    const static BinHashSigTuple NOBULL;
};


/** Dynamic hash tree */
class LiveHashTree: public HashTree
{
   public:
     /** live source */
     LiveHashTree(Storage *storage, KeyPair &keypair, uint32_t chunk_size, uint32_t nchunks_per_sign);
     /** live client */
     LiveHashTree(Storage *storage, KeyPair &pubkeypair, uint32_t chunk_size);
     ~LiveHashTree();

     /** Called when a chunk is added */
     bin_t          AddData(const char* data, size_t length);
     bin_t          GetMunro(bin_t pos);
     bin_t          GetLastMunro();
     /** Called after N chunks have been added, following -06. Returns new munro */
     BinHashSigTuple AddSignedMunro();

     /** Return bin,hash,sig of munro */
     BinHashSigTuple GetSignedMunro(bin_t munro); // LIVECHECKPOINT

     /** Live NCHUNKS_PER_SIG  */
     void 	    SetNChunksPerSig(uint32_t nchunks_per_sig) { nchunks_per_sig_=nchunks_per_sig; }
     uint32_t	    GetNChunksPerSig() { return nchunks_per_sig_; }

     /** Remove subtree rooted at pos */
     void           PruneTree(bin_t pos);

     bool           OfferSignedMunroHash(bin_t pos, SigTintTuple &sigtint);

     /** Add node to the hashtree */
     bool CreateAndVerifyNode(bin_t pos, const Sha1Hash &hash, bool verified);
     /** Mark node as verified. verclass indicates where verification decision came from for debugging */
     bool SetVerifiedIfNot0(Node *piter, bin_t p, int verclass);

     // LIVECHECKPOINT
     bool InitFromCheckpoint(BinHashSigTuple lastmunrotup);

     /** Returns size of signature on the wire */
     uint16_t       GetSigSizeInBytes();


     // Sanity checks
     void sane_tree();
     void sane_node(Node *n, Node *parent);


     // HashTree interface
     bool            OfferHash (bin_t pos, const Sha1Hash& hash);
     bool            OfferData (bin_t bin, const char* data, size_t length);
     int             peak_count () const;
     bin_t           peak (int i) const;
     const Sha1Hash& peak_hash (int i) const;
     bin_t           peak_for (bin_t pos) const;
     const Sha1Hash& hash (bin_t pos) const;
     const Sha1Hash& root_hash () const;
     uint64_t        size () const;
     uint64_t        size_in_chunks () const;
     uint64_t        complete () const;
     uint64_t        chunks_complete () const;
     uint64_t        seq_complete(int64_t offset); // SEEK
     bool            is_complete ();
     binmap_t *      ack_out ();
     uint32_t        chunk_size();

     Storage *       get_storage();
     void            set_size(uint64_t);

     /** Find node for bin. (unprotected for testing) */
     Node *	     FindNode(bin_t pos) const;

   protected:
     lht_state_t     state_;
     Node 	     *root_;
     /** Live source: Right-most base layer node */
     Node 	     *addcursor_;

     KeyPair	     keypair_;

     // From MmapHashTree
     /** Merkle hash tree: peak hashes */
     bin_t           peak_bins_[64];
     int             peak_count_;
     /** Base size, as derived from the hashes. */
     uint64_t        size_;
     uint64_t        sizec_;
     /** Part of the tree currently checked. */
     uint64_t        complete_;
     uint64_t        completec_;
     /**    Binmap of own chunk availability */
     binmap_t        ack_out_;

     // CHUNKSIZE
     /** Arno: configurable fixed chunk size in bytes */
     uint32_t        chunk_size_;

     //MULTIFILE
     Storage *	     storage_;

     bin_t           source_last_munro_;

     /** Temp storage for candidate peak. */
     bin_t           cand_munro_bin_;
     Sha1Hash	     cand_munro_hash_;

     /** Number of chunks before signing new peaks (NCHUNKS_PER_SIG param in -06) */
     uint32_t        nchunks_per_sig_;

     /** Create a new leaf Node next to the current latest leaf (pointed to by
      * addcursor_). This may involve creating a new root and subtree to
      * accommodate it. */
     Node *	     CreateNext();
     /** Find the Node in the tree for the given bin. */
     void	     ComputeTree(Node *start);
     /** Deallocate a node. */
     void 	     FreeTree(Node *n);
     /** Calculate root hash of current tree (unused). */
     Sha1Hash        DeriveRoot();

     bin_t          GetClientLastMunro();

};

}

#endif
