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
  Confirms successfull delivery of data. Used for congestion control, as well.

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
#ifndef SWIFT_H
#define SWIFT_H

#include <deque>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <string>
#include <math.h>

#include "compat.h"
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/buffer.h>
#include "bin.h"
#include "binmap.h"
#include "hashtree.h"
#include "avgspeed.h"
// Arno, 2012-05-21: MacOS X has an Availability.h :-(
#include "avail.h"


namespace swift {

#define SWIFT_MAX_UDP_OVER_ETH_PAYLOAD		(1500-20-8)
// Arno: Maximum size of non-DATA messages in a UDP packet we send.
#define SWIFT_MAX_NONDATA_DGRAM_SIZE 		(SWIFT_MAX_UDP_OVER_ETH_PAYLOAD-SWIFT_DEFAULT_CHUNK_SIZE-1-4)
// Arno: Maximum size of a UDP packet we send. Note: depends on CHUNKSIZE 8192
#define SWIFT_MAX_SEND_DGRAM_SIZE			(SWIFT_MAX_NONDATA_DGRAM_SIZE+1+4+8192)
// Arno: Maximum size of a UDP packet we are willing to accept. Note: depends on CHUNKSIZE 8192
#define SWIFT_MAX_RECV_DGRAM_SIZE			(SWIFT_MAX_SEND_DGRAM_SIZE*2)

#define layer2bytes(ln,cs)	(uint64_t)( ((double)cs)*pow(2.0,(double)ln))
#define bytes2layer(bn,cs)  (int)log2(  ((double)bn)/((double)cs) )

// Arno, 2011-12-22: Enable Riccardo's VodPiecePicker
#define ENABLE_VOD_PIECEPICKER		1

#define SWIFT_URI_SCHEME			"tswift"


/** IPv4 address, just a nice wrapping around struct sockaddr_in. */
    struct Address {
	struct sockaddr_in  addr;
	static uint32_t LOCALHOST;
	void set_port (uint16_t port) {
	    addr.sin_port = htons(port);
	}
	void set_port (const char* port_str) {
	    int p;
	    if (sscanf(port_str,"%i",&p))
		set_port(p);
	}
	void set_ipv4 (uint32_t ipv4) {
	    addr.sin_addr.s_addr = htonl(ipv4);
	}
	void set_ipv4 (const char* ipv4_str) ;
	//{    inet_aton(ipv4_str,&(addr.sin_addr));    }
	void clear () {
	    memset(&addr,0,sizeof(struct sockaddr_in));
	    addr.sin_family = AF_INET;
	}
	Address() {
	    clear();
	}
	Address(const char* ip, uint16_t port)  {
	    clear();
	    set_ipv4(ip);
	    set_port(port);
	}
	Address(const char* ip_port);
	Address(uint16_t port) {
	    clear();
	    set_ipv4((uint32_t)INADDR_ANY);
	    set_port(port);
	}
	Address(uint32_t ipv4addr, uint16_t port) {
	    clear();
	    set_ipv4(ipv4addr);
	    set_port(port);
	}
	Address(const struct sockaddr_in& address) : addr(address) {}
	uint32_t ipv4 () const { return ntohl(addr.sin_addr.s_addr); }
	uint16_t port () const { return ntohs(addr.sin_port); }
	operator sockaddr_in () const {return addr;}
	bool operator == (const Address& b) const {
	    return addr.sin_family==b.addr.sin_family &&
		addr.sin_port==b.addr.sin_port &&
		addr.sin_addr.s_addr==b.addr.sin_addr.s_addr;
	}
	const char* str () const {
		// Arno, 2011-10-04: not thread safe, replace.
	    static char rs[4][32];
	    static int i;
	    i = (i+1) & 3;
	    sprintf(rs[i],"%i.%i.%i.%i:%i",ipv4()>>24,(ipv4()>>16)&0xff,
		    (ipv4()>>8)&0xff,ipv4()&0xff,port());
	    return rs[i];
	}
	const char* ipv4str () const {
		// Arno, 2011-10-04: not thread safe, replace.
	    static char rs[4][32];
	    static int i;
	    i = (i+1) & 3;
	    sprintf(rs[i],"%i.%i.%i.%i",ipv4()>>24,(ipv4()>>16)&0xff,
		    (ipv4()>>8)&0xff,ipv4()&0xff);
	    return rs[i];
	}
	bool operator != (const Address& b) const { return !(*this==b); }
	bool is_private() const {
		// TODO IPv6
		uint32_t no = ipv4(); uint8_t no0 = no>>24,no1 = (no>>16)&0xff;
		if (no0 == 10) return true;
		else if (no0 == 172 && no1 >= 16 && no1 <= 31) return true;
		else if (no0 == 192 && no1 == 168) return true;
		else return false;
	}
    };

// Arno, 2011-10-03: Use libevent callback functions, no on_error?
#define sockcb_t		event_callback_fn
    struct sckrwecb_t {
	sckrwecb_t (evutil_socket_t s=0, sockcb_t mr=NULL, sockcb_t mw=NULL,
		    sockcb_t oe=NULL) :
	    sock(s), may_read(mr), may_write(mw), on_error(oe) {}
	evutil_socket_t sock;
	sockcb_t   may_read;
	sockcb_t   may_write;
	sockcb_t   on_error;
    };

