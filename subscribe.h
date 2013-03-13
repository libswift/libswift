/*
 *  swift.h
 *  the main header file for libswift, normally you should only read this one
 *
 *  Created by Victor Grishchenko on 3/6/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
/*

  The swift protocol

  Messages

  HANDSHAKE    00, channelid
  Communicates the channel id of the sender. The
  initial handshake packet also has the root hash
  (a HASH message).

  DATA        01, bin_32, buffer
  1K of data.

  ACK        02, bin_32, timestamp_32
  HAVE       03, bin_32
  Confirms successfull delivery of data. Used for
  congestion control, as well.

  HINT        08, bin_32
  Practical value of "hints" is to avoid overlap, mostly.
  Hints might be lost in the network or ignored.
  Peer might send out data without a hint.
  Hint which was not responded (by DATA) in some RTTs
  is considered to be ignored.
  As peers cant pick randomly kilobyte here and there,
  they send out "long hints" for non-base bins.

  HASH        04, bin_32, sha1hash
  SHA1 hash tree hashes for data verification. The
  connection to a fresh peer starts with bootstrapping
  him with peak hashes. Later, before sending out
  any data, a peer sends the necessary uncle hashes.

  PEX+/PEX-    05/06, ipv4 addr, port
  Peer exchange messages; reports all connected and
  disconected peers. Might has special meaning (as
  in the case with swarm supervisors).

*/
#ifndef SUBSCRIBE_H
#define SUBSCRIBE_H

struct CloseEvent
{
    Sha1Hash swarmid_;
    Address  peeraddr_;
    uint64_t raw_bytes_[2], bytes_[2];
    CloseEvent(const Sha1Hash &swarmid, Address &peeraddr, uint64_t raw_bytes_down, uint64_t raw_bytes_up, uint64_t bytes_down, uint64_t bytes_up )
    : swarmid_(swarmid), peeraddr_(peeraddr)
    {
	raw_bytes_[DDIR_DOWNLOAD] = raw_bytes_down;
	raw_bytes_[DDIR_UPLOAD] = raw_bytes_up;
	bytes_[DDIR_DOWNLOAD] = bytes_down;
	bytes_[DDIR_UPLOAD] = bytes_up;
    }
    Sha1Hash &swarmid() { return swarmid_; }
    Address &peeraddr() { return peeraddr_; }
    uint64_t raw_bytes(data_direction_t ddir) { return raw_bytes_[ddir]; }
    uint64_t bytes(data_direction_t ddir) { return bytes_[ddir]; }
};

typedef std::vector<CloseEvent> swevent_list_t;

#endif
