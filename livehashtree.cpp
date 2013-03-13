/*
 *  livehashtree.cpp
 *
 *  Created by Arno Bakker, Victor Grishchenko
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#include "swift.h"
#include "bin_utils.h"

using namespace swift;


#define  tree_debug	true


/*
 * Signature
 */


Signature::Signature(uint8_t *sb, uint16_t len) : sigbits_(NULL), siglen_(0)
{
    if (len == 0)
	return;
    siglen_ = len;
    sigbits_ = new uint8_t[siglen_];
    memcpy(sigbits_,sb,siglen_);
}

Signature::Signature(const Signature &copy) : sigbits_(NULL), siglen_(0)
{
    if (copy.siglen_ == 0)
	return;

    siglen_ = copy.siglen_;
    sigbits_ = new uint8_t[siglen_];
    memcpy(sigbits_,copy.sigbits_,siglen_);
}

Signature::~Signature()
{
    if (sigbits_ != NULL)
	delete sigbits_;
    sigbits_ = NULL;
}

Signature & Signature::operator= (const Signature & source)
{
    if (this != &source)
     {
         if (source.siglen_ == 0)
         {
             siglen_ = 0;
             if (sigbits_ != NULL)
                 delete sigbits_;
             sigbits_ = NULL;
         }
         else
         {
            siglen_ = source.siglen_;
            sigbits_ = new uint8_t[source.siglen_];
            memcpy(sigbits_,source.sigbits_,source.siglen_);
         }
     }
     return *this;
 }


/*
 * Node
 */

Node::Node() : parent_(NULL), leftc_(NULL), rightc_(NULL), b_(bin_t::NONE), h_(Sha1Hash::ZERO), verified_(false)
{
}

void Node::SetParent(Node *parent)
{
    parent_ = parent; // TODOinline
}

Node *Node::GetParent()
{
    return parent_; // TODOinline
}


void Node::SetChildren(Node *leftc, Node *rightc)
{
    leftc_ = leftc;
    rightc_ = rightc;
}

Node *Node::GetLeft()
{
    return leftc_;
}

Node *Node::GetRight()
{
    return rightc_;
}


Sha1Hash &Node::GetHash()
{
    return h_;
}

void Node::SetHash(const Sha1Hash &hash)
{
    h_ = hash;
}

bin_t &Node::GetBin()
{
    return b_;
}

void Node::SetBin(bin_t &b)
{
    b_ = b;
}

bool Node::GetVerified()
{
    return verified_;
}

void Node::SetVerified(bool val)
{
    verified_ = val;
}


LiveHashTree::LiveHashTree(Storage *storage, privkey_t privkey, uint32_t chunk_size) :
 HashTree(), state_(LHT_STATE_SIGN_EMPTY), root_(NULL), addcursor_(NULL), privkey_(privkey), peak_count_(0), size_(0), sizec_(0), complete_(0), completec_(0),
 chunk_size_(chunk_size), storage_(storage), signed_peak_count_(0)
{
}

LiveHashTree::LiveHashTree(Storage *storage, pubkey_t swarmid, uint32_t chunk_size) :
 HashTree(), state_(LHT_STATE_VER_AWAIT_PEAK), root_(NULL), addcursor_(NULL), pubkey_(swarmid), peak_count_(0), size_(0), sizec_(0), complete_(0), completec_(0),
 chunk_size_(chunk_size), storage_(storage), signed_peak_count_(0)
{
}

LiveHashTree::~LiveHashTree()
{
    // TODO free nodes
    if (root_ == NULL)
	return;
    else
    {
	FreeTree(root_);
    }
}

void LiveHashTree::FreeTree(Node *n)
{
    if (n->GetLeft() != NULL)
    {
	FreeTree(n->GetLeft());
    }
    if (n->GetRight() != NULL)
    {
	FreeTree(n->GetRight());
    }
    delete n;
}


void LiveHashTree::PurgeTree(bin_t pos)
{
}


/*
 * Live source specific
 */