    struct now_t  {
	static tint now;
    };

#define NOW now_t::now

    /** tintbin is basically a pair<tint,bin64_t> plus some nice operators.
        Most frequently used in different queues (acknowledgements, requests,
        etc). */
    struct tintbin {
        tint    time;
        bin_t bin;
        tintbin(const tintbin& b) : time(b.time), bin(b.bin) {}
        tintbin() : time(TINT_NEVER), bin(bin_t::NONE) {}
        tintbin(tint time_, bin_t bin_) : time(time_), bin(bin_) {}
        tintbin(bin_t bin_) : time(NOW), bin(bin_) {}
        bool operator < (const tintbin& b) const
			{ return time > b.time; }
        bool operator == (const tintbin& b) const
			{ return time==b.time && bin==b.bin; }
        bool operator != (const tintbin& b) const
			{ return !(*this==b); }
    };

    typedef std::deque<tintbin> tbqueue;
    typedef std::deque<bin_t> binqueue;
    typedef Address   Address;

    /** A heap (priority queue) for timestamped bin numbers (tintbins). */
    class tbheap {
        tbqueue data_;
    public:
        int size () const { return data_.size(); }
        bool is_empty () const { return data_.empty(); }
        tintbin         pop() {
            tintbin ret = data_.front();
            std::pop_heap(data_.begin(),data_.end());
            data_.pop_back();
            return ret;
        }
        void            push(const tintbin& tb) {
            data_.push_back(tb);
            push_heap(data_.begin(),data_.end());
        }
        const tintbin&  peek() const {
            return data_.front();
        }
    };

    typedef std::pair<std::string,std::string> stringpair;
    typedef std::map<std::string,std::string>  parseduri_t;
    bool ParseURI(std::string uri,parseduri_t &map);

    /** swift protocol message types; these are used on the wire. */
    typedef enum {
        SWIFT_HANDSHAKE = 0,
        SWIFT_DATA = 1,
        SWIFT_ACK = 2,
        SWIFT_HAVE = 3,
        SWIFT_HASH = 4,
        SWIFT_PEX_ADD = 5,
        SWIFT_PEX_REQ = 6,
        SWIFT_SIGNED_HASH = 7,
        SWIFT_HINT = 8,
        SWIFT_MSGTYPE_RCVD = 9,
        SWIFT_RANDOMIZE = 10, //FRAGRAND
        SWIFT_VERSION = 11, // Arno, 2011-10-19: TODO to match RFC-rev-03
        SWIFT_MESSAGE_COUNT = 12
    } messageid_t;

    typedef enum {
        DDIR_UPLOAD,
        DDIR_DOWNLOAD
    } data_direction_t;


    typedef enum {
        FILE_TRANSFER,
        LIVE_TRANSFER
    } transfer_t;


    class PiecePicker;
    //class CongestionController; // Arno: Currently part of Channel. See ::NextSendTime
    class PeerSelector;
    class Channel;
    typedef void (*ProgressCallback) (int fdes, bin_t bin);
    class Storage;

    /** Superclass for live and vod */
    class ContentTransfer {

      public:
    	ContentTransfer();
    	~ContentTransfer();

        // Global var
        static std::vector<ContentTransfer*> swarms;
        /** Find transfer by the root hash. */
        static ContentTransfer* Find (const Sha1Hash& swarmid);
        /** Find transfer by the file descriptor. */
        static ContentTransfer* transfer(int fdes) { return fdes<swarms.size() ? (ContentTransfer *)swarms[fdes] : NULL; }
        void GlobalAdd();
        void GlobalDel();

        /** Returns the type of transfer, FILE_TRANSFER or LIVE_TRANSFER */
    	virtual transfer_t	ttype() = 0;
    	/** Returns the global ID for this transfer */
        virtual const Sha1Hash& 	swarm_id() const = 0;
        /** The binmap pointer for data already retrieved and checked. */
    	virtual binmap_t *  ack_out() = 0;
    	/** Returns the number of bytes in a chunk for this transfer */
        virtual size_t	  	chunk_size() = 0;

		std::set<Channel *>& GetChannels() { return mychannels_; }
		/** Piece picking strategy used by this transfer. */
        PiecePicker *    picker () { return picker_; }

        /** Returns the local ID for this transfer. */
        int             fd () const { return fd_; }



#define SWFT_MAX_TRANSFER_CB 8
        ProgressCallback callbacks[SWFT_MAX_TRANSFER_CB];
        uint8_t         cb_agg[SWFT_MAX_TRANSFER_CB];
        int             cb_installed;

        // Gertjan fix: return bool
        bool            OnPexIn (const Address& addr);
        // Gertjan
        Channel *		RandomChannel(Channel *notc);
		/** Arno: Return the Channel to peer "addr" that is not equal to "notc". */
		Channel * 		FindChannel(const Address &addr, Channel *notc);
		void			CloseChannels(std::set<Channel *>	delset);
		void			GarbageCollectChannels();

