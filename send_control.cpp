/*
 *  send_control.cpp
 *  congestion control logic for the swift protocol
 *
 *  Created by Victor Grishchenko on 12/10/09.
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#include "swift.h"
#include <cassert>

using namespace swift;
using namespace std;

tint Channel::MIN_DEV = 50*TINT_MSEC;
tint Channel::MAX_SEND_INTERVAL = TINT_SEC*58;
//const uint32_t Channel::LEDBAT_BASE_HISTORY = 10;
uint32_t Channel::LEDBAT_ROLLOVER = TINT_SEC*30;
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
    dprintf("%s #%" PRIu32 " sendctrl switch %s->%s\n",tintstr(),id(),
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
            break;
    }
    send_control_ = control_mode;
    return NextSendTime();
}

tint    Channel::KeepAliveNextSendTime () {
    if (sent_since_recv_>=3 && last_recv_time_<NOW-3*MAX_SEND_INTERVAL) {
        lprintf("\t\t==== Switch to Close Control ==== \n");
        return SwitchSendControl(CLOSE_CONTROL);
    }
    if (ack_rcvd_recent_) {
        lprintf("\t\t==== Switch to Slow Start Control ==== \n");
        return SwitchSendControl(SLOW_START_CONTROL);
    }
    if (data_in_.time!=TINT_NEVER)
        return NOW;

    if (live_have_no_hint_)
    {
	live_have_no_hint_ = false;
	return NOW;
    }
    /* Gertjan fix 5f51e5451e3785a74c058d9651b2d132c5a94557
    "Do not increase send interval in keep-alive mode when previous Reschedule
    was already in the future.
    The problem this solves is that when we keep on receiving packets in keep-alive
    mode, the next packet will be pushed further and further into the future, which is
    not what we want. The scheduled time for the next packet should be unchanged
    on reception."
    ----------------
    Ric: check if we still needed. Now I perform the check for previously scheduled
    events in reschedule(). Commented
    */
    if (!reverse_pex_out_.empty())
        return reverse_pex_out_.front().time;
    //if (NOW < next_send_time_)
    //    return next_send_time_;

    // Arno: Fix that doesn't do exponential growth always, only after sends
    // without following recvs

    //dprintf("KeepAliveNextSendTime: gotka %d sentka %d ss %d si %" PRIi64 " rtt %" PRIi64 "\n", lastrecvwaskeepalive_, lastsendwaskeepalive_, sent_since_recv_, send_interval_, rtt_avg_ );

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
    //fprintf(stderr,"PING: dgrams %d ackrec %d dataintime %" PRIi64 " lastrecv %" PRIi64 " lastsend %" PRIi64 "\n", dgrams_sent_, ack_rcvd_recent_, data_in_.time, last_recv_time_, last_send_time_);
    if (dgrams_sent_>=10) {
        lprintf("\t\t==== Switch to Keep Alive Control (dgrams_sent_>=10) ==== \n");
        return SwitchSendControl(KEEP_ALIVE_CONTROL);
    }
    if (ack_rcvd_recent_) {
        lprintf("\t\t==== Switch to Slow Start Control ==== \n");
        return SwitchSendControl(SLOW_START_CONTROL);
    }
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
    if (last_recv_time_<NOW-rtt_avg_*4) {
        lprintf("\t\t==== Switch to Keep Alive Control (last_recv_time_<NOW-rtt_avg_*4) ==== \n");
        return SwitchSendControl(KEEP_ALIVE_CONTROL);
    }
    send_interval_ = rtt_avg_/cwnd_;
    if (send_interval_>max(rtt_avg_,TINT_SEC)*4) {
        lprintf("\t\t==== Switch to Keep Alive Control (send_interval_>max(rtt_avg_,TINT_SEC)*4) ==== \n");
        return SwitchSendControl(KEEP_ALIVE_CONTROL);
    }
    if (data_out_size_<cwnd_) {
        dprintf("%s #%" PRIu32 " sendctrl send interval %" PRIi64 "us (cwnd %.2f, data_out %" PRIu32 ")\n",
                tintstr(),id_,send_interval_,cwnd_,data_out_size_);
        return last_data_out_time_ + send_interval_;
    } else {
        dprintf("%s #%" PRIu32 " sendctrl avoid sending (cwnd %.2f, data_out %" PRIu32 ")\n",
                tintstr(),id_,cwnd_,data_out_size_);
        assert(data_out_.front().time!=TINT_NEVER);
        return data_out_.front().time + ack_timeout();
    }
}

