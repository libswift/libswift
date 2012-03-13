/*
 *  simple_selector.cpp
 *  swift
 *
 *  Created by Victor Grishchenko on 10/6/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#include <queue>
#include "swift.h"

using namespace swift;

class SimpleSelector : public PeerSelector {
    typedef std::pair<Address,Sha1Hash> memo_t;
    typedef std::deque<memo_t>  peer_queue_t;
    peer_queue_t    peers;
public:
    SimpleSelector () {
    }
    void AddPeer (const Address& addr, const Sha1Hash& root) {
        peers.push_front(memo_t(addr,root)); //,root.fingerprint() !!!
    }
    Address GetPeer (const Sha1Hash& for_root) {
        //uint32_t fp = for_root.fingerprint();
        for(peer_queue_t::iterator i=peers.begin(); i!=peers.end(); i++)
            if (i->second==for_root) {
                i->second = Sha1Hash::ZERO; // horror TODO rewrite
                sockaddr_in ret = i->first;
                while (peers.begin()->second==Sha1Hash::ZERO)
                    peers.pop_front();
                return ret;
            }
        return Address();
    }
};

