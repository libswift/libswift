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
 *  - Storage layer that remembers just part.
 *  - Add unittest that expands tree and see if all hashes are there and verified bits set correctly
 *  - Make sure signed peak messages not too big
 *  - Split signed peaks into multiple datagrams when too big
 *      TODO: also do partial send in AddSignedPeakHashes when required
 *  - Split uncle hashes into multiple datagrams when too big
 *  - For non-source providing chunks: it won't have left side of tree
 *    (but will have all hashes to check all chunks)
 *  - Problem with hook-in and missing hashtree? Arno: what missing hashtree?
 */
#ifndef SWIFT_LIVE_HASH_TREE_H
#define SWIFT_LIVE_HASH_TREE_H


#include "swift.h"
#include "hashtree.h"

namespace swift {

#define DUMMY_DEFAULT_SIG_LENGTH	20	// SIGNPEAKTODO

class Node;

class Node
{
  public:
    Node();
    void SetParent(Node *parent);
    Node *GetParent();
    void SetChildren(Node *leftc, Node *rightc);
    Node *GetLeft();
    Node *GetRight();
    Sha1Hash &GetHash();
    void SetHash(const Sha1Hash &hash);
    bin_t &GetBin();
    void SetBin(bin_t &b);
    /** whether hash checked against signed peak (client) or calculated (source) */
    void SetVerified(bool val);
    bool GetVerified();


  protected:
    Node *parent_;
    Node *leftc_;
    Node *rightc_;
    bin_t b_;
    Sha1Hash h_;
    bool  verified_;
};


typedef enum {
    LHT_STATE_SIGN_EMPTY,      // live source, no chunks yet
    LHT_STATE_SIGN_DATA,       // live source, some chunks, so peaks and transient root known
    LHT_STATE_VER_AWAIT_PEAK,  // live client, has pub key, needs peak
    LHT_STATE_VER_AWAIT_DATA,  // live client, has pub key, needs chunks
} lht_state_t;

#ifndef ARNOCRYPTO
typedef int privkey_t;
typedef Sha1Hash pubkey_t;
#endif


struct Signature
{
    uint8_t    *sigbits_;
    uint16_t   siglen_;
    Signature() : sigbits_(NULL), siglen_(0)  {}
    Signature(uint8_t *sb, uint16_t len);
    Signature(const Signature &copy);
    Signature & operator = (const Signature &source);
    ~Signature();
    uint8_t  *bits()  { return sigbits_; }
    uint16_t length() { return siglen_; }
};


struct BinHashSigTuple
{
    bin_t	b_;
    Sha1Hash	h_;
    Signature	s_;
    BinHashSigTuple(const bin_t &b, const Sha1Hash &h, const Signature &s) : b_(b), h_(h), s_(s) {}
    BinHashSigTuple(const BinHashSigTuple &copy)
    {
	b_ = copy.b_;
	h_ = copy.h_;
	s_ = copy.s_;
    }
    bin_t &bin() { return b_; }
    Sha1Hash &hash() { return h_; }
    Signature &sig() { return s_; }
};

typedef std::vector<BinHashSigTuple>	bhstvector;


class LiveHashTree: public HashTree
{
   public:
     /** live source */
     LiveHashTree(Storage *storage, privkey_t privkey, uint32_t chunk_size);
     /** live client */
     LiveHashTree(Storage *storage, pubkey_t swarmid, uint32_t chunk_size);
     ~LiveHashTree();


     void           PurgeTree(bin_t pos);

     /** Called when a chunk is added */
     bin_t          AddData(const char* data, size_t length);
     /** Called after N chunks have been added, following -06. Returns new peaks */
     bhstvector	    UpdateSignedPeaks();
     /*int            signed_peak_count();
     bin_t          signed_peak(int i);
     bin_t          signed_peak_for (bin_t pos) const; */
     bhstvector	    GetCurrentSignedPeakTuples();

     bool OfferSignedPeakHash(bin_t pos,const uint8_t *signedhash);
     bool CreateAndVerifyNode(bin_t pos, const Sha1Hash &hash, bool verified);

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
     int             TESTGetFD();


   protected:
     lht_state_t     state_;
     Node 	     *root_;
     /** Live source: Right-most base layer node */
     Node 	     *addcursor_;

     privkey_t	     privkey_;
     pubkey_t	     pubkey_;

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

     // SIGNPEAK
     /** List of currently signed peak hashes. Updated every N chunks */
     bin_t           signed_peak_bins_[64];
     int             signed_peak_count_;
     /** Actual signatures */
     Signature       signed_peak_sigs_[64];
     // TODO: cache peak sigs

     /** Temp storage for candidate peak */
     bin_t	     cand_peak_bin_;
     Sha1Hash	     cand_peak_hash_;

     /** Create a new leaf Node next to the current latest leaf (pointed to by
      * addcursor_). This may involve creating a new root and subtree to
      * accommodate it. */
     Node *	     CreateNext();
     Node *	     FindNode(bin_t pos) const;
     void	     ComputeTree(Node *start);

     void 	     FreeTree(Node *n);
     Sha1Hash        DeriveRoot();

     void check_peak_coverage();
     void check_signed_peak_coverage();
     void check_new_peaks(bhstvector &newpeaktuples);
};

}

#endif
