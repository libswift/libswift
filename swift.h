/*
 *  swift.h
 *  the main header file for libswift, normally you should only read this one.
 *
 *  This implementation supports 2 versions of swift:
 *  - the original (legacy version)
 *  - the IETF Peer-to-Peer Streaming Peer Protocol -03 compliant one.
 *
 *  Arno: libswift clients should call the swift top-level API which consists of
 *  swift::Listen, Open, Read, etc. The *Transfer interfaces are internal
 *  to the library, except in a few exceptional cases.
 *
 *  The swift API hides the swarm management mechanism that activates/
 *  deactivates file-based swarms. A swarm is activated when a *Transfer object
 *  exists. Live swarms are always active (LiveTransfer). Orthogonal to swarm
 *  activation is the use of file-based swarms in zero-state mode, which means
 *  that when activated only the minimal information needed to use the swarm is
 *  loaded into memory. In particular, content and hashes are read directly
 *  from disk.
 *
 *  Created by Victor Grishchenko, Arno Bakker, Riccardo Petrocco
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
/*

  The swift protocol (legacy)

  Messages

  HANDSHAKE    00, channelid
  Communicates the channel id of the sender. The
  initial handshake packet also has the root hash
  (a HASH message).

  DATA        01, bin_32, buffer
  1K of data.

  ACK         02, bin_32, timestamp_64
  HAVE        03, bin_32
  Confirms successful delivery of data. Used for congestion control, as well.

  REQUEST     08, bin_32
  Practical value of requests aka "hints" is to avoid overlap, mostly.
  Hints might be lost in the network or ignored.
  Peer might send out data without a hint.
  Hint which was not responded (by DATA) in some RTTs
  is considered to be ignored.
  As peers cant pick randomly kilobyte here and there,
  they send out "long hints" for non-base bins.

  INTEGRITY   04, bin_32, sha1hash
  SHA1 hash tree hashes for data verification. The
  connection to a fresh peer starts with bootstrapping
  him with peak hashes. Later, before sending out
  any data, a peer sends the necessary uncle hashes.

  PEX+/PEX-   05/06, ipv4 addr, port
  Peer exchange messages; reports all connected and
  disconnected peers. Might has special meaning (as
  in the case with swarm supervisors).

*/
#ifndef SWIFT_H
#define SWIFT_H

// Arno, 2013-06-11: Must come first to ensure SIZE_MAX etc are defined
#include "compat.h"
#include <deque>
#include <vector>
#include <set>
#include <map>
#include <list>
#include <algorithm>
#include <string>

#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/buffer.h>
#include "bin.h"
#include "binmap.h"
#include "hashtree.h"
#include "livehashtree.h"
#include "avgspeed.h"
#include "avail.h"
#include "exttrack.h"


namespace swift {

// Arno, 2012-12-12: Configure which PPSP version to use by default. Set to 0 for legacy swift.
#define ENABLE_IETF_PPSP_VERSION      1

// Whether to try legacy protocol when PPSP handshakes don't result in response
#define ENABLE_FALLBACK_TO_LEGACY_PROTO	0

// Arno, 2011-12-22: Enable Riccardo's VodPiecePicker
#define ENABLE_VOD_PIECEPICKER        0

// Arno, 2013-10-02: Configure which live piecepicker: default or with small-swarms optimization
#define ENABLE_LIVE_SMALLSWARMOPT_PIECEPICKER      1


// Arno, 2013-10-02: Default for mobile devices. Set to 0 to disable.
#define DEFAULT_MOBILE_LIVE_DISC_WND_BYTES		   (1*1024*1024*1024) // 1 GB

// Value for protocol option: Live Discard Window
#define POPT_LIVE_DISC_WND_ALL	      	     0xFFFFFFFF	// automatically truncated for 32-bit

// scheme for swift URLs
#define SWIFT_URI_SCHEME              "tswift"

// Max incoming connections. Must be set to large value with swift tracker
#define SWIFT_MAX_INCOMING_CONNECTIONS		0xffff

// Max outgoing connections
#define SWIFT_MAX_OUTGOING_CONNECTIONS 		20

// Max size of the swarm ID protocol option in a HANDSHAKE message.
#define POPT_MAX_SWARMID_SIZE		     1024
// Max size of a X.509 certificate in a PEX_REScert message.
#define PEX_RES_MAX_CERT_SIZE		     1024

// Live streaming via Unified Merkle Tree or Sign All: The number of chunks per signature
// Set to 1 for Sign All, set to a power of 2 > 1 for UMT. This MUST be a power of 2.
#define SWIFT_DEFAULT_LIVE_NCHUNKS_PER_SIGN   32

	// Ric: allowed hints in the future (e.g., 2 x TINT_SEC)
#define HINT_TIME                       1	// seconds

// How much time a SIGNED_INTEGRITY timestamp may diverge from current time
#define SWIFT_LIVE_MAX_SOURCE_DIVERGENCE_TIME	30 // seconds


#define SWIFT_MAX_UDP_OVER_ETH_PAYLOAD        (1500-20-8)
// Arno: Maximum size of non-DATA messages in a UDP packet we send.
#define SWIFT_MAX_NONDATA_DGRAM_SIZE         (SWIFT_MAX_UDP_OVER_ETH_PAYLOAD-SWIFT_DEFAULT_CHUNK_SIZE-1-4)
// Arno: Maximum size of a UDP packet we send. Note: depends on CHUNKSIZE 8192
#define SWIFT_MAX_SEND_DGRAM_SIZE            (SWIFT_MAX_NONDATA_DGRAM_SIZE+1+4+8192)
// Arno: Maximum size of a UDP packet we are willing to accept. Note: depends on CHUNKSIZE 8192
#define SWIFT_MAX_RECV_DGRAM_SIZE            (SWIFT_MAX_SEND_DGRAM_SIZE*2)

#define layer2bytes(ln,cs)    (uint64_t)( ((double)cs)*pow(2.0,(double)ln))
#define bytes2layer(bn,cs)  (int)log2(  ((double)bn)/((double)cs) )




    typedef enum {
	FILE_TRANSFER,
	LIVE_TRANSFER
    } transfer_t;


    struct SwarmID  {
      public:
        SwarmID() : empty_(true) {}
        SwarmID(const Sha1Hash &roothash) { ttype_ = FILE_TRANSFER; roothash_ = roothash; empty_=false;}
        SwarmID(const SwarmPubKey &spubkey) { ttype_ = LIVE_TRANSFER; spubkey_ = spubkey; empty_=false;}
        SwarmID(std::string hexstr);
        SwarmID(uint8_t *data,uint16_t datalength);
        ~SwarmID();
        bool    operator == (const SwarmID& b) const;
        SwarmID & operator = (const SwarmID &source);
        /** Returns the type of transfer, FILE_TRANSFER or LIVE_TRANSFER */
        transfer_t      ttype() { return ttype_; }
        const Sha1Hash	&roothash() const { return roothash_; }
        const SwarmPubKey &spubkey() const { return spubkey_; }
        std::string     hex() const;
        std::string     tofilename() const;
        void		SetRootHash(const Sha1Hash &roothash) { ttype_ = FILE_TRANSFER; roothash_ = roothash; empty_=false;}

        const static SwarmID NOSWARMID;

      protected:
        bool		empty_; // if NOSWARMID
        transfer_t	ttype_;
        Sha1Hash	roothash_;
        SwarmPubKey	spubkey_;
    };



/** IPv4/6 address, just a nice wrapping around struct sockaddr_storage. */
    struct Address {
	struct sockaddr_storage  addr;
	Address();
	Address(const char* ip, uint16_t port);
	/**IPv4 address as "ip:port" or IPv6 address as "[ip]:port" following
	 * RFC2732, or just port in which case the address is set to in6addr_any */
	Address(const char* ip_port);
	Address(uint32_t ipv4addr, uint16_t port);
	Address(const struct sockaddr_storage& address) : addr(address) {}
	Address(struct in6_addr ipv6addr, uint16_t port);

