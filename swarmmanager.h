/*
 *  swarmmanager.h
 *
 *  Arno: Classes for managing FileTransfers, that is, load them into memory or
 *  release them when idle. This is called activation/deactivation.
 *  This manager only manages FileTransfers, LiveTransfers are unmanaged,
 *  i.e., always activated.
 *
 *  The activation/deactivation progress is hidden behind the swift API,
 *  one should not use the SwarmManager directly.
 *
 *  ARNOTODO: tracker registration. The current implementation works if it
 *  runs the primary seeder which is also the tracker for a swarm. Clients
 *  that contact it will activate the swarm. Once a swarm no longer has
 *  clients is can be deactivated (not currently implemented).
 *
 *  However, to be a non-primary seeder (i.e., not the tracker) a new
 *  mechanism needs to be implemented that registers a swarm at a tracker
 *  while being deactivated (activate only when clients come). In the present
 *  swift design registration requires a Channel to be open to the tracker. This
 *  design may need to be changed to allow for a BitTorrent register-for-30-mins
 *  -and-disconnect style.
 *
 *  Current implementation will deactivate:
 *  - when SetMaximumActiveSwarms() is exceeded (Thomas)
 *  - when idle for more than SECONDS_UNUSED_UNTIL_SWARM_MAY_BE_DEACTIVATED.
 *    Idle is when no Read(), Write() or DATA send or receive (Arno)
 *    (see ContentTransfer::GlobalCleanCallback).
 *
 *  Note that FileTransfers with the zero-state implementation are actually
 *  unloaded (=no FileTransfer object and no admin in SwarmManager) when idle,
 *  see zerostate.cpp. This is orthogonal to idle deactivation.
 *
 *  Created by Thomas Schaap
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#include <time.h>
#include <vector>
#include <list>
#include "hashtree.h"

namespace swift {
    class SwarmManager;

    class SwarmData {
    protected:
        int id_;
        Sha1Hash rootHash_;
        bool active_;
        tint latestUse_;
        bool stateToBeRemoved_;
        bool contentToBeRemoved_;
        FileTransfer* ft_;
        std::string filename_;
        Address tracker_;
        bool forceCheckDiskVSHash_;
        bool checkNetworkVSHash_;
        uint32_t chunkSize_;
        bool zerostate_;
        double cachedMaxSpeeds_[2];
        bool cachedStorageReady_;
        std::list<std::string> cachedStorageFilenames_;
        uint64_t cachedSize_;
        bool cachedIsComplete_;
        uint64_t cachedComplete_;
        std::string cachedOSPathName_;
        std::list< std::pair<ProgressCallback, uint8_t> > cachedCallbacks_; //ARNOTODO: how does this work?
        uint64_t cachedSeqComplete_; // Only for offset = 0
        bool cached_;
    public:
        SwarmData( const std::string filename, const Sha1Hash& rootHash, const Address& tracker, bool force_check_diskvshash, bool check_netwvshash, bool zerostate, uint32_t chunk_size );
        SwarmData( const SwarmData& sd );

        ~SwarmData();

        bool Touch();
        bool IsActive();
        const Sha1Hash& RootHash();
        int Id();
        FileTransfer* GetTransfer(bool touch = true);
        std::string& Filename();
        Address& Tracker();
        uint32_t ChunkSize();
        bool IsZeroState();

        // Find out cached values of non-active swarms
        uint64_t Size();
        bool IsComplete();
        uint64_t Complete();
        uint64_t SeqComplete(int64_t offset = 0);
        std::string OSPathName();

        void SetMaxSpeed(data_direction_t ddir, double speed);
        void AddProgressCallback(ProgressCallback cb, uint8_t agg);
        void RemoveProgressCallback(ProgressCallback cb);

        friend class SwarmManager;
    };

    class SwarmManager {
    protected:
        SwarmManager();
        ~SwarmManager();


        // Singleton
        static SwarmManager instance_;

        // Structures to keep track of all the swarms known to this manager
        // That's two lists of swarms, indeed.
        // The first allows very fast lookups
        // - hash table (bucket is rootHash.bits[0]&63) containing lists ordered by rootHash (binary search possible)
        // The second allows very fast access by numeric identifier (used in toplevel API)
        // - just a vector with a new element for each new one, and a list of available indices
        std::vector< std::vector<SwarmData*> > knownSwarms_;
        std::vector<SwarmData*> swarmList_;
        struct UnusedIndex {
            int index;
            tint since;
        };
        std::list<struct UnusedIndex> unusedIndices_;

        // Structures and functions for deferred removal of active swarms
        struct event* eventCheckToBeRemoved_;
        static void CheckSwarmsToBeRemovedCallback(evutil_socket_t fd, short events, void* arg);
        void CheckSwarmsToBeRemoved();

        // Looking up swarms by rootHash, internal functions
        int GetSwarmLocation( const std::vector<SwarmData*>& list, const Sha1Hash& rootHash );
        SwarmData* GetSwarmData( const Sha1Hash& rootHash );

        // Internal activation method
        SwarmData* ActivateSwarm( SwarmData* swarm );
        void BuildSwarm( SwarmData* swarm );

        // Internal method to find the oldest swarm and deactivate it
        bool DeactivateSwarm();
        void DeactivateSwarm( SwarmData* swarm, int activeLoc );

        // Structures to keep track of active swarms
        int maxActiveSwarms_;
        int activeSwarmCount_;
        std::vector<SwarmData*> activeSwarms_;

#if SWARMMANAGER_ASSERT_INVARIANTS
        void invariant();
#endif
    public:
        // Singleton
        static SwarmManager& GetManager();

        // Add and remove swarms
        SwarmData* AddSwarm( const std::string filename, const Sha1Hash& rootHash, const Address& tracker, bool force_check_diskvshash, bool check_netwvshash, bool zerostate, bool activate, uint32_t chunk_size );
        SwarmData* AddSwarm( const SwarmData& swarm, bool activate=true );
        void RemoveSwarm( const Sha1Hash& rootHash, bool removeState = false, bool removeContent = false );

        // Find a swam, either by id or root hash
        SwarmData* FindSwarm( int id );
        SwarmData* FindSwarm( const Sha1Hash& rootHash );

        // Activate a swarm, so it can be used (not active swarms can't be read from/written to)
        SwarmData* ActivateSwarm( const Sha1Hash& rootHash );
        void DeactivateSwarm( const Sha1Hash& rootHash );

        // Manage maximum of active swarms
        int GetMaximumActiveSwarms();
        void SetMaximumActiveSwarms( int newMaxActiveSwarms );

        // Arno
        tdlist_t GetTransferDescriptors();
        // Arno: Called periodically to deactivate unused swarms, even if max not reached
        void DeactivateIdleSwarms();

        class Iterator : public std::iterator<std::input_iterator_tag, SwarmData*> {
        protected:
            int transfer_;
        public:
            Iterator();
            Iterator(int transfer);
            Iterator(const Iterator& other);
            Iterator& operator++();
            Iterator operator++(int);
            bool operator==(const Iterator& other);
            bool operator!=(const Iterator& other);
            SwarmData* operator*();
        };
        friend class Iterator;
        Iterator begin();
        Iterator end();
    };
}
