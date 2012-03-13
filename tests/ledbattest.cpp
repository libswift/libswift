/*
 *  ledbattest.cpp
 *
 *  BROKEN: Arno: must be rewritten to libevent
 *
 *  Created by Victor Grishchenko on 3/22/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <deque>
#include "swift.h"
#include <gtest/gtest.h>

using namespace swift;
using namespace std;

/**
  TODO
  * losses
  * smooth rate
  * seq 12345 stop
  * busy pipe => negative cwnd
*/

TEST(Datagram,LedbatTest) {

    int MAX_REORDERING = 3;
    tint TARGET = 25*TINT_MSEC;
    float GAIN = 1.0/TARGET;
    int seq_off = 0;
    float cwnd = 1;
    tint DELAY_BIN = TINT_SEC*30;
    tint min_delay = TINT_NEVER;
    tint rtt_avg = TINT_NEVER>>4, dev_avg = TINT_NEVER>>4;
    tint last_bin_time = 0;
    tint last_drop_time = 0;
    int delay_bin = 0;
    deque<tint> history, delay_history;
    tint min_delay_bins[4] = {TINT_NEVER,TINT_NEVER,
        TINT_NEVER,TINT_NEVER};
    tint cur_delays[4] = {TINT_NEVER,TINT_NEVER,
        TINT_NEVER,TINT_NEVER};
    tint last_sec = 0;
    int sec_ackd = 0;

    evutil_socket_t send_sock = Datagram::Bind(10001); // bind sending socket
    evutil_socket_t ack_sock = Datagram::Bind(10002);  // bind receiving socket
    struct sockaddr_in send_to, ack_to;
    send_to.sin_family = AF_INET;
    send_to.sin_port = htons(10002);
    send_to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ack_to.sin_family = AF_INET;
    ack_to.sin_port = htons(10001);
    ack_to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    uint8_t* garbage = (uint8_t*) malloc(1024);
    evutil_socket_t socks[2] = {send_sock,ack_sock};
    evutil_socket_t sock2read;
    tint wait_time = 100*TINT_MSEC;

    while (sock2read = Datagram::Wait(2,socks,wait_time)) {
        tint now = Datagram::Time();
        if (sock2read==ack_sock) {
            Datagram data(ack_sock); // send an acknowledgement
            data.Recv();
            int seq = data.Pull32();
            Datagram ack(ack_sock,ack_to);
            ack.Push32(seq);
            ack.Push64(now);
            if (4+8!=ack.Send())
                fprintf(stderr,"short write\n");
            fprintf(stderr,"%lli rcvd%i\n",now/TINT_SEC,seq);
            //cc->OnDataRecv(bin64_t(0,seq));
            // TODO: peer cwnd !!!
            continue;
        }
        if (sock2read==send_sock) {        // process an acknowledgement
            Datagram ack(send_sock);
            ack.Recv();
            int seq = ack.Pull32();
            tint arrival_time = ack.Pull64();
            seq -= seq_off;
            if (seq<0)
                continue;
            if (seq>=history.size())
                continue;
            if (history[seq]==0)
                continue;
            tint send_time = history[seq];
            history[seq] = 0;
            if (seq>MAX_REORDERING*2) { //loss
                if (last_drop_time<now-rtt_avg) {
                    cwnd /= 2;
                    last_drop_time = now;
                }
                fprintf(stderr,"got %i. LOSS, cwnd drop: %f\n",seq,cwnd);
                for(int i=0; i<MAX_REORDERING*2 && history.size(); i++) {
                    seq_off++;
                    history.pop_front();
                }
                continue;
            }
            tint delay = arrival_time - send_time;
            if (seq==0 && seq_off==0) { // FIXME
                rtt_avg = now - send_time;
                dev_avg = rtt_avg;
            }
            if (send_time/DELAY_BIN != last_bin_time) {
                last_bin_time = send_time/DELAY_BIN;
                delay_bin = (delay_bin+1) % 4;
                min_delay_bins[delay_bin] = TINT_NEVER;
                min_delay = TINT_NEVER;
                for(int i=0;i<4;i++)
                    if (min_delay_bins[i]<min_delay)
                        min_delay = min_delay_bins[i];
            }
            if (min_delay_bins[delay_bin] > delay)
                min_delay_bins[delay_bin] = delay;
            if (delay < min_delay)
                min_delay = delay;
            cur_delays[(seq_off+seq)%4] = delay;
            tint current_delay = TINT_NEVER;
            for(int i=0; i<4; i++)
                if (current_delay > cur_delays[i])
                    current_delay = cur_delays[i];  // FIXME avg
            tint queueing_delay = current_delay - min_delay;
            // adjust cwnd
            tint off_target = TARGET - queueing_delay;
            //cerr<<"\t"<<cwnd<<"+="<<GAIN<<"*"<<off_target<<"/"<<cwnd<<endl;
            cwnd += GAIN * off_target / cwnd;
            fprintf(stderr,"ackd cwnd%f cur%lli min%lli seq%i off%i\n",
                    cwnd,current_delay,min_delay,seq_off+seq,seq);

            if (now/TINT_SEC!=last_sec/TINT_SEC) {
                fprintf(stderr,"%i KB/sec\n",sec_ackd);
                sec_ackd = 0;
                last_sec = now; // FIXME
            } else
                sec_ackd++;

        } // if
        while (history[0]==0 && history.size()) {
            history.pop_front();
            seq_off++;
        }
        if (history.size() && history[0]<now-rtt_avg-5*dev_avg) {
            if (last_drop_time<now-rtt_avg) {
                cwnd /= 2;
                last_drop_time = now;
            }
            fprintf(stderr,"TIMEOUT LOSS, cwnd drop: %f\n",cwnd);
            seq_off++;
            history.pop_front();
        }
        // fill cwnd
        if (history.size()<cwnd) {
            int sendseq = history.size() + seq_off;
            Datagram send(send_sock,send_to);
            send.Push32(sendseq);
            send.Push(garbage,1024);
            history.push_back(now);
            fprintf(stderr,"sent%i\n",sendseq);
            if (4+1024!=send.Send())
                fprintf(stderr,"short data write\n");
        }
        if (cwnd<1)
            cwnd = 1;
        if (history.size()<cwnd)
            wait_time = rtt_avg/cwnd;
        else
            wait_time = 100*TINT_MSEC;
    } // while
}

int main (int argc, char** argv) {
    printf("Warning: use the script to set up dummynet!\n");

    swift::LibraryInit();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();

}