	void set_ip   (const char* ip_str, int family);
	void set_port (uint16_t port);
	void set_port (const char* port_str);
	void set_ipv4 (uint32_t ipv4);
	void set_ipv4 (const char* ipv4_str);
	void set_ipv6 (const char* ip_str);
	void set_ipv6 (struct in6_addr &ipv6);
	void clear ();
	uint32_t ipv4() const;
	struct in6_addr ipv6() const;
	uint16_t port () const;
	operator sockaddr_storage () const {return addr;}
	bool operator == (const Address& b) const;
	std::string str () const;
	std::string ipstr (bool includeport=false) const;
	bool operator != (const Address& b) const { return !(*this==b); }
	bool is_private() const;
	int get_family() const { return addr.ss_family; }
	socklen_t get_family_sockaddr_length() const;
    };


// Arno, 2011-10-03: Use libevent callback functions, no on_error?
#define sockcb_t        event_callback_fn
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
    typedef std::deque< std::pair<tint,tint> > ttqueue;
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


    /** swift protocol message types; these are used on the wire. */
    typedef enum {
        SWIFT_HANDSHAKE = 0,
        SWIFT_DATA = 1,
        SWIFT_ACK = 2,
        SWIFT_HAVE = 3,
        SWIFT_INTEGRITY = 4,  // previously SWIFT_HASH
        SWIFT_PEX_RESv4 = 5,    // previously SWIFT_PEX_ADD
        SWIFT_PEX_REQ = 6,
        SWIFT_SIGNED_INTEGRITY = 7, // previously SWIFT_SIGNED_HASH
        SWIFT_REQUEST = 8,    // previously SWIFT_HINT
        SWIFT_CANCEL = 9,
        SWIFT_CHOKE = 10,
        // SWIFT_RANDOMIZE = 10, //FRAGRAND disabled
        SWIFT_UNCHOKE = 11,
        SWIFT_PEX_RESv6 = 12,
        SWIFT_PEX_REScert = 13,
        SWIFT_MESSAGE_COUNT = 14
    } messageid_t;

    typedef enum {
        DDIR_UPLOAD,
        DDIR_DOWNLOAD
    } data_direction_t;


    /** Arno: enum to indicate when to send an explicit close to the peer when
     * doing a local close.
     */
    typedef enum {
	CLOSE_DO_NOT_SEND,
	CLOSE_SEND,
	CLOSE_SEND_IF_ESTABLISHED,
    } close_send_t;


    typedef enum {
        VER_SWIFT_LEGACY=0, //legacy swift
        VER_PPSPP_v1=1      // IETF PPSPP compliant
    } popt_version_t;

    // Protocol options defined by IETF PPSPP
    typedef enum {
	POPT_VERSION = 0,
	POPT_MIN_VERSION = 1,
	POPT_SWARMID = 2,
	POPT_CONT_INT_PROT = 3,    // content integrity protection method
	POPT_MERKLE_HASH_FUNC = 4,
	POPT_LIVE_SIG_ALG = 5,
	POPT_CHUNK_ADDR = 6,
	POPT_LIVE_DISC_WND = 7,
	POPT_SUPP_MSGS = 8,
	POPT_END = 255
    } popt_t;

    typedef enum {
	POPT_CONT_INT_PROT_NONE = 0,
	POPT_CONT_INT_PROT_MERKLE = 1,
	POPT_CONT_INT_PROT_SIGNALL = 2,
	POPT_CONT_INT_PROT_UNIFIED_MERKLE = 3
    } popt_cont_int_prot_t;

    typedef enum {
	POPT_MERKLE_HASH_FUNC_SHA1 = 0,
	POPT_MERKLE_HASH_FUNC_SHA224 = 1,
	POPT_MERKLE_HASH_FUNC_SHA256 = 2,
	POPT_MERKLE_HASH_FUNC_SHA384 = 3,
	POPT_MERKLE_HASH_FUNC_SHA512 = 4
    } popt_merkle_func_t;

    typedef enum {
	POPT_CHUNK_ADDR_BIN32 = 0,
	POPT_CHUNK_ADDR_BYTE64 = 1,
	POPT_CHUNK_ADDR_CHUNK32 = 2,
	POPT_CHUNK_ADDR_BIN64 = 3,
	POPT_CHUNK_ADDR_CHUNK64 = 4
    } popt_chunk_addr_t;

    // popt_live_sig_alg_t: See livesig.h


    class Handshake
    {
      public:
#if ENABLE_IETF_PPSP_VERSION == 1
	Handshake() : version_(VER_PPSPP_v1), min_version_(VER_PPSPP_v1), merkle_func_(POPT_MERKLE_HASH_FUNC_SHA1), live_sig_alg_(DEFAULT_LIVE_SIG_ALG), chunk_addr_(POPT_CHUNK_ADDR_CHUNK32), live_disc_wnd_(POPT_LIVE_DISC_WND_ALL), swarm_id_ptr_(NULL) {}
#else
	Handshake() : version_(VER_SWIFT_LEGACY), min_version_(VER_SWIFT_LEGACY), merkle_func_(POPT_MERKLE_HASH_FUNC_SHA1), live_sig_alg_(DEFAULT_LIVE_SIG_ALG), chunk_addr_(POPT_CHUNK_ADDR_BIN32), live_disc_wnd_(POPT_LIVE_DISC_WND_ALL), swarm_id_ptr_(NULL) {}
#endif
	Handshake(Handshake &c)
	{
	    version_ = c.version_;
	    min_version_ = c.min_version_;
	    cont_int_prot_ = c.cont_int_prot_;
	    merkle_func_ = c.merkle_func_;
	    live_sig_alg_ = c.live_sig_alg_;
	    chunk_addr_ = c.chunk_addr_;
	    live_disc_wnd_ = c.live_disc_wnd_;
            if (c.swarm_id_ptr_ == NULL)
                swarm_id_ptr_ = NULL;
            else
                swarm_id_ptr_ = new SwarmID(*(c.swarm_id_ptr_));

	}
	~Handshake() { ReleaseSwarmID(); }
	void SetSwarmID(SwarmID &swarmid) { swarm_id_ptr_ = new SwarmID(swarmid); }
	SwarmID GetSwarmID() { return (swarm_id_ptr_ == NULL) ? SwarmID::NOSWARMID : *swarm_id_ptr_; }
	void ReleaseSwarmID() { if (swarm_id_ptr_ != NULL) delete swarm_id_ptr_; swarm_id_ptr_ = NULL; }
	bool IsSupported()
	{
	    if (cont_int_prot_ == POPT_CONT_INT_PROT_SIGNALL)
		return false; // PPSPTODO
	    else if (merkle_func_ >= POPT_MERKLE_HASH_FUNC_SHA224)
		return false; // PPSPTODO
	    else if (chunk_addr_ == POPT_CHUNK_ADDR_BYTE64 || chunk_addr_ == POPT_CHUNK_ADDR_BIN64 || chunk_addr_ == POPT_CHUNK_ADDR_CHUNK64)
		return false; // PPSPTODO
	    else if (!(live_sig_alg_ == POPT_LIVE_SIG_ALG_RSASHA1 || live_sig_alg_ == POPT_LIVE_SIG_ALG_ECDSAP256SHA256 || live_sig_alg_ == POPT_LIVE_SIG_ALG_ECDSAP384SHA384))
		return false; // PPSPTODO
	    return true;
	}
	void ResetToLegacy()
	{
	    // Do not reset peer_channel_id
	    version_ = VER_SWIFT_LEGACY;
	    min_version_ = VER_SWIFT_LEGACY;
	    cont_int_prot_ = POPT_CONT_INT_PROT_MERKLE;
	    merkle_func_ = POPT_MERKLE_HASH_FUNC_SHA1;
	    live_sig_alg_ = POPT_LIVE_SIG_ALG_PRIVATEDNS;
	    chunk_addr_ = POPT_CHUNK_ADDR_BIN32;
	    live_disc_wnd_ = (uint32_t)POPT_LIVE_DISC_WND_ALL;
	}

