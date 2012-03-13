/*
 *  congctrltest.cpp
 *  p2tp
 *
 *  Created by Victor Grishchenko on 7/13/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include <stdint.h>
#include <queue>
#include <gtest/gtest.h>
#include <glog/logging.h>
#include "p2tp.h"

using namespace std;
using namespace p2tp;

class SimPeer;

struct SimPacket {
    SimPacket(int from, int to, const SimPacket* toack, bool data) ;
    int peerfrom, peerto;
    tint datatime;
    tint acktime;
    tint arrivaltime;
};

tint now = 0;

/** very simplified; uplink is the bottleneck */
class SimPeer {
public:
    SimPeer (tint tt, tint lt, int qlen) : travtime(tt), latency(lt), queue_length(qlen) {}
    int queue_length;
    int travtime;
    tint freetime;
    tint latency;
    int unackd;
    int rcvd, sent;
    queue<SimPacket> packet_queue;
    queue<SimPacket> dropped_queue;
    CongestionControl congc;
    
    void send(SimPacket pck) {
        if (packet_queue.size()==queue_length) {
            dropped_queue.push(pck);
            return;
        }
        tint start = max(now,freetime);
        tint done = pck.datatime ? start+travtime : start;
        freetime = done;
        pck.arrivaltime = done + latency;
        packet_queue.push(pck);
    }
    
    SimPacket recv () {
        assert(!packet_queue.empty());
        SimPacket ret = packet_queue.front();
        packet_queue.pop();
        return ret;
    }
    
    tint next_recv_time () const {
        return packet_queue.empty() ? NEVER : packet_queue.front().arrivaltime;
    }
    
    void    turn () {
        SimPacket rp = recv();
        SimPacket reply;
        now = rp.arrivaltime;
        if (rp.acktime) {
            congc.RttSample(rp.arrivaltime-rp.acktime);
            congc.OnCongestionEvent(CongestionControl::ACK_EV);
            unackd--;
            rcvd++;
        }
        if (rp.datatime) {
            congc.OnCongestionEvent(CongestionControl::DATA_EV);
            reply.acktime = reply.datatime;
        }
        if (!dropped_queue.empty() && dropped_queue.top().datatime<now+THR)
            congc.OnCongestionEvent(CongestionControl::LOSS_EV);
        if (congc.cwnd()>unackd) {
            unackd++;
            reply.datatime = now;
            sent++;
        }
        rp.from->send(reply);
    }
};

TEST(P2TP, TailDropTest) {
    // two peers exchange packets over 100ms link with tail-drop discipline
    // bw 1Mbits => travel time of 1KB is ~10ms
    SimPeer a(10*MSEC,100*MSEC,20), b(10*MSEC,100*MSEC,20);
    a.send(SimPacket(&b,now,0,0));
    while (now<60*60*SEC) 
        if (a.next_recv_time()<b.next_recv_time())
            a.turn();
        else
            b.turn();
}

int main (int argc, char** argv) {
	bin::init();
	bins::init();
	google::InitGoogleLogging(argv[0]);
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
	
}