		// RATELIMIT
        /** Arno: Call when n bytes are received. */
        void			OnRecvData(int n);
        /** Arno: Call when n bytes are sent. */
        void			OnSendData(int n);
        /** Arno: Call when no bytes are sent due to rate limiting. */
        void 			OnSendNoData();
        /** Arno: Return current speed for the given direction in bytes/s */
		double 			GetCurrentSpeed(data_direction_t ddir);
		/** Arno: Return maximum speed for the given direction in bytes/s */
		double			GetMaxSpeed(data_direction_t ddir);
		/** Arno: Set maximum speed for the given direction in bytes/s */
		void			SetMaxSpeed(data_direction_t ddir, double m);
		/** Arno: Return the number of non-seeders current channeled with. */
		uint32_t		GetNumLeechers();
		/** Arno: Return the number of seeders current channeled with. */
		uint32_t		GetNumSeeders();

		// MULTIFILE
		Storage * GetStorage() { return storage_; }

		/** Add a peer to the set of addresses to connect to */
		void AddPeer(Address &peer);

		static void LibeventCleanCallback(int fd, short event, void *arg);

      protected:
		int					fd_;	// Arno: index into ContentTransfer::swarms vector

        /** Channels working for this transfer. */
        binqueue        	hs_in_;			// Arno, 2011-10-03: Should really be queue of channel ID (=uint32_t)

        std::set<Channel *>	mychannels_;

        /** Piece picker strategy. */
        PiecePicker*    	picker_;

		// RATELIMIT
        MovingAverageSpeed	cur_speed_[2];
        double				max_speed_[2];
        int					speedzerocount_;

        struct event 		evclean_;

        // MULTIFILE
        Storage				*storage_;

        friend void LibeventCleanCallback(int fd, short event, void *arg);
    };



    /** A class representing single file transfer. */
    class    FileTransfer : public ContentTransfer {

    public:

        /** A constructor. Open/submit/retrieve a file.
         *  @param file_name    the name of the file
         *  @param root_hash    the root hash of the file; zero hash if the file
	 	 *                      is newly submitted
	 	 */
        FileTransfer(std::string file_name, const Sha1Hash& root_hash=Sha1Hash::ZERO,bool check_hashes=true,uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE, bool zerostate=false);

        /**    Close everything. */
        ~FileTransfer();

        // LIVE
        transfer_t 		ttype() { return FILE_TRANSFER; }
        const Sha1Hash& swarm_id() const { return hashtree_->root_hash(); }

        /** While we need to feed ACKs to every peer, we try (1) avoid
            unnecessary duplication and (2) keep minimum state. Thus,
            we use a rotating queue of bin completion events. */
        //bin64_t         RevealAck (uint64_t& offset);
        /** Rotating queue read for channels of this transmission. */
        // Jori
        int             RevealChannel (int& i);

        /** The binmap pointer for data already retrieved and checked. */
        binmap_t *      ack_out ()  { return hashtree_->ack_out(); }
        /** Piece picking strategy used by this transfer. */
        PiecePicker *   picker () { return picker_; }
        /** Hash tree checked file; all the hashes and data are kept here. */
        HashTree *      hashtree() { return hashtree_; }
        /** Root SHA1 hash of the transfer (and the data file). */
        const Sha1Hash& root_hash () const { return hashtree_->root_hash(); }
        size_t	  		chunk_size() { return hashtree_->chunk_size(); }
        /** Ric: the availability in the swarm */
        Availability&	availability() { return *availability_; }


		/** Arno: set the tracker for this transfer. Reseting it won't kill
		 * any existing connections.
		 */
		void SetTracker(Address tracker) { tracker_ = tracker; }

		/** Arno: (Re)Connect to tracker for this transfer, or global Channel::tracker if not set */
		void ConnectToTracker();

		/** Arno: Reconnect to the tracker if no established peers and
		 * exp backoff allows it.
		 */
		void ReConnectToTrackerIfAllowed(bool hasestablishedpeers);

		// SAFECLOSE
		static void LibeventCleanCallback(int fd, short event, void *arg);

		//ZEROSTATE
		/** Returns whether this FileTransfer is running in zero-state mode,
		 * meaning that the hash tree is not mmapped into memory but read
		 * directly from disk, and other memory saving measures.
		 */
		bool IsZeroState() { return zerostate_; }

    protected:

        HashTree*		hashtree_;

        /** Messages we are accepting.    */
        uint64_t        cap_out_;

        tint            init_time_;
        
        // Ric: PPPLUG
        /** Availability in the swarm */
        Availability* 	availability_;

        Address 		tracker_; // Tracker for this transfer
        tint			tracker_retry_interval_;
        tint			tracker_retry_time_;

        //ZEROSTATE
        bool			zerostate_;

    public:
        void            OnDataIn (bin_t pos);

        friend class Channel;
        // Ric: maybe not really needed
        friend class Availability;
        friend uint64_t Size (int fdes);
        friend bool     IsComplete (int fdes);
        friend uint64_t Complete (int fdes);
        friend uint64_t SeqComplete (int fdes, int64_t offset);
        friend int     	Open (const char* filename, const Sha1Hash& hash, Address tracker, bool check_hashes, uint32_t chunk_size);
        friend void    	Close (int fd) ;
        friend void 	AddProgressCallback (int fdes,ProgressCallback cb,uint8_t agg);
        friend void 	RemoveProgressCallback (int fdes,ProgressCallback cb);
        friend void 	ExternallyRetrieved (int fdes,bin_t piece);
    };