	/**    Peer channel id; zero if we are trying to open a channel. */
	uint32_t    		peer_channel_id_;
	popt_version_t   	version_;
	popt_version_t   	min_version_;
	popt_cont_int_prot_t  	cont_int_prot_;
	popt_merkle_func_t	merkle_func_;
	popt_live_sig_alg_t	live_sig_alg_;
	popt_chunk_addr_t	chunk_addr_;
	uint64_t		live_disc_wnd_;
      protected:
	/** Dynamically allocated such that we can deallocate it and
	 * save some bytes per channel */
	SwarmID 		*swarm_id_ptr_;
    };

    /** Arno, 2013-09-25: Currently just used for URI processing.
     * Could be used as args to Open and LiveOpen in future.
     */
    class SwarmMeta
    {
      public:
	SwarmMeta() : version_(VER_PPSPP_v1), min_version_(VER_PPSPP_v1), cont_int_prot_(POPT_CONT_INT_PROT_MERKLE), merkle_func_(POPT_MERKLE_HASH_FUNC_SHA1),  live_sig_alg_(DEFAULT_LIVE_SIG_ALG), chunk_addr_(POPT_CHUNK_ADDR_CHUNK32), live_disc_wnd_(POPT_LIVE_DISC_WND_ALL), injector_addr_(), chunk_size_(SWIFT_DEFAULT_CHUNK_SIZE), cont_dur_(0), cont_len_(0), ext_tracker_url_(""), mime_type_("")
	{
	}
	popt_version_t   	version_;
	popt_version_t   	min_version_;
	popt_cont_int_prot_t  	cont_int_prot_;
	popt_merkle_func_t	merkle_func_;
	popt_live_sig_alg_t	live_sig_alg_; // UNUSED
	popt_chunk_addr_t	chunk_addr_;
	uint64_t		live_disc_wnd_;
	Address			injector_addr_;
	uint32_t		chunk_size_;
	int32_t			cont_dur_;
	uint64_t		cont_len_;
	std::string		ext_tracker_url_;
	std::string		mime_type_;
    };

    typedef std::pair<std::string,std::string> stringpair;
    typedef std::map<std::string,std::string>  parseduri_t;
    bool ParseURI(std::string uri,parseduri_t &map);
    std::string URIToSwarmMeta(parseduri_t &map, SwarmMeta *sm);

    class PiecePicker;
    //class CongestionController; // Arno: Currently part of Channel. See ::NextSendTime()
    class Channel;
    typedef std::vector<Channel *>	channels_t;
    typedef void (*ProgressCallback) (int td, bin_t bin);
    typedef std::pair<ProgressCallback,uint8_t>	progcallbackreg_t;
    typedef std::vector<progcallbackreg_t> progcallbackregs_t;
    typedef std::vector<int>		tdlist_t;
    class Storage;

    /*
     * Superclass for live and video-on-demand
     */
    class ContentTransfer : public Operational {

      public:
        ContentTransfer(transfer_t ttype);
        virtual ~ContentTransfer();

        /** Returns the type of transfer, FILE_TRANSFER or LIVE_TRANSFER */
        transfer_t      ttype() { return ttype_; }

        // Overridable methods
        /** Returns the global ID for this transfer */
        virtual SwarmID&     swarm_id() = 0;
        /** The binmap pointer for data already retrieved and checked. */
        virtual binmap_t *  ack_out() = 0;
        /** Returns the number of bytes in a chunk for this transfer */
        virtual uint32_t chunk_size() = 0;
	/** Integrity protection via Hash tree */
        HashTree *      hashtree() { return hashtree_; }
        /** Check whether all components still in working state */
	virtual void 	UpdateOperational() = 0;

        /** Piece picking strategy used by this transfer. */
        PiecePicker *   picker() { return picker_; }
        /** Returns the local ID for this transfer. */
        int             td() const { return td_; }
        /** Sets the ID for this transfer post create (used by SwarmManager) */
        void		SetTD(int td);
        // Gertjan fix: return bool
        bool            OnPexIn(const Address& addr);
        // Gertjan
        Channel *       RandomChannel(Channel *notc);
        /** Arno: Return the Channel to peer "addr" that is not equal to "notc". */
        Channel *       FindChannel(const Address &addr, Channel *notc);
        void            CloseChannels(channels_t delset, bool isall); // do not pass by reference
        void            GarbageCollectChannels();

        // RATELIMIT
        /** Arno: Call when n bytes are received. */
        void            OnRecvData(int n);
        /** Arno: Call when n bytes are sent. */
        void            OnSendData(int n);
        /** Arno: Call when no bytes are sent due to rate limiting. */
        void            OnSendNoData();
        /** Ric:  Call when no bytes are received. */
        void            OnRecvNoData();
        /** Arno: Return current speed for the given direction in bytes/s */
        double          GetCurrentSpeed(data_direction_t ddir);
        /** Arno: Return maximum speed for the given direction in bytes/s */
        double          GetMaxSpeed(data_direction_t ddir);
        /** Arno: Set maximum speed for the given direction in bytes/s */
        void            SetMaxSpeed(data_direction_t ddir, double m);
        /** Arno: Return the number of non-seeders current channeled with. */
        uint32_t        GetNumLeechers();
        /** Arno: Return the number of seeders current channeled with. */
        uint32_t        GetNumSeeders();

        /** Arno: Return (pointer to) the list of Channels for this transfer. MORESTATS */
        channels_t *	GetChannels() { return &mychannels_; }
        /** Arno: Return the list of callbacks for this transfer */
        progcallbackregs_t  GetProgressCallbackRegistrations() { return callbacks_; }

        // MULTIFILE
        Storage *       GetStorage() { return storage_; }

        /** Add a peer to the set of addresses to connect to */
        void            AddPeer(Address &peer);
        void		SetDefaultHandshake(Handshake &default_hs_out) { def_hs_out_ = default_hs_out; }
        Handshake &	GetDefaultHandshake(){ return def_hs_out_; }

        /** Ric: add number of hints for slow start scenario */
        void            SetSlowStartHints(uint32_t hints) { slow_start_hints_ += hints; }
        /** Ric: get the # of slow start hints */
        uint32_t        GetSlowStartHints() { return slow_start_hints_; }

        /** Arno: set the tracker for this transfer. Reseting it won't kill
         * any existing connections. */
        void            SetTracker(std::string trackerurl) { trackerurl_ = trackerurl; }
        /** Arno: (Re)Connect to tracker for this transfer, or global Channel::trackerurl if not set */
        void            ConnectToTracker(bool stop=false);
        /** Arno: Reconnect to the tracker if no established peers or is connected
         * to a live source that went silent and exp backoff allows it. */
        void            ReConnectToTrackerIfAllowed(bool movingforward);
        ExternalTrackerClient *GetExternalTrackerClient() { return ext_tracker_client_; }

