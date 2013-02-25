/*
 *  livetree.h
 *
 *  Implementation of the Unified Merkle Tree approach for content
 *  integrity protection during live streaming
 *
 *  Created by Arno Bakker, Victor Grishchenko
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#ifndef SWIFT_LIVE_HASH_TREE_H
#define SWIFT_LIVE_HASH_TREE_H


#include "swift.h"
#include "hashtree.h"

namespace swift {

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
    LHT_STATE_SIGN_EMPTY,      // live source, no data yet
    LHT_STATE_SIGN_DATA,       // live source, some data, so peaks and transient root known
    LHT_STATE_VER_AWAIT_PEAK, // live client, has root key, needs peak
    LHT_STATE_VER_AWAIT_DATA,  // live client
} lht_state_t;

typedef int privkey_t;
typedef long pubkey_t;

class LiveHashTree: public HashTree
{
   public:
     /** live source */
     LiveHashTree(Storage *storage, privkey_t privkey, uint32_t chunk_size, bool check_netwvshash);
     /** live client */
     LiveHashTree(Storage *storage, pubkey_t swarmid, uint32_t chunk_size, bool check_netwvshash);
     ~LiveHashTree();


     void PurgeTree(bin_t pos);

     /** Called when a chunk is added */
     bin_t AddData(const char* data, size_t length);
     /** Called after N chunks have been added, following -06 */
     binvector_t UpdateSignedPeaks();

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

     bool 	     get_check_netwvshash();
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
     Storage *	    storage_;

     //NETWVSHASH
     bool            check_netwvshash_;

     // SIGNEDPEAK
     /** List of currently signed peak hashes. Updated every N chunks */
     bin_t           signed_peak_bins_[64];
     int             signed_peak_count_;
     /** Actual signatures */
     uint8_t *       signed_peak_hashes_[64];

     /** Create a new leaf Node next to the current latest leaf (pointed to by
      * addcursor_). This may involve creating a new root and subtree to
      * accommodate it. */
     Node *	     CreateNext();

     void 	     FreeTree(Node *n);
     Sha1Hash        DeriveRoot();
};

}

#endif
