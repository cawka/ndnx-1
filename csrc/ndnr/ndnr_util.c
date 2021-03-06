/**
 * @file ndnr_util.c
 * 
 * Part of ndnr -  NDNx Repository Daemon.
 *
 */

/*
 * Portions Copyright (C) 2013 Regents of the University of California.
 * 
 * Based on the CCNx C Library by PARC.
 * Copyright (C) 2011 Palo Alto Research Center, Inc.
 *
 * This work is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 * This work is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details. You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
 
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/in.h>

#include <ndn/bloom.h>
#include <ndn/ndn.h>
#include <ndn/ndn_private.h>
#include <ndn/charbuf.h>
#include <ndn/face_mgmt.h>
#include <ndn/hashtb.h>
#include <ndn/indexbuf.h>
#include <ndn/schedule.h>
#include <ndn/reg_mgmt.h>
#include <ndn/uri.h>

#include "ndnr_private.h"

#include "ndnr_util.h"

PUBLIC struct ndn_charbuf *
r_util_charbuf_obtain(struct ndnr_handle *h)
{
    struct ndn_charbuf *c = h->scratch_charbuf;
    if (c == NULL)
        return(ndn_charbuf_create());
    h->scratch_charbuf = NULL;
    c->length = 0;
    return(c);
}

PUBLIC void
r_util_charbuf_release(struct ndnr_handle *h, struct ndn_charbuf *c)
{
    c->length = 0;
    if (h->scratch_charbuf == NULL)
        h->scratch_charbuf = c;
    else
        ndn_charbuf_destroy(&c);
}

PUBLIC struct ndn_indexbuf *
r_util_indexbuf_obtain(struct ndnr_handle *h)
{
    struct ndn_indexbuf *c = h->scratch_indexbuf;
    if (c == NULL)
        return(ndn_indexbuf_create());
    h->scratch_indexbuf = NULL;
    c->n = 0;
    return(c);
}

PUBLIC void
r_util_indexbuf_release(struct ndnr_handle *h, struct ndn_indexbuf *c)
{
    c->n = 0;
    if (h->scratch_indexbuf == NULL)
        h->scratch_indexbuf = c;
    else
        ndn_indexbuf_destroy(&c);
}

PUBLIC void
r_util_reseed(struct ndnr_handle *h)
{
    int fd;
    ssize_t res;
    
    res = -1;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd != -1) {
        res = read(fd, h->seed, sizeof(h->seed));
        close(fd);
    }
    if (res != sizeof(h->seed)) {
        h->seed[1] = (unsigned short)getpid(); /* better than no entropy */
        h->seed[2] = (unsigned short)time(NULL);
    }
    /*
     * The call to seed48 is needed by cygwin, and should be harmless
     * on other platforms.
     */
    seed48(h->seed);
}

PUBLIC void
r_util_gettime(const struct ndn_gettime *self, struct ndn_timeval *result)
{
    struct ndnr_handle *h = self->data;
    struct timeval now = {0};
    gettimeofday(&now, 0);
    result->s = now.tv_sec;
    result->micros = now.tv_usec;
    h->sec = now.tv_sec;
    h->usec = now.tv_usec;
}

PUBLIC int
r_util_timecmp(long secA, unsigned usecA, long secB, unsigned usecB)
{
    if (secA < secB) return (-1);
    if (secA > secB) return (1);
    if (usecA < usecB) return (-1);
    if (usecA > usecB) return (1);
    return (0);
}
PUBLIC intmax_t
r_util_segment_from_component(const unsigned char *ndnb, size_t start, size_t stop)
{
    const unsigned char *data = NULL;
    size_t len = 0;
    intmax_t segment;
    int i;
    
    if (start < stop) {
        ndn_ref_tagged_BLOB(NDN_DTAG_Component, ndnb, start, stop, &data, &len);
        if (len > 0 && data != NULL && data[0] == 0 && len <= (1 + sizeof(intmax_t))) {
            // parse big-endian encoded number with leading 0 byte
            segment = 0;
            for (i = 1; i < len; i++) {
                segment = segment * 256 + data[i];
            }
            return(segment);
        }
    }
    return(-1);
}
/**
 * Compare a name component at index i to bytes in buf and return 0
 * if they are equal length and equal value.
 * In the case of inequality, a negative or positive value is returned,
 * according to the canonical ordering of names.
 */
int
r_util_name_comp_compare(const unsigned char *data,
                         const struct ndn_indexbuf *indexbuf,
                         unsigned int i, const void *buf, size_t length)
{
    const unsigned char *comp_ptr;
    size_t comp_size;
    
    if (ndn_name_comp_get(data, indexbuf, i, &comp_ptr, &comp_size) != 0)
        return(-1);
    if (comp_size < length)
        return(-1);
    if (comp_size > length)
        return(1);
    return(memcmp(comp_ptr, buf, length));
}
