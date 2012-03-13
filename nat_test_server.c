/*
 *  nat_test_server.c
 *  NAT type testing (server).
 *
 *  Created by Gertjan Halkes.
 *  Copyright 2010 Delft University of Technology. All rights reserved.
 *
 */

//FIXME: add timestamp to log output

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <errno.h>

#define REQUEST_MAGIC 0x5a9e5fa1
#define REPLY_MAGIC 0xa655c5d5
#define REPLY_SEC_MAGIC 0x85e4a5ca

static int has_secondary;

/** Alert the user of a fatal error and quit.
    @param fmt The format string for the message. See fprintf(3) for details.
    @param ... The arguments for printing.
*/
void fatal(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

const char *getTimestamp(void) {
    static char timeBuffer[1024];
    struct timeval now;
    double nowF;

    gettimeofday(&now, NULL);
    nowF = (double) now.tv_sec + (double) now.tv_usec / 1000000;
    snprintf(timeBuffer, 1024, "%.4f", nowF);
    return timeBuffer;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in local, remote, secondary;
    uint32_t packet[3];
    int c, sock, sock2, sock3, sock4;
    ssize_t result;

    local.sin_addr.s_addr = INADDR_ANY;

    while ((c = getopt(argc, argv, "s:")) > 0) {
        switch (c) {
            case 's':
                has_secondary = 1;
                secondary.sin_addr.s_addr = inet_addr(optarg);
                break;
            default:
                fatal("Unknown option %c\n", c);
                break;
        }
    }

    if (argc - optind != 3)
        fatal("Usage: nat_test_server [<options>] <primary address> <primary port> <secondary port>\n");

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = inet_addr(argv[optind++]);
    local.sin_port = htons(atoi(argv[optind++]));

    if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
        fatal("Error opening primary socket: %m\n");
    if (bind(sock, (struct sockaddr *) &local, sizeof(local)) < 0)
        fatal("Error binding primary socket: %m\n");

    if (has_secondary) {
        secondary.sin_family = AF_INET;
        secondary.sin_port = local.sin_port;

        if ((sock3 = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
            fatal("Error opening primary socket on secondary address: %m\n");
        if (bind(sock3, (struct sockaddr *) &secondary, sizeof(secondary)) < 0)
            fatal("Error binding primary socket on secondary address: %m\n");
    }

    local.sin_port = htons(atoi(argv[optind++]));

    if ((sock2 = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
        fatal("Error opening secondary socket: %m\n");
    if (bind(sock2, (struct sockaddr *) &local, sizeof(local)) < 0)
        fatal("Error binding secondary socket: %m\n");

    if (has_secondary) {
        secondary.sin_port = local.sin_port;

        if ((sock4 = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
            fatal("Error opening secondary socket on secondary address: %m\n");
        if (bind(sock4, (struct sockaddr *) &secondary, sizeof(secondary)) < 0)
            fatal("Error binding secondary socket on secondary address: %m\n");
    }

    while (1) {
        socklen_t socklen = sizeof(remote);
        if ((result = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr *) &remote, &socklen)) < 0) {
            if (errno == EAGAIN)
                continue;
            fatal("%s: Error receiving packet: %m\n", getTimestamp());
        } else if (result != 4 || ntohl(packet[0]) != REQUEST_MAGIC) {
            fprintf(stderr, "Strange packet received from %s\n", inet_ntoa(remote.sin_addr));
        } else {
            fprintf(stderr, "%s: Received packet from %s:%d\n", getTimestamp(), inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));
            packet[0] = htonl(REPLY_MAGIC);
            packet[1] = remote.sin_addr.s_addr;
            *(uint16_t *)(packet + 2) = remote.sin_port;
    retry:
            if (sendto(sock, packet, 10, 0, (const struct sockaddr *) &remote, socklen) < 10) {
                if (errno == EAGAIN)
                    goto retry;
                fprintf(stderr, "%s: Error sending packet on primary socket: %m\n", getTimestamp());
            }
    retry2:
            if (sendto(sock2, packet, 10, 0, (const struct sockaddr *) &remote, socklen) < 10) {
                if (errno == EAGAIN)
                    goto retry2;
                fprintf(stderr, "%s: Error sending packet on secondary socket: %m\n", getTimestamp());
            }

            if (has_secondary) {
                packet[0] = htonl(REPLY_SEC_MAGIC);
        retry3:
                if (sendto(sock3, packet, 4, 0, (const struct sockaddr *) &remote, socklen) < 4) {
                    if (errno == EAGAIN)
                        goto retry3;
                    fprintf(stderr, "%s: Error sending packet on primary socket on secondary address: %m\n", getTimestamp());
                }
        retry4:
                if (sendto(sock4, packet, 4, 0, (const struct sockaddr *) &remote, socklen) < 4) {
                    if (errno == EAGAIN)
                        goto retry4;
                    fprintf(stderr, "%s: Error sending packet on secondary socket on secondary address: %m\n", getTimestamp());
                }
            }

        }
    }
    return 0;
}