        /** Progress callback management **/
        void 		AddProgressCallback(ProgressCallback cb, uint8_t agg);
        void 		RemoveProgressCallback(ProgressCallback cb);
        void 		Progress(bin_t bin);  /** Called by channels when data comes in */

        /** Arno: Callback to do maintenance for all transfers */
        static void     LibeventGlobalCleanCallback(int fd, short event, void *arg);
        static struct event evclean; // Global for all Transfers
        static uint64_t	cleancounter;

      protected:
        transfer_t      ttype_;
        SwarmID 	swarm_id_;
        int             td_;	// transfer descriptor as used by swift API.
        Handshake 	def_hs_out_;

        /** Channels working for this transfer. */
        channels_t    	mychannels_;

        /** Progress callback management **/
        progcallbackregs_t callbacks_;

        /** Piece picker strategy. */
        PiecePicker*    picker_;

        // RATELIMIT
        MovingAverageSpeed    cur_speed_[2];
        double          max_speed_[2];
        uint32_t        speedupcount_;
        uint32_t        speeddwcount_;
        // MULTIFILE
        Storage         *storage_;

        /** HashTree for transfer (either MmapHashTree, ZeroHashTree, LiveHashTree or NULL) */
        HashTree*       hashtree_;

        std::string     trackerurl_; // Tracker URL for this transfer
        tint            tracker_retry_interval_;
        tint            tracker_retry_time_;
        ExternalTrackerClient *ext_tracker_client_; // if external tracker
        // Ric: slow start 4 requesting hints
        uint32_t        slow_start_hints_;

    };


    /** A class representing a file/VOD transfer of one or multiple files */
    class    FileTransfer : public ContentTransfer {

      public:
        /** A constructor. Open/submit/retrieve a file.
         *  @param file_name    the name of the file
         *  @param root_hash    the root hash of the file; zero hash if the file
	 *                      is newly submitted
	 *  @param force_check_diskvshash	whether to force a check of disk versus hashes
	 *  @param check_netwvshash	whether to hash check chunk on receipt
	 *  @param chunk_size	size of chunk to use
	 *  @param zerostate	whether to serve the hashes + content directly from disk
	 */
        FileTransfer(int td, std::string file_name, const Sha1Hash& root_hash=Sha1Hash::ZERO, bool force_check_diskvshash=true, popt_cont_int_prot_t cipm=POPT_CONT_INT_PROT_MERKLE, uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE, bool zerostate=false);
        /**    Close everything. */
        ~FileTransfer();

        // ContentTransfer overrides

        SwarmID& swarm_id() { swarm_id_.SetRootHash(hashtree_->root_hash()); return swarm_id_; }
        /** The binmap pointer for data already retrieved and checked. */
        binmap_t *      ack_out ()  { return hashtree_->ack_out(); }
        /** Piece picking strategy used by this transfer. */
        uint32_t 	chunk_size() { return hashtree_->chunk_size(); }
	/** Check whether all components still in working state */
	void 		UpdateOperational();

	// FileTransfer specific methods

        /** Ric: the availability in the swarm */
        Availability*   availability() { return availability_; }
        //ZEROSTATE
        /** Returns whether this FileTransfer is running in zero-state mode,
         * meaning that the hash tree is not mmapped into memory but read
         * directly from disk, and other memory saving measures.
         */
        bool 		IsZeroState() { return zerostate_; }

      protected:
        // Ric: PPPLUG
        /** Availability in the swarm */
        Availability*   availability_;

        //ZEROSTATE
        bool            zerostate_;
    };

    /** A class representing a live transfer. */
    class    LiveTransfer : public ContentTransfer {
      public:

        /** A constructor for a live source. */
	LiveTransfer(std::string filename, KeyPair &keypair, std::string checkpoint_filename, popt_cont_int_prot_t cipm, uint64_t disc_wnd=POPT_LIVE_DISC_WND_ALL, uint32_t nchunks_per_sign=SWIFT_DEFAULT_LIVE_NCHUNKS_PER_SIGN, uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE);

	/** A constructor for live client. */
	LiveTransfer(std::string filename, SwarmID &swarmid, Address &srcaddr, popt_cont_int_prot_t cipm, uint64_t disc_wnd=POPT_LIVE_DISC_WND_ALL, uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE);

        /**  Close everything. */
        ~LiveTransfer();

        // ContentTransfer overrides

        SwarmID&  swarm_id() { return swarm_id_; }
        /** The binmap for data already retrieved and checked. */
        binmap_t *      ack_out ();
        /** Returns the number of bytes in a chunk for this transmission */
        uint32_t        chunk_size() { return chunk_size_; }
	/** Check whether all components still in working state */
	void 		UpdateOperational();

	// LiveTransfer specific methods

        /** Returns the number of bytes that are complete sequentially, starting from the
             hookin offset, till the first not-yet-retrieved packet. */
        uint64_t        SeqComplete();

        /** Returns whether this transfer is the source */
        bool            am_source() { return am_source_; }

        /** Source: add a chunk to the swarm */
        int             AddData(const void *buf, uint32_t nbyte);

        /** Source: announce only chunks under signed munros */
        void            UpdateSignedAckOut();


        /** Returns the byte offset at which we hooked into the live stream */
        uint64_t        GetHookinOffset();

        /** Source: Returns current write position in storage file */
        uint64_t        GetSourceWriteOffset() { return offset_; }

        // SIGNPEAK
        /** Source: Return all chunks in ack_out_ covered by peaks */
        binmap_t *      ack_out_signed();

        /** Received a correctly signed munro hash with timestamp sourcet */
        void 		OnVerifiedMunroHash(bin_t munro, tint sourcet);

        /** If live discard window is used, purge unused parts of tree.
         * pos is last received chunk. */
        void            OnDataPruneTree(Handshake &hs_out, bin_t pos, uint32_t nchunks2forget);

        // Arno: FileTransfers are managed by the SwarmManager which
        // activates/deactivates them as required. LiveTransfers are unmanaged.
        /** Find transfer by the transfer descriptor. */
        static LiveTransfer* FindByTD(int td);
        /** Find transfer by the swarm ID. */
        static LiveTransfer* FindBySwarmID(const SwarmID& swarmid);
        /** Return list of transfer descriptors of all LiveTransfers */
        static tdlist_t GetTransferDescriptors();
        /** Add this LiveTransfer to the global list */
        void GlobalAdd();
        /** Remove this LiveTransfer for the global list */
        void GlobalDel();

        // LIVECHECKPOINT
        int WriteCheckpoint(BinHashSigTuple &roottup);
        BinHashSigTuple ReadCheckpoint();

        /** Source: returns current last_chunkid_ as bin */
        bin_t 		GetSourceCurrentPos();

        /** Client: return source address */
        Address		GetSourceAddress() { return srcaddr_; }

      protected:
        /**    Binmap of own chunk availability, when not POPT_CONT_INT_PROT_UNIFIED_MERKLE (so _NONE or _SIGNALL) */
        binmap_t        ack_out_;
        /**    Binmap of own chunk availability restricted to current signed peaks SIGNPEAK */
        binmap_t	signed_ack_out_;
        /**    Bin of right-most received chunk LIVE */
        bin_t		ack_out_right_basebin_; // FUTURE: make part of binmap_t?
        // CHUNKSIZE
        /** Arno: configurable fixed chunk size in bytes */
       uint32_t         chunk_size_;

        /** Source: Am I a source */
        bool            am_source_;
        /** Name of file used for storing live chunks */
        std::string     filename_;
        /** Source: ID of last generated chunk */
        uint64_t        last_chunkid_;
        /** Source: Current write position in storage file */
        uint64_t        offset_;

        /** Source: Count of chunks generated since last signed peak epoch */
        uint32_t        chunks_since_sign_;