void    Channel::BackOffOnLosses (float ratio) {
    //ack_rcvd_recent_ = 0;
    ack_not_rcvd_recent_ =  0;
    if (last_loss_time_<NOW-rtt_avg_) {
        cwnd_ *= ratio;
        last_loss_time_ = NOW;
        dprintf("%s #%" PRIu32 " sendctrl backoff %3.2f\n",tintstr(),id_,cwnd_);
    }
}

tint    Channel::SlowStartNextSendTime () {
    if (ack_not_rcvd_recent_) {
        BackOffOnLosses();
        lprintf("\t\t==== Switch to LEDBAT Control (1) ==== \n");
        return SwitchSendControl(LEDBAT_CONTROL);//AIMD_CONTROL);
    }
    if (rtt_avg_/cwnd_<TINT_SEC/10) {
        lprintf("\t\t==== Switch to LEDBAT Control (2) ==== \n");
        return SwitchSendControl(LEDBAT_CONTROL);//AIMD_CONTROL);
    }
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
    //float oldcwnd = cwnd_;

    if (ack_rcvd_recent_) {

        //tint owd_cur(TINT_NEVER), owd_min(TINT_NEVER);
        // reset the min value
        owd_min_ = TINT_NEVER;

        // Ric: TODO for the moment we only use one sample!!
        for(int i=0; i<10; i++) {
            if (owd_min_>owd_min_bins_[i])
                owd_min_ = owd_min_bins_[i];
        }

        // We may apply a filter over the elements.. as suggested in the rfc
        ttqueue::iterator it = owd_current_.begin();
        int32_t count = 0;
        tint total = 0;
        tint timeout = NOW - rtt_avg_;
        // use the acks received during the last rtt, or at least 4 values
        while (it != owd_current_.end() && (it->second > timeout || count < 4) ) {
            total += it->first;
            count++;
            it++;
        }
        owd_cur_ = total/count;

        dprintf("%s #%" PRIu32 " sendctrl using %" PRIi32 " samples from the last rtt value [%" PRIi64 "], current owd: %" PRIi64 "\n",
                tintstr(),id_,count, rtt_avg_, owd_cur_);

        if (ack_not_rcvd_recent_)
            BackOffOnLosses(0.8);

        ack_rcvd_recent_ = 0;

        tint queueing_delay = owd_cur_ - owd_min_;
        tint off_target = LEDBAT_TARGET - queueing_delay;
        cwnd_ += LEDBAT_GAIN * off_target / cwnd_;
        if (cwnd_<1)
            cwnd_ = 1;
        if (owd_cur_==TINT_NEVER || owd_min_==TINT_NEVER)
            cwnd_ = 1;
    }

    /*Arno, 2012-02-02: Somehow LEDBAT gets stuck at cwnd_ == 1 sometimes
    // This hack appears to work to get it back on the right track quickly.
    if (oldcwnd == 1 && cwnd_ == 1)
       cwnd_count1_++;
    else
       cwnd_count1_ = 0;
    if (cwnd_count1_ > 10)
    {
        dprintf("%s #%" PRIu32 " sendctrl ledbat stuck, reset\n",tintstr(),id() );
        cwnd_count1_ = 0;
        for(int i=0; i<4; i++) {
            owd_min_bins_[i] = TINT_NEVER;
            owd_current_[i] = TINT_NEVER;
        }
    }*/


    return CwndRateNextSendTime();
}