    /** A class representing live transfer. */
     class    LiveTransfer : public ContentTransfer {
       public:

        /** A constructor. */
        LiveTransfer(std::string filename, const Sha1Hash& swarm_id=Sha1Hash::ZERO,bool amsource=false,size_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE);

        /**    Close everything. */
        ~LiveTransfer();

        transfer_t 		ttype() { return LIVE_TRANSFER; }
        const Sha1Hash& swarm_id() const { return swarm_id_; }
        /** The binmap for data already retrieved and checked. */
        binmap_t *     ack_out ()  { return &ack_out_; }

        /** Returns the number of bytes that are complete sequentially, starting from the
             beginning, till the first not-yet-retrieved packet. */
        uint64_t  		SeqComplete();
        /** Returns the number of bytes in a chunk for this transmission */
        size_t	  		chunk_size() { return chunk_size_; }
        bool 	  		am_source() { return am_source_; }


        /** Source: add a chunk to the swarm */
        int 			AddData(const void *buf, size_t nbyte);

        /** Returns the byte offset at which we hooked into the live stream */
        uint64_t  		GetHookinOffset();

       protected:
        /** Swarm Identifier */
        Sha1Hash swarm_id_;
        /**    Binmap of own chunk availability */
        binmap_t        ack_out_;

        /** Source: Am I a source */
        bool 			am_source_;

        /** Name of file used for storing live chunks */
        std::string		filename_;
     	// CHUNKSIZE
     	/** Arno: configurable fixed chunk size in bytes */
     	size_t			chunk_size_;

        /** Source: ID of last generated chunk */
        uint64_t		last_chunkid_;
        /** Source: Current write position in storage file */
        size_t			offset_;
     };


    /** PiecePicker implements some strategy of choosing (picking) what
        to request next, given the possible range of choices:
        data acknowledged by the peer minus data already retrieved.
        May pick sequentially, do rarest first or in some other way. */
    class PiecePicker {
      public:
        virtual void Randomize (uint64_t twist) = 0;
        /** The piece picking method itself.
         *  @param  offered     the data acknowledged by the peer
         *  @param  max_width   maximum number of packets to ask for
         *  @param  expires     (not used currently) when to consider request expired
         *  @return             the bin number to request */
        virtual bin_t Pick (binmap_t& offered, uint64_t max_width, tint expires) = 0;
        virtual void LimitRange (bin_t range) = 0;
        /** updates the playback position for streaming piece picking.
         *  @param  offbin		bin number of new playback pos
         *  @param  whence      only SEEK_CUR supported */
        virtual int Seek(bin_t offbin, int whence) = 0;
        virtual ~PiecePicker() {}
    };

	class LivePiecePicker : public PiecePicker {
      public:
        virtual void StartAddPeerPos(uint32_t channelid, bin_t peerpos, bool peerissource) = 0;
        virtual void EndAddPeerPos(uint32_t channelid) = 0;

        virtual bin_t GetHookinPos() = 0;
        virtual bin_t GetCurrentPos() = 0;
    };


    class PeerSelector { // Arno: partically unused
    public:
        virtual void AddPeer (const Address& addr, const Sha1Hash& root) = 0;
        virtual Address GetPeer (const Sha1Hash& for_root) = 0;
    };


    /**    swift channel's "control block"; channels loosely correspond to TCP
	   connections or FTP sessions; one channel is created for one file
	   being transferred between two peers. As we don't need buffers and
	   lots of other TCP stuff, sizeof(Channel+members) must be below 1K.
	   Normally, API users do not deal with this class. */
    class Channel {

    public:
        Channel    (ContentTransfer* transfer, int socket=INVALID_SOCKET, Address peer=Address(), bool peerissource=false);
        ~Channel();

        typedef enum {
            KEEP_ALIVE_CONTROL,
            PING_PONG_CONTROL,
            SLOW_START_CONTROL,
            AIMD_CONTROL,
            LEDBAT_CONTROL,
            CLOSE_CONTROL
        } send_control_t;

        static Address  tracker; // Global tracker for all transfers
        struct event *evsend_ptr_; // Arno: timer per channel // SAFECLOSE
        //LIVE
        struct event *evsendlive_ptr_; // Arno: timer per channel
        static struct event_base *evbase;
        static struct event evrecv;
        static const char* SEND_CONTROL_MODES[];

	    static tint epoch, start;
	    static uint64_t global_dgrams_up, global_dgrams_down, global_raw_bytes_up, global_raw_bytes_down, global_bytes_up, global_bytes_down;
        static void CloseChannelByAddress(const Address &addr);

        // SOCKMGMT
        // Arno: channel is also a "singleton" class that manages all sockets
        // for a swift process
        static void LibeventSendCallback(int fd, short event, void *arg);
        static void LibeventReceiveCallback(int fd, short event, void *arg);
        static void RecvDatagram (evutil_socket_t socket); // Called by LibeventReceiveCallback
	    static int RecvFrom(evutil_socket_t sock, Address& addr, struct evbuffer *evb); // Called by RecvDatagram
	    static int SendTo(evutil_socket_t sock, const Address& addr, struct evbuffer *evb); // Called by Channel::Send()
	    static evutil_socket_t Bind(Address address, sckrwecb_t callbacks=sckrwecb_t());
	    static Address BoundAddress(evutil_socket_t sock);
	    static evutil_socket_t default_socket()
            { return sock_count ? sock_open[0].sock : INVALID_SOCKET; }