        // LIVECHECKPOINT
	/** Filename to store source checkpoint */
        std::string checkpoint_filename_;
        /** bin of old tree, which becomes new peak but must announced as
         * being in possession to others */
        bin_t	    checkpoint_bin_;

        /** Client: Source address for chunk picker and protocol optimization */
        Address		srcaddr_;

        /** Arno: global list of LiveTransfers, which are not managed via SwarmManager */
        static std::vector<LiveTransfer*> liveswarms;

        /** Joint constructor code between source and client */
        void Initialize(KeyPair &keypair, popt_cont_int_prot_t cipm, uint64_t disc_wnd,uint32_t nchunks_per_sign);
    };


    /** PiecePicker implements some strategy of choosing (picking) what
        to request next, given the possible range of choices:
        data acknowledged by the peer minus data already retrieved.
        May pick sequentially, do rarest first or in some other way. */
    class PiecePicker {
      public:
        virtual void    Randomize (uint64_t twist) = 0;
        /** The piece picking method itself.
         *  @param  offered     the data acknowledged by the peer
         *  @param  max_width   maximum number of packets to ask for
         *  @param  expires     (not used currently) when to consider request expired
         *  @return             the bin number to request */
        virtual bin_t   Pick (binmap_t& offered, uint64_t max_width, tint expires, uint32_t channelid) = 0;
        virtual void    LimitRange (bin_t range) = 0;
        /** updates the playback position for streaming piece picking.
         *  @param  offbin        bin number of new playback pos
         *  @param  whence      only SEEK_CUR supported */
        virtual int     Seek(bin_t offbin, int whence) = 0;
        virtual         ~PiecePicker() {}
    };

    class LivePiecePicker : public PiecePicker {
      public:
	/** Arno: Register the last munro sent by a peer, to be able to choose
	 * a hook-in point. */
        virtual void    AddPeerMunro(bin_t munro, tint sourcet) = 0;
        /** Returns the bin at which we hooked into the live stream. */
        virtual bin_t   GetHookinPos() = 0;
        /** Returns the bin in the live stream we currently want to download. */
        virtual bin_t   GetCurrentPos() = 0;
    };


    /**    swift channel's "control block"; channels loosely correspond to TCP
       connections or FTP sessions; one channel is created for one file
       being transferred between two peers. As we don't need buffers and
       lots of other TCP stuff, sizeof(Channel+members) must be below 1K.
       Normally, API users do not deal with this class. */
    class Channel {

      public:
        Channel( ContentTransfer* transfer, int socket=INVALID_SOCKET, Address peer=Address());
        ~Channel();

        typedef enum {
            KEEP_ALIVE_CONTROL,
            PING_PONG_CONTROL,
            SLOW_START_CONTROL,
            AIMD_CONTROL,
            LEDBAT_CONTROL,
            CLOSE_CONTROL
        } send_control_t;

#define DGRAM_MAX_SOCK_OPEN 128
        static int sock_count;
        static sckrwecb_t sock_open[DGRAM_MAX_SOCK_OPEN];
        static std::string  trackerurl; // Global tracker for all transfers
        static struct event_base *evbase;
        static struct event evrecv;
        static const char* SEND_CONTROL_MODES[];

        static tint     epoch, start;
        static uint64_t global_dgrams_up, global_dgrams_down, global_raw_bytes_up, global_raw_bytes_down, global_bytes_up, global_bytes_down;
        static void 	CloseChannelByAddress(const Address &addr);

        // SOCKMGMT
        // Arno: channel is also a "singleton" class that manages all sockets
        // for a swift process
        static void     LibeventSendCallback(int fd, short event, void *arg);
        static void     LibeventReceiveCallback(int fd, short event, void *arg);
        static void     RecvDatagram (evutil_socket_t socket); // Called by LibeventReceiveCallback
        static int      RecvFrom(evutil_socket_t sock, Address& addr, struct evbuffer *evb); // Called by RecvDatagram
        static int      SendTo(evutil_socket_t sock, const Address& addr, struct evbuffer *evb); // Called by Channel::Send()
        static evutil_socket_t Bind(Address address, sckrwecb_t callbacks=sckrwecb_t());
        static Address  BoundAddress(evutil_socket_t sock);
        static evutil_socket_t default_socket()
            { return sock_count ? sock_open[0].sock : INVALID_SOCKET; }

        /** close the port */
        static void     CloseSocket(evutil_socket_t sock);
        static void     Shutdown();
        /** the current time */
        static tint     Time();
        static tint 	last_tick;
        // Arno: send explicit close outside Channel context
        static void 	StaticSendClose(evutil_socket_t socket,Address &addr, uint32_t peer_channel_id);

        // Ric: used for testing LEDBAT's behaviour
        float		GetCwnd() { return cwnd_; }
        uint64_t 	GetHintSize(data_direction_t ddir) { return ddir ? hint_out_size_ : hint_in_size_; }
        bool 		Totest;
        bool 		Tocancel;

        // Arno: Per instance methods
        void        Recv (struct evbuffer *evb);
        void        Send ();  // Called by LibeventSendCallback
        void        Close (close_send_t closesend);
        void        ClearTransfer() { transfer_ = NULL; } // for swarm cleanup

        void        OnAck (struct evbuffer *evb);
        void        OnHave (struct evbuffer *evb);
        void 	    OnHaveLive(bin_t ackd_pos);
        bin_t       OnData (struct evbuffer *evb);
        void        OnHint (struct evbuffer *evb);
        void        OnHash (struct evbuffer *evb);
        void        OnPexAdd(struct evbuffer *evb, int family);
        void        OnPexAddCert (struct evbuffer *evb);
        static Handshake *StaticOnHandshake( Address &addr, uint32_t cid, bool ver_known, popt_version_t ver, struct evbuffer *evb);
        void        OnHandshake (Handshake *hishs);
        void        OnCancel(struct evbuffer *evb); // PPSP
        void        OnChoke(struct evbuffer *evb);
        void        OnUnchoke(struct evbuffer *evb);
        void        OnSignedHash (struct evbuffer *evb);
        void        AddHandshake (struct evbuffer *evb);
        bin_t       AddData (struct evbuffer *evb);
        void 	    SendIfTooBig(struct evbuffer *evb);
        void        AddAck (struct evbuffer *evb);
        void        AddHave (struct evbuffer *evb);
        void        AddHint (struct evbuffer *evb);
        void        AddCancel (struct evbuffer *evb);
        void        AddRequiredHashes (struct evbuffer *evb, bin_t pos, bool isretransmit);
        void        AddUnsignedPeakHashes (struct evbuffer *evb);
        void        AddFileUncleHashes (struct evbuffer *evb, bin_t pos);
        void        AddLiveSignedMunroHash(struct evbuffer *evb,bin_t munro); // SIGNMUNRO
        void        AddLiveUncleHashes (struct evbuffer *evb, bin_t pos, bin_t munro, bool isretransmit); // SIGNMUNRO
        void        AddPex (struct evbuffer *evb);
        void        OnPexReq(void);
        void        AddPexReq(struct evbuffer *evb);
        void        BackOffOnLosses (float ratio=0.5);
        tint        SwitchSendControl (send_control_t control_mode);
        tint        NextSendTime ();
        tint        KeepAliveNextSendTime ();
        tint        PingPongNextSendTime ();
        tint        CwndRateNextSendTime ();
        tint        SlowStartNextSendTime ();
        tint        AimdNextSendTime ();
        tint        LedbatNextSendTime ();
        /** Arno: return true if this peer has complete file. May be fuzzy if Peak Hashes not in */
        bool        IsComplete();
        /** Arno: return (UDP) port for this channel */
        uint16_t    GetMyPort();
        bool        IsDiffSenderOrDuplicate(Address addr, uint32_t chid);