bin_t LiveHashTree::AddData(const char* data, size_t length)
{
    // Source adds new data

    if (tree_debug)
    {
	if (addcursor_ == NULL)
	    fprintf(stderr,"AddData: addcursor_ NULL\n");
	else
	    fprintf(stderr,"AddData: addcursor_: %s\n", addcursor_->GetBin().str().c_str() );
    }

    Sha1Hash hash(data,length);
    Node *next = CreateNext();
    next->SetHash(hash);
    next->SetVerified(true); // Mark node as computed

    if (tree_debug)
	fprintf(stderr,"AddData: set %s hash %s\n", next->GetBin().str().c_str(), next->GetHash().hex().c_str() );

    // Calc new peaks
    size_ += length;
    sizec_++;
    complete_ += length;
    completec_++;
    peak_count_ = gen_peaks(size_in_chunks(),peak_bins_);

    state_ = LHT_STATE_SIGN_DATA;

    return next->GetBin();
}


Node *LiveHashTree::CreateNext()
{
    if (addcursor_ == NULL)
    {
	if (tree_debug)
	    fprintf(stderr,"CreateNext: create root\n" );
	root_ = new Node();
	root_->SetBin(bin_t(0,0));
	addcursor_ = root_;
    }
    else if (addcursor_->GetBin().is_left())
    {
	// Left child, create sibling
	Node *newright = new Node();
	newright->SetBin(addcursor_->GetBin().sibling());

	if (tree_debug)
	    fprintf(stderr,"CreateNext: create sibling %s\n", newright->GetBin().str().c_str() );

	Node *par = addcursor_->GetParent();
	if (par == NULL)
	{
	    // We was root, create new parent
	    par = new Node();
	    par->SetBin(bin_t(addcursor_->GetBin().layer()+1,0));
	    root_ = par;

	    if (tree_debug)
		fprintf(stderr,"CreateNext: create new root %s\n", root_->GetBin().str().c_str() );
	}
	par->SetChildren(addcursor_,newright);
	newright->SetParent(par);
	addcursor_->SetParent(par);
	addcursor_ = newright;
    }
    else
    {
	if (tree_debug)
	    fprintf(stderr,"CreateNext: create tree\n");

	// We right child, need next
	Node *iter = addcursor_;
	while(true)
	{
	    iter = iter->GetParent();
	    if (tree_debug)
		fprintf(stderr,"CreateNext: create tree: check %s\n", iter->GetBin().str().c_str() );

	    if (iter == root_)
	    {
		// Need new root
		Node *newroot = new Node();
		newroot->SetBin(bin_t(iter->GetBin().layer()+1,0));

		if (tree_debug)
		    fprintf(stderr,"CreateNext: create tree: new root %s\n", newroot->GetBin().str().c_str() );

		newroot->SetChildren(iter,NULL);
		root_ = newroot;
		iter->SetParent(newroot);
		iter = newroot;
	    }
	    if (iter->GetRight() == NULL) // not elsif
	    {
		// Create new subtree
		Node *newright = new Node();
		newright->SetBin(iter->GetBin().right());

		if (tree_debug)
		    fprintf(stderr,"CreateNext: create tree: new right %s\n", newright->GetBin().str().c_str() );

		iter->SetChildren(iter->GetLeft(),newright);
		newright->SetParent(iter);

		// Need tree down to base layer
		int depth = iter->GetBin().layer()-1;

		iter = newright;
		Node *newleft = NULL;
		for (int i=0; i<depth; i++)
		{
		    newleft = new Node();
		    newleft->SetBin(iter->GetBin().left());

		    if (tree_debug)
			fprintf(stderr,"CreateNext: create tree: new left down %s\n", newleft->GetBin().str().c_str() );

		    iter->SetChildren(newleft,NULL);
		    newleft->SetParent(iter);

		    iter = newleft;
		}
		addcursor_ = newleft;
		break;
	    }
	    // if left, continue
	}
    }
    return addcursor_;

}