	    /** close the port */
	    static void CloseSocket(evutil_socket_t sock);
	    static void Shutdown ();
	    /** the current time */
	    static tint Time();

	    // Arno: Per instance methods
        void        Recv (struct evbuffer *evb);
        void        Send ();  // Called by LibeventSendCallback
        void        Close ();

        void        OnAck (struct evbuffer *evb);
        void        OnHave (struct evbuffer *evb);
        bin_t       OnData (struct evbuffer *evb);
        void        OnHint (struct evbuffer *evb);
        void        OnHash (struct evbuffer *evb);
        void        OnPexAdd (struct evbuffer *evb);
        void        OnHandshake (struct evbuffer *evb);
        void        OnRandomize (struct evbuffer *evb); //FRAGRAND
        void        AddHandshake (struct evbuffer *evb);
        bin_t       AddData (struct evbuffer *evb);
        void        AddAck (struct evbuffer *evb);
        void        AddHave (struct evbuffer *evb);
        void        AddHint (struct evbuffer *evb);
        void        AddUncleHashes (struct evbuffer *evb, bin_t pos);
        void        AddPeakHashes (struct evbuffer *evb);
        void        AddPex (struct evbuffer *evb);
        void        OnPexReq(void);
        void        AddPexReq(struct evbuffer *evb);
        void        BackOffOnLosses (float ratio=0.5);
        tint        SwitchSendControl (int control_mode);
        tint        NextSendTime ();
        tint        KeepAliveNextSendTime ();
        tint        PingPongNextSendTime ();
        tint        CwndRateNextSendTime ();
        tint        SlowStartNextSendTime ();
        tint        AimdNextSendTime ();
        tint        LedbatNextSendTime ();
        /** Arno: return true if this peer has complete file. May be fuzzy if Peak Hashes not in */
        bool		IsComplete();
        /** Arno: return (UDP) port for this channel */
        uint16_t 	GetMyPort();
        bool 		IsDiffSenderOrDuplicate(Address addr, uint32_t chid);

        static int  MAX_REORDERING;
        static tint TIMEOUT;
        static tint MIN_DEV;
        static tint MAX_SEND_INTERVAL;
        static tint LEDBAT_TARGET;
        static float LEDBAT_GAIN;
        static tint LEDBAT_DELAY_BIN;
        static bool SELF_CONN_OK;
        static tint MAX_POSSIBLE_RTT;
        static tint MIN_PEX_REQUEST_INTERVAL;
        static FILE* debug_file;

        const std::string id_string () const;
        /** A channel is "established" if had already sent and received packets. */
        bool        is_established () { return peer_channel_id_ && own_id_mentioned_; }
        HashTree *   hashtree();
        ContentTransfer *transfer() { return transfer_; }
        const Address& peer() const { return peer_; }
        const Address& recv_peer() const { return recv_peer_; }
        tint ack_timeout () {
        	tint dev = dev_avg_ < MIN_DEV ? MIN_DEV : dev_avg_;
        	tint tmo = rtt_avg_ + dev * 4;
        	return tmo < 30*TINT_SEC ? tmo : 30*TINT_SEC;
        }
        uint32_t    id () const { return id_; }

        // MORESTATS
        uint64_t raw_bytes_up() { return raw_bytes_up_; }
        uint64_t raw_bytes_down() { return raw_bytes_down_; }
        uint64_t bytes_up() { return bytes_up_; }
        uint64_t bytes_down() { return bytes_down_; }

        static int  DecodeID(int scrambled);
        static int  EncodeID(int unscrambled);
        static Channel* channel(int i) {
            return i<channels.size()?channels[i]:NULL;
        }
        static void CloseTransfer (ContentTransfer* trans);

        // SAFECLOSE
        void		ClearEvents();
        void 		Schedule4Close() { scheduled4close_ = true; }
        bool		IsScheduled4Close() { return scheduled4close_; }

        //ZEROSTATE
        void OnDataZeroState(struct evbuffer *evb);
        void OnHaveZeroState(struct evbuffer *evb);
        void OnHashZeroState(struct evbuffer *evb);
        void OnPexAddZeroState(struct evbuffer *evb);
        void OnPexReqZeroState(struct evbuffer *evb);

        // LIVE
        void        LiveSend();  // Called when source generates chunk.

    protected:
#define DGRAM_MAX_SOCK_OPEN 128
   	    static int sock_count;
	    static sckrwecb_t sock_open[DGRAM_MAX_SOCK_OPEN];


