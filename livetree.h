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


  protected:
    Node *parent_;
    Node *leftc_;
    Node *rightc_;
    bin_t b_;
    Sha1Hash h_;
};


class LiveHashTree: public HashTree
{
   public:
     LiveHashTree();
     ~LiveHashTree();
     Node *GetRoot();
     void SetRoot(Node *r);
     void PurgeTree(Node *r);
     bin_t AddData(const char* data, size_t length);
     Node *CreateNext();
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
     Node *root_;
     /** Right-most base layer node */
     Node *addcursor_;

     bin_t           peak_bins_[64];
     int             peak_count_;
     uint64_t	     size_;
     uint32_t        chunk_size_;
};

}