bhstvector LiveHashTree::UpdateSignedPeaks()
{
    // Calc peak diffs
    bool changed=false;

    int i=0;
    if (signed_peak_count_ == peak_count_ && peak_count_ != 0)
    {
	for (i=0; i<signed_peak_count_; i++)
	{
	    if (signed_peak_bins_[i] != peak_bins_[i])
	    {
		changed = true;
		break;
	    }
	}
    }
    else
	changed = true;

    bhstvector newpeaktuples;
    if (!changed)
	return newpeaktuples;

    // Arno, 2013-03-06: Determine which peaks have been subsumed by new ones.
    /* for (i=0; i<peak_count_; i++)
    {
	bin_t peak = peak_bins_[i];
	for (int j=0; j<signed_peak_count_; j++)
	{
	    bin_t signed_peak = signed_peak_bins_[j];
	    if (peak != signed_peak && peak.contains(signed_peak))
	    {
		// Save info for old peak: bin,hash,sig
                fprintf(stderr,"UpdateSignedPeaks: %s subsumed by %s\n", signed_peak.str().c_str(), peak.str().c_str() );
		Signature copysig = signed_peak_sigs_[j];
		BinHashSigTuple bhst(signed_peak,hash(signed_peak),copysig);
		subsumed.push_back(bhst);
	    }
	}
    } */

    int startidx=0;
    changed = false;

    // Copy new peaks to signed peaks
    signed_peak_count_ = peak_count_;
    for (i=0; i<peak_count_; i++)
    {
	if (peak_bins_[i] != signed_peak_bins_[i])
	{
            fprintf(stderr,"UpdateSignedPeaks: new %s \n", peak_bins_[i].str().c_str() );

	    signed_peak_bins_[i] = peak_bins_[i];

	    if (!changed)
	    {
		startidx = i;
		changed = true;
	    }
	}
    }
    // Clear old peaks
    for (i=peak_count_; i<signed_peak_count_; i++)
    {
	signed_peak_sigs_[i] = Signature();
    }

    check_signed_peak_coverage();


    // Now the trees below peaks are stable, so we can calculate the hash tree
    // for them.
    for (i=startidx; i<signed_peak_count_; i++)
    {
	bin_t newpeak = signed_peak_bins_[i];
	fprintf(stderr,"UpdateSignedPeaks: compute till %s\n", signed_peak_bins_[i].str().c_str() );
	Node *spnode = FindNode(newpeak);
	if (spnode == NULL)
	{
	    fprintf(stderr,"UpdateSignedPeaks: cannot find peak?!\n");
	    return newpeaktuples;
	}
	ComputeTree(spnode);

	// Hash of new peak known, now sign and store for transmission
	Sha1Hash hash = spnode->GetHash();
	uint8_t* signedhash = new uint8_t[DUMMY_DEFAULT_SIG_LENGTH]; // placeholder
        for (int k=0; k<20; k++)
            signedhash[k] = 'v';
        signedhash[19] = '\0';
        Signature sig(signedhash,DUMMY_DEFAULT_SIG_LENGTH);
        delete signedhash;

        signed_peak_sigs_[i] = sig;

	BinHashSigTuple bhst(newpeak,hash,sig);
	newpeaktuples.push_back(bhst);
    }

    check_new_peaks(newpeaktuples);

    return newpeaktuples;
}


bhstvector LiveHashTree::GetCurrentSignedPeakTuples()
{
    bhstvector peaktuples;

    for (int j=0; j<signed_peak_count_; j++)
    {
	// Save info for old peak: bin,hash,sig
	bin_t signed_peak = signed_peak_bins_[j];
	Signature copysig = signed_peak_sigs_[j];
	BinHashSigTuple bhst(signed_peak,hash(signed_peak),copysig);

	peaktuples.push_back(bhst);
    }
    return peaktuples;
}


void LiveHashTree::ComputeTree(Node *start)
{
    fprintf(stderr,"ComputeTree: start %s\n", start->GetBin().str().c_str() );
    if (!start->GetVerified())
    {
	ComputeTree(start->GetLeft());
	ComputeTree(start->GetRight());
	if (!start->GetLeft()->GetVerified())
	    fprintf(stderr,"ComputeTree: left failed to become verified!");
	if (!start->GetRight()->GetVerified())
	    fprintf(stderr,"ComputeTree: right failed to become verified!");
	Sha1Hash h(start->GetLeft()->GetHash(),start->GetRight()->GetHash());
	start->SetHash(h);
	start->SetVerified(true);
    }
}



/* int LiveHashTree::signed_peak_count()
{
    return signed_peak_count_;
}

bin_t LiveHashTree::signed_peak(int i)
{
    return signed_peak_bins_[i];
}

uint8_t *LiveHashTree::signed_peak_sig(int i)
{
    return signed_peak_sigs_[i];
}

int LiveHashTree::signed_peak_sig_length(int i)
{
    return signed_peak_sigs_len_[i];
}


// TEMP FORCE TRYOUT signed_peak_for pos will be lower than peak_for, so just optimization
bin_t LiveHashTree::signed_peak_for(bin_t pos) const
{
    for (int i=0; i<signed_peak_count_; i++)
    {
	//fprintf(stderr,"signed_peak_for: %s covers %s to %s\n", signed_peak_bins_[i].str().c_str(), signed_peak_bins_[i].base_left().str().c_str(), signed_peak_bins_[i].base_right().str().c_str());
	if (signed_peak_bins_[i].contains(pos))
	    return signed_peak_bins_[i];
    }
    return bin_t::NONE;
}*/



