/*
 *  send_control.cpp
 *  congestion control logic for the swift protocol
 *
 *  Created by Victor Grishchenko on 12/10/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#include "swift.h"
#include <cassert>

using namespace swift;
using namespace std;

tint Channel::MIN_DEV = 50*TINT_MSEC;
tint Channel::MAX_SEND_INTERVAL = TINT_SEC*58;
tint Channel::LEDBAT_TARGET = TINT_MSEC*25;
float Channel::LEDBAT_GAIN = 1.0/LEDBAT_TARGET;
tint Channel::LEDBAT_DELAY_BIN = TINT_SEC*30;
tint Channel::MAX_POSSIBLE_RTT = TINT_SEC*10;
const char* Channel::SEND_CONTROL_MODES[] = {"keepalive", "pingpong",
    "slowstart", "standard_aimd", "ledbat", "closing"};


tint    Channel::NextSendTime () {
    TimeoutDataOut(); // precaution to know free cwnd
    switch (send_control_) {
        case KEEP_ALIVE_CONTROL: return KeepAliveNextSendTime();
        case PING_PONG_CONTROL:  return PingPongNextSendTime();
        case SLOW_START_CONTROL: return SlowStartNextSendTime();
        case AIMD_CONTROL:       return AimdNextSendTime();
        case LEDBAT_CONTROL:     return LedbatNextSendTime();
        case CLOSE_CONTROL:      return TINT_NEVER;
        default:                 fprintf(stderr,"send_control.cpp: unknown control %d\n", send_control_); return TINT_NEVER;
    }
}

tint    Channel::SwitchSendControl (send_control_t control_mode) {
    dprintf("%s #%u sendctrl switch %s->%s\n",tintstr(),id(),
            SEND_CONTROL_MODES[send_control_],SEND_CONTROL_MODES[control_mode]);
    switch (control_mode) {
        case KEEP_ALIVE_CONTROL:
            send_interval_ = rtt_avg_; //max(TINT_SEC/10,rtt_avg_);
            dev_avg_ = max(TINT_SEC,rtt_avg_);
            data_out_cap_ = bin_t::ALL;
            cwnd_ = 1;
            break;
        case PING_PONG_CONTROL:
            dev_avg_ = max(TINT_SEC,rtt_avg_);
            data_out_cap_ = bin_t::ALL;
            cwnd_ = 1;
            break;
        case SLOW_START_CONTROL:
            cwnd_ = 1;
            break;
        case AIMD_CONTROL:
            break;
        case LEDBAT_CONTROL:
            break;
        case CLOSE_CONTROL:
            break;
        default:
            assert(false);
    }
    send_control_ = control_mode;
    return NextSendTime();
}

tint    Channel::KeepAliveNextSendTime () {
    if (sent_since_recv_>=3 && last_recv_time_<NOW-3*MAX_SEND_INTERVAL)
        return SwitchSendControl(CLOSE_CONTROL);
    if (ack_rcvd_recent_)
        return SwitchSendControl(SLOW_START_CONTROL);
    if (data_in_.time!=TINT_NEVER)
        return NOW;
    /* Gertjan fix 5f51e5451e3785a74c058d9651b2d132c5a94557
    "Do not increase send interval in keep-alive mode when previous Reschedule
    was already in the future.
    The problem this solves is that when we keep on receiving packets in keep-alive
    mode, the next packet will be pushed further and further into the future, which is
    not what we want. The scheduled time for the next packet should be unchanged
    on reception."
    */
    if (!reverse_pex_out_.empty())
        return reverse_pex_out_.front().time;
    if (NOW < next_send_time_)
        return next_send_time_;

    // Arno: Fix that doesn't do exponential growth always, only after sends
    // without following recvs

    // fprintf(stderr,"KeepAliveNextSendTime: gotka %d sentka %d ss %d si %lli\n", lastrecvwaskeepalive_, lastsendwaskeepalive_, sent_since_recv_, send_interval_);

    if (lastrecvwaskeepalive_ && lastsendwaskeepalive_)
    {
    	send_interval_ <<= 1;
    }
    else if (lastrecvwaskeepalive_ || lastsendwaskeepalive_)
    {
        // Arno, 2011-11-29: we like eachother again, start fresh
    	// Arno, 2012-01-25: Unless we're talking to a dead peer.
        if (sent_since_recv_ < 4) {
    	    send_interval_ = rtt_avg_;
        } else 
            send_interval_ <<= 1;
    }
    else if (sent_since_recv_ <= 1) 
    {
    	send_interval_ = rtt_avg_;
    }
    else if (sent_since_recv_ > 1)
    {
        send_interval_ <<= 1;
    }
    if (send_interval_>MAX_SEND_INTERVAL)
        send_interval_ = MAX_SEND_INTERVAL;
    return last_send_time_ + send_interval_;
}