        static int  MAX_REORDERING;
        static tint TIMEOUT;
        static tint MIN_DEV;
        static tint MAX_SEND_INTERVAL;
        static tint LEDBAT_TARGET;
        static float LEDBAT_GAIN;
        static tint LEDBAT_DELAY_BIN;
        static uint32_t LEDBAT_BASE_HISTORY;
        static uint32_t LEDBAT_ROLLOVER;
        static bool SELF_CONN_OK;
        static tint MAX_POSSIBLE_RTT;
        static tint MIN_PEX_REQUEST_INTERVAL;
        static FILE* debug_file;
        // Only in devel: file used to debug LEDBAT
        static FILE* debug_ledbat;

        const std::string id_string () const;
        /** A channel is "established" if had already sent and received packets. */
        bool        is_established ();
        HashTree *  hashtree();
        ContentTransfer *transfer() { return transfer_; }
        const Address& peer() const { return peer_; }
        const Address& recv_peer() const { return recv_peer_; }
        tint 	    ack_timeout () {
            tint dev = dev_avg_ < MIN_DEV ? MIN_DEV : dev_avg_;
            tint tmo = rtt_avg_ + dev * 4;
            return tmo < 30*TINT_SEC ? tmo : 30*TINT_SEC;
        }
        uint32_t    id () const { return id_; }
        const binmap_t& ack_in() const { return ack_in_; }

        // MORESTATS
        uint64_t    raw_bytes_up() { return raw_bytes_up_; }
        uint64_t    raw_bytes_down() { return raw_bytes_down_; }
        uint64_t    bytes_up() { return bytes_up_; }
        uint64_t    bytes_down() { return bytes_down_; }

        static int  DecodeID(int scrambled);
        static int  EncodeID(int unscrambled);
        static Channel* channel(int i) {
            return i<channels.size()?channels[i]:NULL;
        }

        // SAFECLOSE
        void        ClearEvents();
        void        Schedule4Delete() { scheduled4del_ = true; }
        bool        IsScheduled4Delete() { return scheduled4del_; }

        //ZEROSTATE
        // Message handler replacements
        void 	    OnDataZeroState(struct evbuffer *evb);
        void        OnHaveZeroState(struct evbuffer *evb);
        void        OnHashZeroState(struct evbuffer *evb);
        void        OnPexAddZeroState(struct evbuffer *evb, int family);
        void        OnPexAddCertZeroState(struct evbuffer *evb);
        void        OnPexReqZeroState(struct evbuffer *evb);
        tint        GetOpenTime() { return open_time_; }

        // LIVE
        /** Arno: Called when source generates chunk. */
        void        LiveSend();
        bool        PeerIsSource();
        tint	    GetLastRecvTime() { return last_recv_time_; }

        void 	    CloseOnError();

        // MOVINGFWD
        /** Whether or not channel is uploading when seeder, or downloading when leecher */
        bool        IsMovingForward();

      protected:
        struct event    *evsend_ptr_; // Arno: timer per channel // SAFECLOSE
        //LIVE
        struct event    *evsendlive_ptr_; // Arno: timer per channel

        /** Channel id: index in the channel array. */
        uint32_t    id_;
        /**    Socket address of the peer. */
        Address     peer_;
        /**    The UDP socket fd. */
        evutil_socket_t      socket_;
        /**    Descriptor of the file in question. */
        ContentTransfer*    transfer_;
        bool        own_id_mentioned_;
        /**    Peer's progress, based on acknowledgements. */
        binmap_t    ack_in_;
        /**    Bin of right-most acked chunk LIVE */
        bin_t	    ack_in_right_basebin_; // FUTURE: make part of binmap_t?

        /**    Last data received; needs to be acked immediately. */
        tintbin     data_in_;
        bin_t       data_in_dbl_;
        /** The history of data sent and still unacknowledged. */
        tbqueue     data_out_;
        uint32_t    data_out_size_; // pkts not acknowledged
        /** Timeouted data (potentially to be retransmitted). */
        tbqueue     data_out_tmo_;
        bin_t       data_out_cap_; // Ric: maybe we should remove it.. creates problems if lost
        /** Index in the history array. */
        binmap_t    have_out_;
        /**    Transmit schedule: in most cases filled with the peer's hints */
        tbqueue     hint_in_;
        uint64_t    hint_in_size_;
        /** Hints sent (to detect and reschedule ignored hints). */
        tbqueue     hint_out_;
        uint64_t    hint_out_size_;
        /** Hints queued to be sent. */
		tbqueue     hint_queue_out_;
		uint64_t    hint_queue_out_size_;
        /** Ric: hints that are removed from the hint_out_ queue and need to be canceled */
		std::deque<bin_t> cancel_out_;
        /** Types of messages the peer accepts. */
        uint64_t    cap_in_;
        /** PEX progress */
        bool        pex_requested_;
        tint        last_pex_request_time_;
        tint        next_pex_request_time_;
        bool        pex_request_outstanding_;
        tbqueue     reverse_pex_out_;        // Arno, 2011-10-03: should really be a queue of (tint,channel id(= uint32_t)) pairs.
        int         useless_pex_count_;
        /** Smoothed averages for RTT, RTT deviation and data interarrival periods. */
        tint        rtt_avg_, dev_avg_, dip_avg_;
        tint        last_send_time_;
        tint        last_recv_time_;
        tint        last_data_out_time_;
        tint        last_data_in_time_;
        tint        last_loss_time_;
        tint        next_send_time_;
        tint	    open_time_;
        /** Congestion window; TODO: int, bytes. */
        float       cwnd_;
        int         cwnd_count1_;
        /** Data sending interval. */
        tint        send_interval_;
        /** The congestion control strategy. */
        send_control_t         send_control_;
        /** Datagrams (not data) sent since last recv.    */
        int         sent_since_recv_;

        /** Arno: Fix for KEEP_ALIVE_CONTROL */
        bool        lastrecvwaskeepalive_;
        bool        lastsendwaskeepalive_;
        /** Arno: For live, we may receive a HAVE but have no hints
            outstanding. In that case we should not wait till next_send_time_
            but request directly. See send_control.cpp */
        bool	    live_have_no_hint_;

        /** Recent acknowlegements for data previously sent. */
        int         ack_rcvd_recent_; // Arno, 2013-07-01: appears broken at the moment
        /** Recent non-acknowlegements (losses) of data previously sent.    */
        int         ack_not_rcvd_recent_;
        /** LEDBAT one-way delay machinery */
        tint        owd_min_bins_[10];
        int         owd_min_bin_;
        tint        owd_min_bin_start_;
        tint        owd_cur_;
        tint        owd_min_;
        /** LEDBAT current delay list should be > 4 && == RTT */
        ttqueue     owd_current_;
        /** Stats */
        int         dgrams_sent_;
        int         dgrams_rcvd_;
        // Arno, 2011-11-28: for detailed, per-peer stats. MORESTATS
        uint64_t    raw_bytes_up_, raw_bytes_down_, bytes_up_, bytes_down_;
        uint64_t    old_movingfwd_bytes_;

        // SAFECLOSE
        bool        scheduled4del_;
        /** Arno: Socket address of the peer where packets are received from,
         * when an IANA private address, otherwise 0.
         * May not be equal to peer_. 2PEERSBEHINDSAMENAT */
        Address     recv_peer_;

        // keep memory of previous delays
        bool        direct_sending_;
        tint        timer_delay_;
        tint        reschedule_delay_;