Sha1Hash  LiveHashTree::DeriveRoot()
{
    // From MmapHashTree

    int c = peak_count_-1;
    bin_t p = peak_bins_[c];
    Sha1Hash hash = this->hash(p);
    c--;
    // Arno, 2011-10-14: Root hash = top of smallest tree covering content IMHO.
    //while (!p.is_all()) {
    while (c >= 0) {
        if (p.is_left()) {
            p = p.parent();
            hash = Sha1Hash(hash,Sha1Hash::ZERO);
        } else {
            if (c<0 || peak_bins_[c]!=p.sibling())
                return Sha1Hash::ZERO;
            hash = Sha1Hash(this->hash(peak_bins_[c]),hash);
            p = p.parent();
            c--;
        }
    }

    //fprintf(stderr,"DeriveRoot: root hash is %s\n", hash.hex().c_str() );
    //fprintf(stderr,"DeriveRoot: bin is %s\n", p.str().c_str() );
    return hash;
}



/*
 * Live client specific
 */

bool LiveHashTree::OfferSignedPeakHash(bin_t pos, const uint8_t *signedhash)
{
    // TODO check sig
    // TODO store sig

    if (tree_debug)
	fprintf(stderr,"OfferSignedPeakHash: peak %s\n", pos.str().c_str() );

    if (pos != cand_peak_bin_)
    {
	// Ignore duplicate (or message mixup)
        if (tree_debug)
	    fprintf(stderr,"OfferSignedPeakHash: message mixup! %s %s\n", pos.str().c_str(), cand_peak_bin_.str().c_str() );
	return true;
    }

    // TODO: must remove old peaks if consumed by new
    int i=0;
    bool stored=false;
    while (i<peak_count_)
    {
	if (pos == peak_bins_[i])
	{
	    stored = true;
	    break;
	}
	else if (pos.contains(peak_bins_[i]))
	{
            if (tree_debug)
	        fprintf(stderr,"OfferSignedPeakHash: %s contains %s, update\n", pos.str().c_str(), peak_bins_[i].str().c_str() );

	    if (!stored)
	    {
                if (tree_debug)
		    fprintf(stderr,"OfferSignedPeakHash: overwriting\n" );
		peak_bins_[i] = pos;
		stored = true;
	    }
	    else
	    {
                if (tree_debug)
		    fprintf(stderr,"OfferSignedPeakHash: subsume %i\n", i );

		// This peak subsumed by new peak
		peak_count_--;
		for (int j=i; j<peak_count_; j++)
		{
                    if (tree_debug)
		        fprintf(stderr,"OfferSignedPeakHash: copy %d to %d\n", j, j+1 );
		    peak_bins_[j] = peak_bins_[j+1];
		}
		// Retest current i as it has been replaced
		continue;
	    }
	}
	i++;
    }
    if (!stored)
	peak_bins_[peak_count_++] = pos;

    check_peak_coverage();



    sizec_ = peak_bins_[peak_count_].base_right().layer_offset();
    size_ = sizec_ * chunk_size_;

    if (state_ == LHT_STATE_VER_AWAIT_PEAK)
	state_ = LHT_STATE_VER_AWAIT_DATA;

    CreateAndVerifyNode(cand_peak_bin_,cand_peak_hash_,true);

    // Could recalc root hash here, but never really used. Doing it on-demand
    // in root_hash() conflicts with const def :-(
    //root_->SetHash(DeriveRoot());

    return true;
}


void LiveHashTree::check_peak_coverage()
{
    // Sanity check
    bin_t::uint_t end = 0;
    for (int i=0; i<peak_count_; i++)
    {
	if (i == 0)
	{
	    end = peak_bins_[i].base_right().layer_offset();
	    continue;
	}
	bin_t::uint_t start = peak_bins_[i].base_left().layer_offset();
	if (start != end+1)
	{
	    fprintf(stderr,"peak broken!\n");
	    for (int j=0; j<peak_count_; j++)
	    {
		fprintf(stderr,"peak bork: %s covers %s to %s\n", peak_bins_[j].str().c_str(), peak_bins_[j].base_left().str().c_str(), peak_bins_[j].base_right().str().c_str());
	    }
            getchar();
	    exit(-1);
	}
	end = peak_bins_[i].base_right().layer_offset();
    }
}


