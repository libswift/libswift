/*
 *  availability.h
 *  Tree keeping track of the availability of each bin in a swarm
 *
 *  Created by Riccardo Petrocco
 *  Copyright 2009-2012 Delft University of Technology. All rights reserved.
 *
 */
#include "bin.h"
#include "binmap.h"
#include "compat.h"
#include <cassert>
#include <vector>

#ifndef AVAILABILITY_H
#define AVAILABILITY_H

namespace swift
{

    typedef std::vector< std::pair<uint32_t, binmap_t*> > WaitingPeers;

    class Availability
    {
    public:

        /**
         * Constructor
         */
        explicit Availability(int connections) {
            connections_ = connections;
            initRarity();
        }

        ~Availability(void) {
            for (int i=0; i<connections_; i++)
                delete rarity_[i];
            delete [] rarity_;
        }

        /** sets the number of connections */
        void setParams(int connections);

        /** set/update the rarity */
        void set(uint32_t channel_id, binmap_t& binmap, bin_t target);

        /** removes the binmap of leaving peers */
        void removeBinmap(uint32_t channel_id, binmap_t& binmap);

        /** adds an entire binmap from the rarity array */
        void addBinmap(binmap_t * binmap);

        /** get rarest bin, of specified width, within a range */
        bin_t getRarest(const bin_t range, int width);

        /** returns the entire array of binmaps ordered by rarity */
        binmap_t** getRarityArray() {
            return rarity_;
        }

        /** returns a binmaps filled with bins of rarity idx */
        binmap_t* get(int idx) {
            return rarity_[idx];
        }

        /** Echo the availability status to stdout */
        void status() const;

    protected:
        // init with the number of connections.
        // it is an array of pointers to binmaps, where the index represents the local view of
        // availability in the swarm, of which the binmap is composed from the rare bins.
        binmap_t **rarity_;
        int connections_;


        /** removes the binmap */
        void removeBinmap(binmap_t& binmap);

        /** removes the bin */
        void removeBin(bin_t bin, int idx);

        /** sets a bin */
        void setBin(bin_t bin, int idx);

        /** updates the rarity array*/
        void updateRarity(bin_t bin, int idx);

        /** initialize the rarity array */
        void initRarity();

        void find_empty(binmap_t& binmap, bin_t range);


    };

}

#endif