        /** Channel id: index in the channel array. */
        uint32_t    id_;
        /**    Socket address of the peer. */
        Address     peer_;
        /**    The UDP socket fd. */
        evutil_socket_t      socket_;
        /**    Descriptor of the file in question. */
        ContentTransfer*    transfer_;
        /**    Peer channel id; zero if we are trying to open a channel. */
        uint32_t    peer_channel_id_;
        bool        own_id_mentioned_;
        /**    Peer's progress, based on acknowledgements. */
        binmap_t    ack_in_;
        /**    Last data received; needs to be acked immediately. */
        tintbin     data_in_;
        bin_t       data_in_dbl_;
        /** The history of data sent and still unacknowledged. */
        tbqueue     data_out_;
        /** Timeouted data (potentially to be retransmitted). */
        tbqueue     data_out_tmo_;
        bin_t       data_out_cap_;
        /** Index in the history array. */
        binmap_t    have_out_;
        /**    Transmit schedule: in most cases filled with the peer's hints */
        tbqueue     hint_in_;
        /** Hints sent (to detect and reschedule ignored hints). */
        tbqueue     hint_out_;
        uint64_t    hint_out_size_;
        /** Types of messages the peer accepts. */
        uint64_t    cap_in_;
        /** For repeats. */
        //tint        last_send_time, last_recv_time;
        /** PEX progress */
        bool        pex_requested_;
        tint        last_pex_request_time_;
        tint        next_pex_request_time_;
        bool        pex_request_outstanding_;
        tbqueue     reverse_pex_out_;		// Arno, 2011-10-03: should really be a queue of (tint,channel id(= uint32_t)) pairs.
        int         useless_pex_count_;
        /** Smoothed averages for RTT, RTT deviation and data interarrival periods. */
        tint        rtt_avg_, dev_avg_, dip_avg_;
        tint        last_send_time_;
        tint        last_recv_time_;
        tint        last_data_out_time_;
        tint        last_data_in_time_;
        tint        last_loss_time_;
        tint        next_send_time_;
        /** Congestion window; TODO: int, bytes. */
        float       cwnd_;
        int         cwnd_count1_;
        /** Data sending interval. */
        tint        send_interval_;
        /** The congestion control strategy. */
        int         send_control_;
        /** Datagrams (not data) sent since last recv.    */
        int         sent_since_recv_;

        /** Arno: Fix for KEEP_ALIVE_CONTROL */
        bool 		lastrecvwaskeepalive_;
        bool 		lastsendwaskeepalive_;

        /** Recent acknowlegements for data previously sent.    */
        int         ack_rcvd_recent_;
        /** Recent non-acknowlegements (losses) of data previously sent.    */
        int         ack_not_rcvd_recent_;
        /** LEDBAT one-way delay machinery */
        tint        owd_min_bins_[4];
        int         owd_min_bin_;
        tint        owd_min_bin_start_;
        tint        owd_current_[4];
        int         owd_cur_bin_;
        /** Stats */
        int         dgrams_sent_;
        int         dgrams_rcvd_;
        // Arno, 2011-11-28: for detailed, per-peer stats. MORESTATS
        uint64_t raw_bytes_up_, raw_bytes_down_, bytes_up_, bytes_down_;

        // SAFECLOSE
        bool		scheduled4close_;
        /** Arno: Socket address of the peer where packets are received from,
         * when an IANA private address, otherwise 0.
         * May not be equal to peer_. 2PEERSBEHINDSAMENAT */
        Address     recv_peer_;

		bool		direct_sending_;

        //LIVE
        bool		peer_is_source_;

        int         PeerBPS() const {
            return TINT_SEC / dip_avg_ * 1024;
        }
        /** Get a request for one packet from the queue of peer's requests. */
        bin_t       DequeueHint(bool *retransmitptr);
        bin_t       ImposeHint();
        void        TimeoutDataOut ();
        void        CleanStaleHintOut();
        void        CleanHintOut(bin_t pos);
        void        Reschedule();
        void 		UpdateDIP(bin_t pos); // RETRANSMIT


        static PeerSelector* peer_selector;

        static tint     last_tick;
        //static tbheap   send_queue;

        static std::vector<Channel*> channels;

        friend int      Listen (Address addr);
        friend void     Shutdown (int sock_des);
        friend void     AddPeer (Address address, const Sha1Hash& root);
        friend void     SetTracker(const Address& tracker);
        friend int      Open (const char*, const Sha1Hash&, Address tracker, bool check_hashes, uint32_t chunk_size) ; // FIXME
        // SOCKTUNNEL
        friend void 	CmdGwTunnelSendUDP(struct evbuffer *evb);
        friend int      LiveOpen(std::string filename, const Sha1Hash&,Address, bool,size_t); //LIVETODO
    };


    // MULTIFILE
    /*
     * Class representing a single file in a multi-file swarm.
     */
    class StorageFile
    {
       public:
    	 StorageFile(std::string specpath, int64_t start, int64_t size, std::string ospath);
    	 ~StorageFile();
    	 int64_t GetStart() { return start_; }
    	 int64_t GetEnd() { return end_; }
    	 int64_t GetSize() { return end_+1-start_; }
    	 std::string GetSpecPathName() { return spec_pathname_; }
    	 std::string GetOSPathName() { return os_pathname_; }
    	 ssize_t  Write(const void *buf, size_t nbyte, int64_t offset) { return pwrite(fd_,buf,nbyte,offset); }
    	 ssize_t  Read(void *buf, size_t nbyte, int64_t offset) {  return pread(fd_,buf,nbyte,offset); }
    	 int ResizeReserved() { return file_resize(fd_,GetSize()); }