void LiveHashTree::check_signed_peak_coverage()
{
    // Sanity check
    bin_t::uint_t end = 0;
    for (int i=0; i<signed_peak_count_; i++)
    {
        if (tree_debug)
	    fprintf(stderr,"UpdateSignedPeaks: signed peak is: %s\n", signed_peak_bins_[i].str().c_str() );
	if (i == 0)
	{
	    end = signed_peak_bins_[i].base_right().layer_offset();
	    continue;
	}
	bin_t::uint_t start = signed_peak_bins_[i].base_left().layer_offset();
	if (start != end+1)
	{
	    fprintf(stderr,"UpdateSignedPeaks: signed peak broken!\n");
	    for (int j=0; j<signed_peak_count_; j++)
	    {
		fprintf(stderr,"UpdateSignedPeaks: signed peak bork: %s covers %s to %s\n", signed_peak_bins_[j].str().c_str(), signed_peak_bins_[j].base_left().str().c_str(), signed_peak_bins_[j].base_right().str().c_str());
	    }
            fprintf(stderr,"Press...\n");
            getchar();
	    exit(-1);
	}
	end = signed_peak_bins_[i].base_right().layer_offset();
    }
}


void LiveHashTree::check_new_peaks(bhstvector &newpeaktuples)
{
    if (tree_debug)
    {
        bhstvector::iterator iter;
        for (iter=newpeaktuples.begin(); iter!=newpeaktuples.end(); iter++)
        {    
	    BinHashSigTuple bhst = *iter;
	    fprintf(stderr,"UpdateSignedPeaks: new peak: %s %s\n", bhst.bin().str().c_str(), bhst.hash().hex().c_str() );
        }
    }
}