        // PPSP
        /** Handshake I sent to peer. swarmid not set. */
        Handshake   *hs_out_;
        /** Handshake I got from peer. */
        Handshake   *hs_in_;

        // SIGNMUNRO
        bin_t	    last_sent_munro_;
        bool 	    munro_ack_rcvd_;

        // RTTCS
        tintbin	    rtt_hint_tintbin_;

        int         PeerBPS() const { return TINT_SEC / dip_avg_ * 1024; }
        /** Get a request for one packet from the queue of peer's requests. */
        bin_t       DequeueHint(bool *retransmitptr);
        bin_t       ImposeHint();
        void        TimeoutDataOut ();
        void        CleanStaleHintOut();
        void        CleanHintOut(bin_t pos);
        void        Reschedule();
        void        UpdateDIP(bin_t pos); // RETRANSMIT
        void        UpdateRTT(int32_t pos, tbqueue data_out, tint owd);

        bin_t       DequeueHintOut(uint64_t size);

        // Arno, 2012-06-14: Replace with hashtable (unsorted_map). This
        // currently grows for ever, filling with NULLs for old channels
        // and results in channel IDs with are not really random.
        //
        static channels_t channels;
    };


    // MULTIFILE
    /*
     * Class representing a single file in a multi-file swarm.
     */
    class StorageFile : public Operational
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
         int64_t     start_;
         int64_t     end_;