tint    Channel::PingPongNextSendTime () { // FIXME INFINITE LOOP
    //fprintf(stderr,"PING: dgrams %d ackrec %d dataintime %lli lastrecv %lli lastsend %lli\n", dgrams_sent_, ack_rcvd_recent_, data_in_.time, last_recv_time_, last_send_time_);
    if (dgrams_sent_>=10)
        return SwitchSendControl(KEEP_ALIVE_CONTROL);
    if (ack_rcvd_recent_)
        return SwitchSendControl(SLOW_START_CONTROL);
    if (data_in_.time!=TINT_NEVER)
        return NOW;
    if (last_recv_time_>last_send_time_)
        return NOW;
    if (!last_send_time_)
        return NOW;
    return last_send_time_ + ack_timeout(); // timeout
}

tint    Channel::CwndRateNextSendTime () {
    if (data_in_.time!=TINT_NEVER)
        return NOW; // TODO: delayed ACKs
    //if (last_recv_time_<NOW-rtt_avg_*4)
    //    return SwitchSendControl(KEEP_ALIVE_CONTROL);
    send_interval_ = rtt_avg_/cwnd_;
    if (send_interval_>max(rtt_avg_,TINT_SEC)*4)
        return SwitchSendControl(KEEP_ALIVE_CONTROL);
    if (data_out_.size()<cwnd_) {
        dprintf("%s #%u sendctrl next in %lldus (cwnd %.2f, data_out %u)\n",
	    tintstr(),id_,send_interval_,cwnd_,data_out_.size());
        return last_data_out_time_ + send_interval_;
    } else {
        assert(data_out_.front().time!=TINT_NEVER);
        return data_out_.front().time + ack_timeout();
    }
}

void    Channel::BackOffOnLosses (float ratio) {
    ack_rcvd_recent_ = 0;
    ack_not_rcvd_recent_ =  0;
    if (last_loss_time_<NOW-rtt_avg_) {
        cwnd_ *= ratio;
        last_loss_time_ = NOW;
        dprintf("%s #%u sendctrl backoff %3.2f\n",tintstr(),id_,cwnd_);
    }
}

tint    Channel::SlowStartNextSendTime () {
    if (ack_not_rcvd_recent_) {
        BackOffOnLosses();
        return SwitchSendControl(LEDBAT_CONTROL);//AIMD_CONTROL);
    }
    if (rtt_avg_/cwnd_<TINT_SEC/10)
        return SwitchSendControl(LEDBAT_CONTROL);//AIMD_CONTROL);
    cwnd_+=ack_rcvd_recent_;
    ack_rcvd_recent_=0;
    return CwndRateNextSendTime();
}

tint    Channel::AimdNextSendTime () {
    if (ack_not_rcvd_recent_)
        BackOffOnLosses();
    if (ack_rcvd_recent_) {
        if (cwnd_>1)
            cwnd_ += ack_rcvd_recent_/cwnd_;
        else
            cwnd_ *= 2;
    }
    ack_rcvd_recent_=0;
    return CwndRateNextSendTime();
}

tint Channel::LedbatNextSendTime () {
    float oldcwnd = cwnd_;

    tint owd_cur(TINT_NEVER), owd_min(TINT_NEVER);
    for(int i=0; i<4; i++) {
        if (owd_min>owd_min_bins_[i])
            owd_min = owd_min_bins_[i];
        if (owd_cur>owd_current_[i])
            owd_cur = owd_current_[i];
    }
    if (ack_not_rcvd_recent_)
        BackOffOnLosses(0.8);
    ack_rcvd_recent_ = 0;
    tint queueing_delay = owd_cur - owd_min;
    tint off_target = LEDBAT_TARGET - queueing_delay;
    cwnd_ += LEDBAT_GAIN * off_target / cwnd_;
    if (cwnd_<1) 
        cwnd_ = 1;
    if (owd_cur==TINT_NEVER || owd_min==TINT_NEVER) 
        cwnd_ = 1;

    //Arno, 2012-02-02: Somehow LEDBAT gets stuck at cwnd_ == 1 sometimes
    // This hack appears to work to get it back on the right track quickly.
    if (oldcwnd == 1 && cwnd_ == 1)
       cwnd_count1_++;
    else
       cwnd_count1_ = 0;
    if (cwnd_count1_ > 10)
    {
        dprintf("%s #%u sendctrl ledbat stuck, reset\n",tintstr(),id() );
        cwnd_count1_ = 0;
        for(int i=0; i<4; i++) {
            owd_min_bins_[i] = TINT_NEVER;
            owd_current_[i] = TINT_NEVER;
        }
    }

    dprintf("%s #%u sendctrl ledbat %lld-%lld => %3.2f\n",
            tintstr(),id_,owd_cur,owd_min,cwnd_);
    return CwndRateNextSendTime();
}