bool LiveHashTree::CreateAndVerifyNode(bin_t pos, const Sha1Hash &hash, bool verified)
{
    // This adds hashes on client side
    if (tree_debug)
	fprintf(stderr,"OfferHash: %s %s\n", pos.str().c_str(), hash.hex().c_str() );

    // Find or create node
    Node *iter = root_;
    Node *parent = NULL;
    while (true)
    {
	if (tree_debug)
	{
	    if (iter == NULL)
	       fprintf(stderr,"OfferHash: iter NULL\n");
	    else
	        fprintf(stderr,"OfferHash: iter %s\n", iter->GetBin().str().c_str() );
	}

	if (iter == NULL)
	{
	    // Need to create some tree for it
	    if (parent == NULL)
	    {
		// No root
		root_ = new Node();
		root_->SetBin(pos);

		if (tree_debug)
		    fprintf(stderr,"OfferHash: new root %s %s\n", root_->GetBin().str().c_str(), hash.hex().c_str() );

		root_->SetHash(hash);
		root_->SetVerified(verified);
		return false;
	    }
	    else
	    {
		// Create left or right tree
		if (pos.toUInt() < parent->GetBin().toUInt())
		{
		    // Need node on left
		    Node *newleft = new Node();
		    newleft->SetBin(parent->GetBin().left());

		    if (tree_debug)
			fprintf(stderr,"OfferHash: create left %s\n", newleft->GetBin().str().c_str() );

		    newleft->SetParent(parent);

		    parent->SetChildren(newleft,parent->GetRight());
		    iter = newleft;
		}
		else
		{
		    // Need new node on right
		    Node *newright = new Node();
		    newright->SetBin(parent->GetBin().right());

		    if (tree_debug)
			fprintf(stderr,"OfferHash: create right %s\n", newright->GetBin().str().c_str() );

		    newright->SetParent(parent);

		    parent->SetChildren(parent->GetLeft(),newright);
		    iter = newright;
		}
	    }
	}
	else if (!iter->GetBin().contains(pos))
	{
	    // Offered pos not a child, error or create new root
	    Node *newroot = new Node();
	    newroot->SetBin(iter->GetBin().parent());

	    if (tree_debug)
		fprintf(stderr,"OfferHash: new root no cover %s\n", newroot->GetBin().str().c_str() );

	    if (pos.layer_offset() < iter->GetBin().layer_offset())
		newroot->SetChildren(NULL,iter);
	    else
		newroot->SetChildren(iter,NULL);
	    root_ = newroot;
	    iter->SetParent(newroot);
	    iter = newroot;
	}

	if (pos.toUInt() == iter->GetBin().toUInt())
	{
	    // Found it
	    if (tree_debug)
		fprintf(stderr,"OfferHash: found node %s\n", iter->GetBin().str().c_str() );
	    break;
	}
	else if (pos.toUInt() < iter->GetBin().toUInt())
	{
	    if (tree_debug)
		fprintf(stderr,"OfferHash: go left\n" );

	    parent = iter;
	    iter = iter->GetLeft();
	}
	else
	{
	    if (tree_debug)
		fprintf(stderr,"OfferHash: go right\n" );

	    parent = iter;
	    iter = iter->GetRight();
	}
    }


    if (iter == NULL)
    {
	if (tree_debug)
	    fprintf(stderr,"OfferHash: internal error, couldn't find or create node\n" );
	return false;
    }

    if (state_ == LHT_STATE_VER_AWAIT_PEAK)
    {
	if (tree_debug)
	    fprintf(stderr,"OfferHash: No peak yet, can't verify!\n" );
	return false;
    }


    //
    // From MmapHashTree::OfferHash
    //

    if (tree_debug)
        fprintf(stderr,"OfferHash: found node %s isverified %d\n",iter->GetBin().str().c_str(),iter->GetVerified() );

    bin_t peak = peak_for(pos);
    if (peak.is_none())
        return false;
    if (peak==pos)
    {
	// Diff from MmapHashTree: store peak here
	if (verified)
	{
	    if (tree_debug)
		fprintf(stderr,"OfferHash: setting peak %s %s\n",pos.str().c_str(),hash.hex().c_str() );

	    iter->SetHash(hash);
	    iter->SetVerified(verified);
	}
        return hash == iter->GetHash();
    }
    // AddLiveRightHashes
    /*if (!ack_out_.is_empty(pos.parent()))
    {
	fprintf(stderr,"OfferHash: think I have hash already %s\n",iter->GetBin().str().c_str() );

        return hash == iter->GetHash(); // have this hash already, even accptd data
    }*/

    // LESSHASH
    // Arno: if we already verified this hash against the root, don't replace
    if (iter->GetVerified())
        return hash == iter->GetHash();

    iter->SetHash(hash);

    if (tree_debug)
        fprintf(stderr,"OfferHash: setting hash %s %s\n",pos.str().c_str(),hash.hex().c_str() );

    if (!pos.is_base())
        return false; // who cares?

    Node *piter = iter;
    Sha1Hash uphash = hash;

    if (tree_debug)
	fprintf(stderr,"OfferHash: verifying %s\n", pos.str().c_str() );
    // Walk to the nearest proven hash
    // Arno, 2013-03-11: Can't use is_empty as we may have verified some hashes
    // under an old peak, but now that the tree has grown this doesn't indicate
    // verified status anymore.
    //
    // while ( piter->GetBin()!=peak && ack_out_.is_empty(piter->GetBin()) && !piter->GetVerified() ) {
    while ( piter->GetBin()!=peak && !piter->GetVerified() ) {
        piter->SetHash(uphash);
        piter = piter->GetParent();

        // Arno: Prevent poisoning the tree with bad values:
        // Left hand hashes should never be zero, and right
        // hand hash is only zero for the last packet, i.e.,
        // layer 0. Higher layers will never have 0 hashes
        // as SHA1(zero+zero) != zero (but b80de5...)
        //

        if (tree_debug)
            fprintf(stderr,"OfferHash: squirrel %s %p %p\n", piter->GetBin().str().c_str(), piter->GetLeft(), piter->GetRight() );

        if (piter->GetLeft() == NULL || piter->GetRight() == NULL)
        {
            if (tree_debug)
            {
        	if (piter->GetLeft() == NULL)
        	    fprintf(stderr,"OfferHash: Error! Missing left child of %s\n", piter->GetBin().str().c_str() );
        	if (piter->GetRight() == NULL)
        	    fprintf(stderr,"OfferHash: Error! Missing right child of %s\n", piter->GetBin().str().c_str() );
            }

            return false; // tree still incomplete
        }

        if (tree_debug)
            fprintf(stderr,"OfferHash: hashsquirrel %s %s %s\n", piter->GetBin().str().c_str(), piter->GetLeft()->GetHash().hex().c_str(), piter->GetRight()->GetHash().hex().c_str() );

        if (piter->GetLeft()->GetHash() == Sha1Hash::ZERO || piter->GetRight()->GetHash() == Sha1Hash::ZERO)
            break;
        uphash = Sha1Hash(piter->GetLeft()->GetHash(),piter->GetRight()->GetHash());
    }

    if (tree_debug)
    {
	fprintf(stderr,"OfferHash: while %d %d %d\n", piter->GetBin()!=peak, ack_out_.is_empty(piter->GetBin()),  !piter->GetVerified() );
	fprintf(stderr,"OfferHash: %s computed %s truth %s\n", piter->GetBin().str().c_str(), uphash.hex().c_str(), piter->GetHash().hex().c_str() );
    }

    bool success = (uphash==piter->GetHash());

    //TEMP
    if (!success)
    {
	fprintf(stderr,"OfferHash: !success data %s\n", pos.str().c_str() );
        getchar();
	exit(-1);
    }



    // LESSHASH
    if (success) {
	// Arno: The hash checks out. Mark all hashes on the uncle path as
	// being verified, so we don't have to go higher than them on a next
	// check.
	bin_t p = pos;
	piter = iter;
	piter->SetVerified(true);
	while (p.layer() != peak.layer()) {
	    p = p.parent().sibling();
	    if (piter->GetParent() == NULL)
		break;
	    if (piter->GetParent()->GetParent() == NULL)
		break;
	    if (piter->GetParent()->GetBin().is_left())
		piter = piter->GetParent()->GetParent()->GetRight();
	    else
		piter = piter->GetParent()->GetParent()->GetLeft();
            if (piter == NULL)
            {
                fprintf(stderr,"OfferHash: SetVerified %s has NULL node!!!\n", p.str().c_str() );
                return false;
            }
            else
            {
	        fprintf(stderr,"OfferHash: SetVerified %s\n", piter->GetBin().str().c_str() );
	        piter->SetVerified(true);
            }
	}
	// Also mark hashes on direct path to root as verified. Doesn't decrease
	// #checks, but does increase the number of verified hashes faster.
        // Arno, 2013-03-08: Only holds if hashes are permanent (i.e., not parents of peaks
        // while live streaming with Unified Merkle Trees.
	p = pos;
	piter = iter;
	while (p != peak) {
	    p = p.parent();
	    piter = piter->GetParent();
            if (piter == NULL)
            {
                fprintf(stderr,"OfferHash: SetVerified2 %s has NULL node!!!\n", p.str().c_str() );
            }
	    else if (piter->GetHash() == Sha1Hash::ZERO)
	    {
                if (tree_debug)
		    fprintf(stderr,"OfferHash: SetVerified2 %s ZERO!!!\n", piter->GetBin().str().c_str() );
	    }
	    else
	    {
		piter->SetVerified(true);
                if (tree_debug)
		    fprintf(stderr,"OfferHash: SetVerified2 %s\n", piter->GetBin().str().c_str() );
	    }
	}
    }
    return success;
}


