/*
 *  exttrack.h
 *
 *  Defines and classes for working with external trackers
 *
 *  Created by Arno Bakker
 *  Copyright 2013-2016 Vrije Universiteit Amsterdam. All rights reserved.
 */
#ifndef SWIFT_EXTTRACK_H_
#define SWIFT_EXTTRACK_H_

namespace swift
{

#define EXTTRACK_EVENT_STARTED      "started"
#define EXTTRACK_EVENT_COMPLETED    "completed"
#define EXTTRACK_EVENT_STOPPED      "stopped"
#define EXTTRACK_EVENT_WORKING      ""

    struct Address;
    typedef std::vector<Address>    peeraddrs_t;

    /** External tracker */
    typedef void (*exttrack_peerlist_callback_t)(int td, std::string result, uint32_t interval, peeraddrs_t peerlist);


    class ContentTransfer;

    struct ExtTrackCallbackRecord {
        ExtTrackCallbackRecord(int td, exttrack_peerlist_callback_t callback) : td_(td), callback_(callback) {}
        int td_;
        exttrack_peerlist_callback_t    callback_;
    };

    class ExternalTrackerClient
    {
    public:
        ExternalTrackerClient(std::string url);
        ~ExternalTrackerClient();
        /** (Re)Registers at tracker and adds returned peer addresses to peerlist */
        int Contact(ContentTransfer *transfer, std::string event, exttrack_peerlist_callback_t callback);
        bool GetReportedComplete() {
            return reported_complete_;
        }
        tint GetReportLastTime() {
            return report_last_time_;
        }
        void SetReportInterval(uint32_t interval) {
            report_interval_ = interval;
        }
        uint32_t GetReportInterval() {
            return report_interval_;
        }
    protected:
        std::string     url_;
        uint8_t         *peerid_;
        tint        report_last_time_; // tint
        uint32_t        report_interval_; // seconds
        bool        reported_complete_;

        /** IP in myaddr currently unused */
        std::string CreateQuery(ContentTransfer *transfer, Address myaddr, std::string event);
        int HTTPConnect(std::string query,ExtTrackCallbackRecord *callbackrec);
    };

}

#endif /* SWIFT_EXTTRACK_H_ */