         int         fd_; // actual fd
    };

    typedef std::vector<StorageFile *>    storage_files_t;

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
    class Storage : public Operational {

      public:

        static const std::string MULTIFILE_PATHNAME;
        static const std::string MULTIFILE_PATHNAME_FILE_SEP;
        static const int         MULTIFILE_MAX_PATH = 2048;
        static const int         MULTIFILE_MAX_LINE = MULTIFILE_MAX_PATH+1+32+1;

        typedef enum {
            STOR_STATE_INIT,
            STOR_STATE_MFSPEC_SIZE_KNOWN,
            STOR_STATE_MFSPEC_COMPLETE,
            STOR_STATE_SINGLE_FILE,
            STOR_STATE_SINGLE_LIVE_WRAP  // single file containing just live discard window
        } storage_state_t;

        /** StorageFile for every file in this transfer */
        typedef std::vector<StorageFile *>    storage_files_t;

        /** convert multi-file spec filename (UTF-8 encoded Unicode) to OS name and vv. */
        static std::string spec2ospn(std::string specpn);
        static std::string os2specpn(std::string ospn);

        /** Create Storage from specified path and destination dir if content turns about to be a multi-file.
         * If live_disc_wnd_bytes !=0 then live single-file, wrapping if != POPT_LIVE_DISC_WND_ALL */
        Storage(std::string ospathname, std::string destdir, int td, uint64_t live_disc_wnd_bytes);
        ~Storage();

        /** UNIX pread approximation. Does change file pointer. Thread-safe if no concurrent writes */
        ssize_t     Read(void *buf, size_t nbyte, int64_t offset); // off_t not 64-bit dynamically on Win32

        /** UNIX pwrite approximation. Does change file pointer. Is not thread-safe */
        ssize_t     Write(const void *buf, size_t nbyte, int64_t offset);

        /** Link to HashTree */
        void        SetHashTree(HashTree *ht) { ht_ = ht; }

        /** Size of content according to multi-file spec, -1 if unknown or single file */
        int64_t     GetSizeFromSpec();

        /** Size reserved for storage */
        int64_t     GetReservedSize();

        /** 0 for single file, spec size for multi-file */
        int64_t     GetMinimalReservedSize();

        /** Change size reserved for storage */
        int         ResizeReserved(int64_t size);

        /** Return the operating system path for this Storage */
        std::string GetOSPathName() { return os_pathname_; }

        /** Return the root hash of the content being stored */
        std::string roothashhex() { if (ht_ == NULL) return "0000000000000000000000000000000000000000"; else return ht_->root_hash().hex(); }

        /** Return the destination directory for this Storage */
        std::string GetDestDir() { return destdir_; }

        /** Whether Storage is ready to be used */
        bool        IsReady() { return state_ == STOR_STATE_SINGLE_FILE || STOR_STATE_SINGLE_LIVE_WRAP || state_ == STOR_STATE_MFSPEC_COMPLETE; }

        /** Return the list of StorageFiles for this Storage, empty if not multi-file */
        storage_files_t    GetStorageFiles() { return sfs_; }

        /** Return a one-time callback when swift starts allocating disk space */
        void AddOneTimeAllocationCallback(ProgressCallback cb) { alloc_cb_ = cb; }

        /** Sets the transfer descriptor for this storage obj post create (used by SwarmManager) */
        void SetTD(int td) { td_ = td; }


      protected:
        storage_state_t    state_;

        std::string os_pathname_;
        std::string destdir_;

        /** HashTree this Storage is linked to */
        HashTree    *ht_;

        int64_t     spec_size_;

        storage_files_t    sfs_;
        int         single_fd_;
        int64_t     reserved_size_;
        int64_t     total_size_from_spec_;
        StorageFile *last_sf_;

        int         td_; // transfer ID of the *Transfer we're part of.
        ProgressCallback alloc_cb_;
        uint64_t    live_disc_wnd_bytes_;

        int         WriteSpecPart(StorageFile *sf, const void *buf, size_t nbyte, int64_t offset);
        std::pair<int64_t,int64_t> WriteBuffer(StorageFile *sf, const void *buf, size_t nbyte, int64_t offset);
        StorageFile * FindStorageFile(int64_t offset);
        int         ParseSpec(StorageFile *sf);
        int         OpenSingleFile();

    };

    /*
     * Manager for starting on-demand transfers that serve content and hashes
     * directly from disk (so little state in memory). Requires content (named
     * as roothash-in-hex), hashes (roothash-in-hex.mhash file) and checkpoint
     * (roothash-in-hex.mbinmap) to be present on disk.
     */
    class ZeroState
    {
      public:
    	ZeroState();
    	~ZeroState();
    	static ZeroState *GetInstance();
    	void SetContentDir(std::string contentdir);
    	void SetConnectTimeout(tint timeout);
    	int Find(const Sha1Hash &root_hash);

        static void LibeventCleanCallback(int fd, short event, void *arg);

      protected:
        static ZeroState *__singleton;

        struct event	evclean_;
        std::string     contentdir_;

        /* Arno, 2012-07-20: A very slow peer can keep a transfer alive
          for a long time (3 minute channel close timeout not reached).
          This causes problems on Mac where there are just 256 file
          descriptors per process and this problem causes all of them
          to be used.
        */
        tint		connect_timeout_;
    };


    /*************** The top-level API ****************/
    // See api.cpp for the implementation.
    /** Must be called by any client using the library */
    void    LibraryInit(void);

    /** Start listening a port. Returns socket descriptor. */
    int     Listen (Address addr);
    /** Stop listening to a port. */
    /** Get the address bound to the socket descriptor returned by Listen() */
    Address BoundAddress(evutil_socket_t sock);
    void    Shutdown();
    /** Open a file, start a transmission; fill it with content for a given
        root hash and tracker (optional). If "force_check_diskvshash" is true, the
        hashtree state will be (re)constructed from the file on disk (if any).
        If not, open will try to reconstruct the hashtree state from
        the .mhash and .mbinmap files on disk. .mhash files are created
        automatically, .mbinmap files must be written by checkpointing the
        transfer by calling FileTransfer::serialize(). If the reconstruction
        fails, it will hashcheck anyway. Roothash is optional for new files or
        files already hashchecked and checkpointed. If "check_netwvshash" is
        false, no uncle hashes will be sent and no data will be verified against
        them on receipt. In this mode, checking disk contents against hashes
        no longer works on restarts, unless checkpoints are used.
        */
        // TODO: replace check_netwvshash with full set of protocol options
    int     Open( std::string filename, SwarmID& swarmid, std::string trackerurl="", bool force_check_diskvshash=true, popt_cont_int_prot_t cipm=POPT_CONT_INT_PROT_MERKLE, bool zerostate=false, bool activate=true, uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE);
    /** Get the root hash for the transmission. */
    SwarmID GetSwarmID( int file);
    /** Close a file and a transmission, remove state or content if desired. */
    void    Close( int td, bool removestate = false, bool removecontent = false);
    /** Add a possible peer which participares in a given transmission. In the case
        root hash is zero, the peer might be talked to regarding any transmission
        (likely, a tracker, cache or an archive). */
    void    AddPeer( Address& address, SwarmID& swarmid);

    /** UNIX pread approximation. Does change file pointer. Thread-safe if no concurrent writes. Autoactivates */
    ssize_t Read( int td, void *buf, size_t nbyte, int64_t offset); // off_t not 64-bit dynamically on Win32

    /** UNIX pwrite approximation. Does change file pointer. Is not thread-safe. Autoactivates. */
    ssize_t Write( int td, const void *buf, size_t nbyte, int64_t offset);

    /** Seek, i.e., move start of interest window */
    int     Seek( int td, int64_t offset, int whence);
    /** Set the default tracker that is used when Open is not passed a tracker
        address. */
    void    SetTracker(std::string trackerurl);
    /** Returns size of the file in bytes, 0 if unknown. Might be rounded up
        to a kilobyte before the transmission is complete. */
    uint64_t Size( int td);
    /** Returns the amount of retrieved and verified data, in bytes.
        A 100% complete transmission has Size()==Complete(). */
    uint64_t Complete( int td);
    bool    IsComplete( int td);
    /** Returns the number of bytes that are complete sequentially, starting
        from the beginning, till the first not-yet-retrieved packet.
        For LIVE beginning = GetHookinOffset() */
    uint64_t SeqComplete( int td, int64_t offset=0);
    /** Returns the bin at which we hooked into the live stream. */
    uint64_t GetHookinOffset( int td);

    /** Arno: See if swarm is known and activate if requested */
    int     Find( SwarmID& swarmid, bool activate=false);
    /** Returns the number of bytes in a chunk for this transmission */
    uint32_t ChunkSize(int td);

    // LIVE
    /** To create a live stream as source */
    LiveTransfer *LiveCreate(std::string filename, KeyPair &keypair, std::string checkpoint_filename, popt_cont_int_prot_t cipm=POPT_CONT_INT_PROT_UNIFIED_MERKLE, uint64_t disc_wnd=POPT_LIVE_DISC_WND_ALL, uint32_t nchunks_per_sign=SWIFT_DEFAULT_LIVE_NCHUNKS_PER_SIGN, uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE);
    /** To add chunks to a live stream as source */
    int     LiveWrite(LiveTransfer *lt, const void *buf, size_t nbyte);
    /** To open a live stream as peer */
    int     LiveOpen(std::string filename, SwarmID &swarmid, std::string trackerurl, Address &srcaddr, popt_cont_int_prot_t cipm, uint64_t disc_wnd=POPT_LIVE_DISC_WND_ALL, uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE);

    /** Register a callback for when the download of another pow(2,agg) chunks
        is finished. So agg = 0 = 2^0 = 1, means every chunk. */
    void    AddProgressCallback( int td, ProgressCallback cb, uint8_t agg);
    /** Deregister a previously added callback. */
    void    RemoveProgressCallback( int td, ProgressCallback cb );

    /** Return the transfer descriptors of all loaded transfers (incl. LIVE). */
    tdlist_t GetTransferDescriptors();
    /** Set the maximum speed in bytes/s for the transfer */
    void    SetMaxSpeed( int td, data_direction_t ddir, double speed);
    /** Get the current speed in bytes/s for the transfer, if activated. */
    double  GetCurrentSpeed( int td, data_direction_t ddir);
    /** Get the number of incomplete peers for the transfer, if activated. */
    uint32_t  GetNumLeechers( int td);
    /** Get the number of completed peers for the transfer, if activated. */
    uint32_t  GetNumSeeders( int td);
    /** Return the type of this transfer */
    transfer_t ttype( int td);
    /** Get Storage object for the transfer, if activated. */
    Storage *GetStorage( int td);
    /** Get Storage object's main storage filename. */
    std::string GetOSPathName( int td);
    /** Whether this transfer is in working condition, if activated. */
    bool    IsOperational( int td);
    /** Whether this transfer uses the zero-state implementation. */
    bool    IsZeroState( int td);
    /** Save the binmap for the transfer for restarts without from-disk hash checking */
    int Checkpoint(int transfer);

    /** Return the ContentTransfer * for the transfer, if activated.
        For internal use only */
    ContentTransfer *GetActivatedTransfer(int td);
    /** Record use of this transfer. For internal use only. */
    void Touch(int td);
    /** Write a checkpoint for filename in .mhash and .mbinmap files without
     * creating a FileTransfer object */
    int HashCheckOffline( std::string filename, Sha1Hash *calchashptr, uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE);

    // Arno: helper functions for constructing datagrams */
    int evbuffer_add_string(struct evbuffer *evb, std::string str);
    int evbuffer_add_8(struct evbuffer *evb, uint8_t b);
    int evbuffer_add_16be(struct evbuffer *evb, uint16_t w);
    int evbuffer_add_32be(struct evbuffer *evb, uint32_t i);
    int evbuffer_add_64be(struct evbuffer *evb, uint64_t l);
    int evbuffer_add_hash(struct evbuffer *evb, const Sha1Hash& hash);
    int evbuffer_add_chunkaddr(struct evbuffer *evb, bin_t &b, popt_chunk_addr_t chunk_addr); // PPSP
    int evbuffer_add_pexaddr(struct evbuffer *evb, Address& a);

    uint8_t evbuffer_remove_8(struct evbuffer *evb);
    uint16_t evbuffer_remove_16be(struct evbuffer *evb);
    uint32_t evbuffer_remove_32be(struct evbuffer *evb);
    uint64_t evbuffer_remove_64be(struct evbuffer *evb);
    Sha1Hash evbuffer_remove_hash(struct evbuffer* evb);
    binvector evbuffer_remove_chunkaddr(struct evbuffer *evb, popt_chunk_addr_t chunk_addr); // PPSP
    Address evbuffer_remove_pexaddr(struct evbuffer *evb, int family);
    void chunk32_to_bin32(uint32_t schunk, uint32_t echunk, binvector *bvptr);
    binvector bin_fragment(bin_t &origbin, bin_t &cancelbin);

    const char* tintstr(tint t=0);

    // SOCKTUNNEL
    void CmdGwTunnelUDPDataCameIn(Address srcaddr, uint32_t srcchan, struct evbuffer* evb);
    void CmdGwTunnelSendUDP(struct evbuffer *evb); // for friendship with Channel

} // namespace end

// #define SWIFT_MUTE

#ifndef SWIFT_MUTE
#define dprintf(...) do { if (Channel::debug_file) fprintf(Channel::debug_file,__VA_ARGS__); } while (0)
#define dflush() fflush(Channel::debug_file)
#define lprintf(...) do { if (Channel::debug_ledbat) fprintf(Channel::debug_ledbat,__VA_ARGS__); } while (0)
#define lflush() fflush(Channel::debug_ledbat)
#else
#define dprintf(...) do {} while(0)
#define dflush() do {} while(0)
#endif
#define eprintf(...) fprintf(stderr,__VA_ARGS__)

#endif