/*
 * HashTree interface
 */

bool LiveHashTree::OfferHash(bin_t pos, const Sha1Hash& hash)
{
    bin_t peak = peak_for(pos);
    if (peak.is_none())
    {
        cand_peak_bin_ = pos;
        cand_peak_hash_ = hash;
        if (tree_debug)
	    fprintf(stderr,"OfferData: no peak\n");
        return false;
    }
    else
    {
	cand_peak_bin_ = bin_t::NONE;
        if (tree_debug)
	    fprintf(stderr,"OfferHash: %s has peak %s\n", pos.str().c_str(), peak.str().c_str() );
	return CreateAndVerifyNode(pos,hash,false);
    }
}


bool LiveHashTree::OfferData(bin_t pos, const char* data, size_t length)
{
    if (state_ == LHT_STATE_VER_AWAIT_PEAK)
    {
        if (tree_debug)
	    fprintf(stderr,"OfferData: await peak\n");
        return false;
    }
    if (!pos.is_base())
    {
        if (tree_debug)
	    fprintf(stderr,"OfferData: not base\n");
        return false;
    }
    if (length<chunk_size_ && pos!=bin_t(0,sizec_-1))
    {
        if (tree_debug)
	    fprintf(stderr,"OfferData: bad len %d\n", length);
        return false;
    }
    if (ack_out_.is_filled(pos))
    {
        if (tree_debug)
	    fprintf(stderr,"OfferData: already have\n");
        return true; // to set data_in_
    }
    bin_t peak = peak_for(pos);
    if (peak.is_none())
    {
        if (tree_debug)
	    fprintf(stderr,"OfferData: couldn't find peak\n");
        return false;
    }
    Sha1Hash data_hash(data,length);

    if (tree_debug)
        fprintf(stderr,"OfferData: %s hash %s\n", pos.str().c_str(), data_hash.hex().c_str() );

    if (!OfferHash(pos, data_hash)) {
        //printf("invalid hash for %s: %s\n",pos.str(bin_name_buf),data_hash.hex().c_str()); // paranoid
        //fprintf(stderr,"INVALID HASH FOR %lli layer %d\n", pos.toUInt(), pos.layer() );
        // Ric: TODO it's not necessarily a bug.. it happens if a pkt was lost!
        dprintf("%s hashtree check failed (bug TODO) %s\n",tintstr(),pos.str().c_str());
        return false;
    }

    //printf("g %lli %s\n",(uint64_t)pos,hash.hex().c_str());
    if (tree_debug)
        fprintf(stderr,"OfferData: set ack_out_ %s\n", pos.str().c_str() );

    ack_out_.set(pos);

#ifdef SIGNPEAKTODO
    // Arno,2011-10-03: appease g++
    if (storage_->Write(data,length,pos.base_offset()*chunk_size_) < 0)
        print_error("pwrite failed");
#endif
    complete_ += length;
    completec_++;

    return true;

}