       protected:
    	 std::string spec_pathname_;
    	 std::string os_pathname_;
    	 int64_t	start_;
    	 int64_t	end_;

    	 int		fd_;
    };

    typedef std::vector<StorageFile *>	storage_files_t;

    /*
     * Class representing the persistent storage layer. Supports a swarm
     * stored as multiple files.
     *
     * This is implemented by storing a multi-file specification in chunk 0
     * (and further if needed). This spec lists what other files the swarm
     * contains and their sizes. E.g.
     *
     * META-INF-multifilespec.txt 113
	 * seeder/190557.ts 249798796
	 * seeder/berta.dat 2395920988
	 * seeder/bunny.ogg 166825767
	 *
	 * The concatenation of these files (starting with the multi-file spec with
	 * pseudo filename META-INF-multifile-spec.txt) are the contents of the
	 * swarm.
     */
	class Storage {

	  public:

		static const std::string MULTIFILE_PATHNAME;
		static const std::string MULTIFILE_PATHNAME_FILE_SEP;
		static const int 		 MULTIFILE_MAX_PATH = 2048;
		static const int 		 MULTIFILE_MAX_LINE = MULTIFILE_MAX_PATH+1+32+1;

		typedef enum {
			STOR_STATE_INIT,
			STOR_STATE_MFSPEC_SIZE_KNOWN,
			STOR_STATE_MFSPEC_COMPLETE,
			STOR_STATE_SINGLE_FILE
		} storage_state_t;

		typedef std::vector<StorageFile *>	storage_files_t;

		/** convert multi-file spec filename (UTF-8 encoded Unicode) to OS name and vv. */
		static std::string spec2ospn(std::string specpn);
		static std::string os2specpn(std::string ospn);

		/** Create Storage from specified path and destination dir if content turns about to be a multi-file */
		Storage(std::string ospathname, std::string destdir,int transferfd);
		~Storage();

		/** UNIX pread approximation. Does change file pointer. Thread-safe if no concurrent writes */
		ssize_t  Read(void *buf, size_t nbyte, int64_t offset); // off_t not 64-bit dynamically on Win32

		/** UNIX pwrite approximation. Does change file pointer. Is not thread-safe */
		ssize_t  Write(const void *buf, size_t nbyte, int64_t offset);

		/** Link to HashTree */
		void SetHashTree(HashTree *ht) { ht_ = ht; }

		/** Size of content according to multi-file spec, -1 if unknown or single file */
		int64_t GetSizeFromSpec();

		/** Size reserved for storage */
		int64_t GetReservedSize();

		/** 0 for single file, spec size for multi-file */
		int64_t GetMinimalReservedSize();

		/** Change size reserved for storage */
		int ResizeReserved(int64_t size);

		/** Return the operating system path for this Storage */
		std::string GetOSPathName() { return os_pathname_; }

		/** Return the root hash of the content being stored */
		std::string roothashhex() { if (ht_ == NULL) return "0000000000000000000000000000000000000000"; else return ht_->root_hash().hex(); }

		/** Return the destination directory for this Storage */
		std::string GetDestDir() { return destdir_; }

		/** Whether Storage is ready to be used */
		bool IsReady() { return state_ == STOR_STATE_SINGLE_FILE || state_ == STOR_STATE_MFSPEC_COMPLETE; }

		/** Return the list of StorageFiles for this Storage, empty if not multi-file */
		storage_files_t	GetStorageFiles() { return sfs_; }

		/** Return a one-time callback when swift starts allocating disk space */
		void AddOneTimeAllocationCallback(ProgressCallback cb) { alloc_cb_ = cb; }

	  protected:
			storage_state_t	state_;

			std::string os_pathname_;
			std::string destdir_;

			/** HashTree this Storage is linked to */
			HashTree *ht_;

			int64_t spec_size_;

			storage_files_t	sfs_;
			int single_fd_;
			int64_t reserved_size_;
			int64_t total_size_from_spec_;
			StorageFile *last_sf_;

			int transfer_fd_;
			ProgressCallback alloc_cb_;

			int WriteSpecPart(StorageFile *sf, const void *buf, size_t nbyte, int64_t offset);
			std::pair<int64_t,int64_t> WriteBuffer(StorageFile *sf, const void *buf, size_t nbyte, int64_t offset);
			StorageFile * FindStorageFile(int64_t offset);
			int ParseSpec(StorageFile *sf);
			int OpenSingleFile();

	};

	class ZeroState
	{
	  public:
    	ZeroState();
    	~ZeroState();
    	static ZeroState *GetInstance();
    	void SetContentDir(std::string contentdir);
    	FileTransfer * Find(Sha1Hash &root_hash);


    	static void LibeventCleanCallback(int fd, short event, void *arg);

	  protected:
    	static ZeroState *__singleton;

    	struct event 		evclean_;
        std::string 		contentdir_;

	};


    /*************** The top-level API ****************/
    /** Start listening a port. Returns socket descriptor. */
    int     Listen (Address addr);
    /** Stop listening to a port. */
    void    Shutdown (int sock_des=-1);

