/*
 *  nat_test.cpp
 *  NAT type testing.
 *
 *  Created by Gertjan Halkes.
 *  Copyright 2010 Delft University of Technology. All rights reserved.
 *
 */

#include "swift.h"
#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <errno.h>
#include <netinet/in.h>
#endif

#define REQUEST_MAGIC 0x5a9e5fa1
#define REPLY_MAGIC 0xa655c5d5
#define REPLY_SEC_MAGIC 0x85e4a5ca
#define MAX_TRIES 3
namespace swift {

static void on_may_receive(SOCKET sock);
static void on_may_send(SOCKET sock);
static tint test_start;
static int tries;
static int packets_since_last_try;

static sckrwecb_t callbacks(0, on_may_receive, on_may_send, NULL);
/* Note that we lookup the addresses when we actually send, because Windows requires that
   the winsock library is first intialized. If we use Address type variables here, the
   lookup would be tried before that initialization, which fails... */
//FIXME: Change addresses to actual addresses used in test (at least 2 should be provided!)
static const char *servers[] = { "dutigp.st.ewi.tudelft.nl:18375" ,
    "127.0.0.3:18375" };

static void on_may_receive(SOCKET sock) {
    Datagram data(sock);

    data.Recv();

    uint32_t magic = data.Pull32();
    if ((magic != REPLY_MAGIC && magic != REPLY_SEC_MAGIC) ||
            (magic == REPLY_MAGIC && data.size() != 6) || (magic == REPLY_SEC_MAGIC && data.size() != 0))
    {
        dprintf("%s #0 NATTEST weird packet %s \n", tintstr(), data.address().str());
        return;
    }

    if (magic == REPLY_MAGIC) {
        uint32_t ip = data.Pull32();
        uint16_t port = data.Pull16();
        Address reported(ip, port);
        dprintf("%s #0 NATTEST incoming %s %s\n", tintstr(), data.address().str(), reported.str());
    } else {
        dprintf("%s #0 NATTEST incoming secondary %s\n", tintstr(), data.address().str());
    }
    packets_since_last_try++;
}

static void on_may_send(SOCKET sock) {
    callbacks.may_write = NULL;
    Datagram::Listen3rdPartySocket(callbacks);

    for (size_t i = 0; i < (sizeof(servers)/sizeof(servers[0])); i++) {
        Datagram request(sock, Address(servers[i]));

        request.Push32(REQUEST_MAGIC);
        request.Send();
    }
    test_start = NOW;

    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    if (getsockname(sock, (struct sockaddr *) &name, &namelen) < 0) {
        dprintf("%s #0 NATTEST could not get local address\n", tintstr());
    } else {
        Address local(ntohl(name.sin_addr.s_addr), ntohs(name.sin_port));
        dprintf("%s #0 NATTEST local %s\n", tintstr(), local.str());
    }
}

static void printAddresses(void) {
#ifdef _WIN32
    IP_ADAPTER_INFO *adapterInfo = NULL;
    IP_ADAPTER_INFO *adapter = NULL;
    DWORD retval = 0;
    UINT i;
    ULONG size = 0;

    if ((retval = GetAdaptersInfo(adapterInfo, &size)) != ERROR_BUFFER_OVERFLOW) {
        dprintf("ERROR: %d\n", (int) retval);
        return;
    }

    adapterInfo = (IP_ADAPTER_INFO *) malloc(size);
    if (adapterInfo == NULL) {
        dprintf("ERROR: out of memory\n");
        return;
    }

    if ((retval = GetAdaptersInfo(adapterInfo, &size)) == NO_ERROR) {
        adapter = adapterInfo;
        while (adapter) {
            IP_ADDR_STRING *address;
            for (address = &adapter->IpAddressList; address != NULL; address = address->Next) {
                if (address->IpAddress.String[0] != 0)
                    dprintf("ADDRESS: %s\n", address->IpAddress.String);
            }
            adapter = adapter->Next;
        }
    } else {
        dprintf("ERROR: %d\n", (int) retval);
    }
    free(adapterInfo);
#else
    struct ifaddrs *addrs, *ptr;
    if (getifaddrs(&addrs) < 0) {
        dprintf("ERROR: %s\n", strerror(errno));
        return;
    }

    for (ptr = addrs; ptr != NULL; ptr = ptr->ifa_next) {
        if (ptr->ifa_addr->sa_family == AF_INET) {
            dprintf("ADDRESS: %s\n", inet_ntoa(((struct sockaddr_in *) ptr->ifa_addr)->sin_addr));
        }
    }
    freeifaddrs(addrs);
#endif
}


void nat_test_update(void) {
    static bool initialized;
    if (!initialized) {
        initialized = true;
        printAddresses();
    }

    if (tries < MAX_TRIES && NOW - test_start > 30 * TINT_SEC) {
        if (tries == 0) {
            Address any;
            SOCKET sock = Datagram::Bind(any, callbacks);
            callbacks.sock = sock;
        } else if (packets_since_last_try == 0) {
            // Keep on trying if we didn't receive _any_ packet in response to our last request
            tries--;
        }
        tries++;
        callbacks.may_write = on_may_send;
        Datagram::Listen3rdPartySocket(callbacks);
    }
}

}
