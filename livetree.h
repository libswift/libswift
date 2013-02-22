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
typedef int pubkey_t;

class LiveHashTree: public HashTree
{
   public:
     LiveHashTree(privkey_t privkey); // live source
     LiveHashTree(pubkey_t swarmid, bool check_netwvshash); // live client
     ~LiveHashTree();

     Node *GetRoot();
     void SetRoot(Node *r);
     void PurgeTree(Node *r);

     bin_t AddData(const char* data, size_t length);
     Node *CreateNext();
     bool OfferSignedPeakHash(bin_t pos,const uint8_t *signedhash);
     bool CreateAndVerifyNode(bin_t pos, const Sha1Hash &hash, bool verified);

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


     bin_t           peak_bins_[64];
     int             peak_count_;
     uint64_t	     size_;
     uint64_t	     sizec_;
     uint32_t        chunk_size_;

     /**    Binmap of own chunk availability */
     binmap_t        ack_out_;

     bool            check_netwvshash_;
};

}