    /** Open a file, start a transmission; fill it with content for a given
        root hash and tracker (optional). If hashchecking is requested the
        hashtree state will be (re)constructed from the file on disk (if any).
        If not requested, open will try to reconstruct the hashtree state from
        the .mhash and .mbinmap files on disk. .mhash files are created
        automatically, .mbinmap files must be written by checkpointing the
        transfer by calling FileTransfer::serialize(). If the reconstruction
        fails, it will hashcheck anyway. Roothash is optional for new files or
        files already hashchecked and checkpointed. If roothash is set and
        filename is a directory, swift will download the content to dir/roothash-in-hex.
        */
    int     Open (std::string filename, const Sha1Hash& hash=Sha1Hash::ZERO,Address tracker=Address(), bool check_hashes=true,uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE);
    /** Get the root hash for the transmission. */
    const Sha1Hash& SwarmID (int file) ;
    /** Close a file and a transmission. */
    void    Close (int fd) ;
    /** Add a possible peer which participares in a given transmission. In the case
        root hash is zero, the peer might be talked to regarding any transmission
        (likely, a tracker, cache or an archive). */
    void    AddPeer (Address address, const Sha1Hash& root=Sha1Hash::ZERO);

	/** UNIX pread approximation. Does change file pointer. Thread-safe if no concurrent writes */
	ssize_t  Read(int fd, void *buf, size_t nbyte, int64_t offset); // off_t not 64-bit dynamically on Win32

	/** UNIX pwrite approximation. Does change file pointer. Is not thread-safe */
	ssize_t  Write(int fd, const void *buf, size_t nbyte, int64_t offset);

    /** Seek, i.e., move start of interest window */
    int Seek(int fd, int64_t offset, int whence);

	void    SetTracker(const Address& tracker);
    /** Set the default tracker that is used when Open is not passed a tracker
        address. */

    /** Returns size of the file in bytes, 0 if unknown. Might be rounded up to a kilobyte
        before the transmission is complete. */
    uint64_t  Size (int fdes);
    /** Returns the amount of retrieved and verified data, in bytes.
        A 100% complete transmission has Size()==Complete(). */
    uint64_t  Complete (int fdes);
    bool      IsComplete (int fdes);
    /** Returns the number of bytes that are complete sequentially, starting from the
        beginning, till the first not-yet-retrieved packet.
        For LIVE beginning = GetHookinOffset() */
    uint64_t  SeqComplete(int fdes, int64_t offset=0);

    uint64_t  GetHookinOffset(int fdes);

    /***/
    int       Find (Sha1Hash hash);
    /** Returns the number of bytes in a chunk for this transmission */
    uint32_t	  ChunkSize(int fdes);

    /** Get the address bound to the socket descriptor returned by Listen() */
    Address BoundAddress(evutil_socket_t sock);

    // LIVE
    /** To create a live stream as source */
    LiveTransfer *LiveCreate(std::string filename, const Sha1Hash& swarmid, size_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE);
    /** To add chunks to a live stream as source */
    int LiveWrite(LiveTransfer *lt, const void *buf, size_t nbyte, long offset);
    /** To open a live stream as peer */
    int LiveOpen(std::string filename, const Sha1Hash& hash=Sha1Hash::ZERO,Address tracker=Address(), bool check_hashes=true,size_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE);


    void AddProgressCallback (int fdes,ProgressCallback cb,uint8_t agg);
    void RemoveProgressCallback (int fdes,ProgressCallback cb);
    void ExternallyRetrieved (int fdes,bin_t piece);


    /** Must be called by any client using the library */
    void LibraryInit(void);

    int evbuffer_add_string(struct evbuffer *evb, std::string str);
    int evbuffer_add_8(struct evbuffer *evb, uint8_t b);
    int evbuffer_add_16be(struct evbuffer *evb, uint16_t w);
    int evbuffer_add_32be(struct evbuffer *evb, uint32_t i);
    int evbuffer_add_64be(struct evbuffer *evb, uint64_t l);
    int evbuffer_add_hash(struct evbuffer *evb, const Sha1Hash& hash);

    uint8_t evbuffer_remove_8(struct evbuffer *evb);
    uint16_t evbuffer_remove_16be(struct evbuffer *evb);
    uint32_t evbuffer_remove_32be(struct evbuffer *evb);
    uint64_t evbuffer_remove_64be(struct evbuffer *evb);
    Sha1Hash evbuffer_remove_hash(struct evbuffer* evb);

    const char* tintstr(tint t=0);
    std::string sock2str (struct sockaddr_in addr);
 #define SWIFT_MAX_CONNECTIONS 20

    void nat_test_update(void);

    // Arno: Save transfer's binmap for zero-hashcheck restart
    int Checkpoint(int fdes);

    // SOCKTUNNEL
    void CmdGwTunnelUDPDataCameIn(Address srcaddr, uint32_t srcchan, struct evbuffer* evb);
    void CmdGwTunnelSendUDP(struct evbuffer *evb); // for friendship with Channel

} // namespace end

// #define SWIFT_MUTE

#ifndef SWIFT_MUTE
#define dprintf(...) do { if (Channel::debug_file) fprintf(Channel::debug_file,__VA_ARGS__); } while (0)
#define dflush() fflush(Channel::debug_file)
#else
#define dprintf(...) do {} while(0)
#define dflush() do {} while(0)
#endif
#define eprintf(...) fprintf(stderr,__VA_ARGS__)

#endif