int  LiveHashTree::peak_count() const
{
    return peak_count_; // TODOinline
}

bin_t LiveHashTree::peak(int i) const
{
    return peak_bins_[i]; // TODOinline
}

const Sha1Hash& LiveHashTree::peak_hash(int i) const
{
    return hash(peak(i)); // TODOinline
}

bin_t LiveHashTree::peak_for(bin_t pos) const
{
    for (int i=0; i<peak_count_; i++)
    {
	if (peak_bins_[i].contains(pos))
	    return peak_bins_[i];
    }
    return bin_t::NONE;
}

const Sha1Hash& LiveHashTree::hash(bin_t pos) const
{
    // This API may not be fastest with dynamic tree.
    Node *n = FindNode(pos);
    if (n == NULL)
	return Sha1Hash::ZERO;
    else
	return n->GetHash();
}

Node *LiveHashTree::FindNode(bin_t pos) const
{
    Node *iter = root_;
    while (true)
    {
	if (iter == NULL)
	    return NULL;
	else if (pos.toUInt() == iter->GetBin().toUInt())
	    return iter;
	else if (pos.toUInt() < iter->GetBin().toUInt())
	    iter = iter->GetLeft();
	else
	    iter = iter->GetRight();
    }
}


const Sha1Hash& LiveHashTree::root_hash() const
{
    if (root_ == NULL)
	return Sha1Hash::ZERO;
    else
	return root_->GetHash();
}

uint64_t LiveHashTree::size() const
{
    return size_;
}

uint64_t LiveHashTree::size_in_chunks() const
{
    return size_/chunk_size_; // TODOinline
}


uint64_t LiveHashTree::complete() const
{
    return complete_;
}

uint64_t LiveHashTree::chunks_complete() const
{
    return completec_;
}

uint64_t LiveHashTree::seq_complete(int64_t offset)
{
    return 0;
}
bool LiveHashTree::is_complete()
{
    return false;
}
binmap_t *LiveHashTree::ack_out ()
{
    return &ack_out_;
}

uint32_t LiveHashTree::chunk_size()
{
    return chunk_size_; // TODOinline
}


Storage *LiveHashTree::get_storage()
{
    return storage_;
}
void LiveHashTree::set_size(uint64_t)
{
}

int LiveHashTree::TESTGetFD()
{
    return 481;
}


void LiveHashTree::sane_tree()
{
    if (root_ == NULL)
	return;
    else
    {
	sane_node(root_,NULL);
    }
}


void LiveHashTree::sane_node(Node *n, Node *parent)
{
    //fprintf(stderr,"Sane: %s\n", n->GetBin().str().c_str() );
    assert(n->GetParent() == parent);
    if (n->GetLeft() != NULL)
    {
	assert(n->GetLeft()->GetParent() == n);
	sane_node(n->GetLeft(),n);
    }
    if (n->GetRight() != NULL)
    {
	assert(n->GetRight()->GetParent() == n);
	sane_node(n->GetRight(),n);
    }
}
