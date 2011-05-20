#include "common.h"

/**
 * Looks up a fdholder based on its filedesc (private).
 */
PUBLIC struct fdholder *
r_io_fdholder_from_fd(struct ccnr_handle *h, unsigned filedesc)
{
    unsigned slot = filedesc;
    struct fdholder *fdholder = NULL;
    if (slot < h->face_limit) {
        fdholder = h->fdholder_by_fd[slot];
        if (fdholder != NULL && fdholder->filedesc != filedesc)
            fdholder = NULL;
    }
    return(fdholder);
}

/**
 * Looks up a fdholder based on its filedesc.
 */
PUBLIC struct fdholder *
ccnr_r_io_fdholder_from_fd(struct ccnr_handle *h, unsigned filedesc)
{
    return(r_io_fdholder_from_fd(h, filedesc));
}

/**
 * Assigns the filedesc for a nacent fdholder,
 * calls r_io_register_new_face() if successful.
 */
PUBLIC int
r_io_enroll_face(struct ccnr_handle *h, struct fdholder *fdholder)
{
    unsigned i = fdholder->filedesc;
    unsigned n = h->face_limit;
    struct fdholder **a = h->fdholder_by_fd;
    if (i < n && a[i] == NULL) {
        if (a[i] == NULL)
            goto use_i;
        abort();
    }
    if (i > 65535)
        abort();
    a = realloc(a, (i + 1) * sizeof(struct fdholder *));
    if (a == NULL)
        return(-1); /* ENOMEM */
    h->face_limit = i + 1;
    while (n < h->face_limit)
        a[n++] = NULL;
    h->fdholder_by_fd = a;
use_i:
    a[i] = fdholder;
    fdholder->filedesc = i;
    fdholder->meter[FM_BYTI] = ccnr_meter_create(h, "bytein");
    fdholder->meter[FM_BYTO] = ccnr_meter_create(h, "byteout");
    fdholder->meter[FM_INTI] = ccnr_meter_create(h, "intrin");
    fdholder->meter[FM_INTO] = ccnr_meter_create(h, "introut");
    fdholder->meter[FM_DATI] = ccnr_meter_create(h, "datain");
    fdholder->meter[FM_DATO] = ccnr_meter_create(h, "dataout");
    r_io_register_new_face(h, fdholder);
    return (fdholder->filedesc);
}

/**
 * Close an open file descriptor quietly.
 */
static void
close_fd(int *pfd)
{
    if (*pfd != -1) {
        close(*pfd);
        *pfd = -1;
    }
}

/**
 * Close an open file descriptor, and grumble about it.
 */
static void
ccnr_close_fd(struct ccnr_handle *h, unsigned filedesc, int *pfd)
{
    int res;
    
    if (*pfd != -1) {
        int linger = 0;
        setsockopt(*pfd, SOL_SOCKET, SO_LINGER,
                   &linger, sizeof(linger));
        res = close(*pfd);
        if (res == -1)
            ccnr_msg(h, "close failed for fdholder %u fd=%d: %s (errno=%d)",
                     filedesc, *pfd, strerror(errno), errno);
        else
            ccnr_msg(h, "closing fd %d while finalizing fdholder %u", *pfd, filedesc);
        *pfd = -1;
    }
}


/**
 * Initialize the fdholder flags based upon the addr information
 * and the provided explicit setflags.
 */
static void
init_face_flags(struct ccnr_handle *h, struct fdholder *fdholder, int setflags)
{
    const struct sockaddr *addr = fdholder->addr;
    const unsigned char *rawaddr = NULL;
    
    if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
        fdholder->flags |= CCN_FACE_INET6;
#ifdef IN6_IS_ADDR_LOOPBACK
        if (IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr))
            fdholder->flags |= CCN_FACE_LOOPBACK;
#endif
    }
    else if (addr->sa_family == AF_INET) {
        const struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
        rawaddr = (const unsigned char *)&addr4->sin_addr.s_addr;
        fdholder->flags |= CCN_FACE_INET;
        if (rawaddr[0] == 127)
            fdholder->flags |= CCN_FACE_LOOPBACK;
        else {
            /* If our side and the peer have the same address, consider it loopback */
            /* This is the situation inside of FreeBSD jail. */
            struct sockaddr_in myaddr;
            socklen_t myaddrlen = sizeof(myaddr);
            if (0 == getsockname(fdholder->recv_fd, (struct sockaddr *)&myaddr, &myaddrlen)) {
                if (addr4->sin_addr.s_addr == myaddr.sin_addr.s_addr)
                    fdholder->flags |= CCN_FACE_LOOPBACK;
            }
        }
    }
    else if (addr->sa_family == AF_UNIX)
        fdholder->flags |= (CCN_FACE_GG | CCN_FACE_LOCAL);
    fdholder->flags |= setflags;
}

/**
 * Make a new fdholder entered in the faces_by_fd table.
 */
PUBLIC struct fdholder *
r_io_record_connection(struct ccnr_handle *h, int fd,
                  struct sockaddr *who, socklen_t wholen,
                  int setflags)
{
    int res;
    struct fdholder *fdholder = NULL;
    unsigned char *addrspace;
    
    res = fcntl(fd, F_SETFL, O_NONBLOCK);
    if (res == -1)
        ccnr_msg(h, "fcntl: %s", strerror(errno));
    fdholder = calloc(1, sizeof(*fdholder));
    if (fdholder == NULL)
        return(fdholder);
    addrspace = calloc(1, wholen);
    if (addrspace != NULL) {
        memcpy(addrspace, who, wholen);
        fdholder->addrlen = wholen;
    }
    fdholder->addr = (struct sockaddr *)addrspace;
    fdholder->recv_fd = fd;
    fdholder->filedesc = fd;
    fdholder->sendface = CCN_NOFACEID;
    init_face_flags(h, fdholder, setflags);
    res = r_io_enroll_face(h, fdholder);
    if (res == -1) {
        if (addrspace != NULL)
            free(addrspace);
        free(fdholder);
        fdholder = NULL;
    }
    return(fdholder);
}

/**
 * Accept an incoming DGRAM_STREAM connection, creating a new fdholder.
 * @returns fd of new socket, or -1 for an error.
 */
PUBLIC int
r_io_accept_connection(struct ccnr_handle *h, int listener_fd)
{
    struct sockaddr_storage who;
    socklen_t wholen = sizeof(who);
    int fd;
    struct fdholder *fdholder;

    fd = accept(listener_fd, (struct sockaddr *)&who, &wholen);
    if (fd == -1) {
        ccnr_msg(h, "accept: %s", strerror(errno));
        return(-1);
    }
    fdholder = r_io_record_connection(h, fd,
                            (struct sockaddr *)&who, wholen,
                            CCN_FACE_UNDECIDED);
    if (fdholder == NULL)
        close_fd(&fd);
    else
        ccnr_msg(h, "accepted client fd=%d id=%u", fd, fdholder->filedesc);
    return(fd);
}

PUBLIC void
r_io_shutdown_client_fd(struct ccnr_handle *h, int fd)
{
    struct fdholder *fdholder = NULL;
    enum cq_delay_class c;
    int m;
    int res;
    
    fdholder = r_io_fdholder_from_fd(h, fd);
    if (fdholder == NULL) {
        ccnr_msg(h, "no fd holder for fd %d", fd);
        abort();
    }
    res = close(fd);
    ccnr_msg(h, "shutdown client fd=%d", fd);
    ccn_charbuf_destroy(&fdholder->inbuf);
    ccn_charbuf_destroy(&fdholder->outbuf);
    for (c = 0; c < CCN_CQ_N; c++)
        r_sendq_content_queue_destroy(h, &(fdholder->q[c]));
    if (fdholder->addr != NULL) {
        free(fdholder->addr);
		fdholder->addr = NULL;
    }
	for (m = 0; m < CCNR_FACE_METER_N; m++)
        ccnr_meter_destroy(&fdholder->meter[m]);
	if (h->fdholder_by_fd[fd] != fdholder) abort();
	h->fdholder_by_fd[fd] = NULL;
    free(fdholder);
    r_fwd_reap_needed(h, 250000);
}

/**
 * Destroys the fdholder identified by filedesc.
 * @returns 0 for success, -1 for failure.
 */
PUBLIC int
r_io_destroy_face(struct ccnr_handle *h, unsigned filedesc)
{
    r_io_shutdown_client_fd(h, filedesc);
    return(0);
}

/**
 * Called when a fdholder is first created, and (perhaps) a second time in the case
 * that a fdholder transitions from the undecided state.
 */
PUBLIC void
r_io_register_new_face(struct ccnr_handle *h, struct fdholder *fdholder)
{
    if (fdholder->filedesc != 0 && (fdholder->flags & (CCN_FACE_UNDECIDED | CCN_FACE_PASSIVE)) == 0) {
        ccnr_face_status_change(h, fdholder->filedesc);
        r_link_ccn_link_state_init(h, fdholder);
    }
}

/**
 * Handle errors after send() or sendto().
 * @returns -1 if error has been dealt with, or 0 to defer sending.
 */
static int
handle_send_error(struct ccnr_handle *h, int errnum, struct fdholder *fdholder,
                  const void *data, size_t size)
{
    int res = -1;
    if (errnum == EAGAIN) {
        res = 0;
    }
    else if (errnum == EPIPE) {
        fdholder->flags |= CCN_FACE_NOSEND;
        fdholder->outbufindex = 0;
        ccn_charbuf_destroy(&fdholder->outbuf);
    }
    else {
        ccnr_msg(h, "send to fd %u failed: %s (errno = %d)",
                 fdholder->filedesc, strerror(errnum), errnum);
        if (errnum == EISCONN)
            res = 0;
    }
    return(res);
}

static int
sending_fd(struct ccnr_handle *h, struct fdholder *fdholder)
{
    return(fdholder->filedesc);
}

/**
 * Send data to the fdholder.
 *
 * No direct error result is provided; the fdholder state is updated as needed.
 */
PUBLIC void
r_io_send(struct ccnr_handle *h,
          struct fdholder *fdholder,
          const void *data, size_t size)
{
    ssize_t res;
    if ((fdholder->flags & CCN_FACE_NOSEND) != 0)
        return;
    fdholder->surplus++;
    if (fdholder->outbuf != NULL) {
        ccn_charbuf_append(fdholder->outbuf, data, size);
        return;
    }
    if (fdholder == h->face0) {
        ccnr_meter_bump(h, fdholder->meter[FM_BYTO], size);
        ccn_dispatch_message(h->internal_client, (void *)data, size);
        r_dispatch_process_internal_client_buffer(h);
        return;
    }
    if ((fdholder->flags & CCN_FACE_DGRAM) == 0)
        res = send(fdholder->recv_fd, data, size, 0);
    else
        res = sendto(sending_fd(h, fdholder), data, size, 0,
                     fdholder->addr, fdholder->addrlen);
    if (res > 0)
        ccnr_meter_bump(h, fdholder->meter[FM_BYTO], res);
    if (res == size)
        return;
    if (res == -1) {
        res = handle_send_error(h, errno, fdholder, data, size);
        if (res == -1)
            return;
    }
    if ((fdholder->flags & CCN_FACE_DGRAM) != 0) {
        ccnr_msg(h, "sendto short");
        return;
    }
    fdholder->outbufindex = 0;
    fdholder->outbuf = ccn_charbuf_create();
    if (fdholder->outbuf == NULL) {
        ccnr_msg(h, "do_write: %s", strerror(errno));
        return;
    }
    ccn_charbuf_append(fdholder->outbuf,
                       ((const unsigned char *)data) + res, size - res);
}
/**
 * Set up the array of fd descriptors for the poll(2) call.
 *
 */
PUBLIC void
r_io_prepare_poll_fds(struct ccnr_handle *h)
{
    int i, j, nfds;
    
    for (i = 0, nfds = 0; i < h->face_limit; i++)
        if (r_io_fdholder_from_fd(h, i) != NULL)
            nfds++;
    
    if (nfds != h->nfds) {
        h->nfds = nfds;
        h->fds = realloc(h->fds, h->nfds * sizeof(h->fds[0]));
        memset(h->fds, 0, h->nfds * sizeof(h->fds[0]));
    }
    for (i = 0, j = 0; i < h->face_limit; i++) {
        struct fdholder *fdholder = r_io_fdholder_from_fd(h, i);
        if (fdholder != NULL) {
            h->fds[j].fd = fdholder->filedesc;
            h->fds[j].events = ((fdholder->flags & CCN_FACE_NORECV) == 0) ? POLLIN : 0;
            if ((fdholder->outbuf != NULL || (fdholder->flags & CCN_FACE_CLOSING) != 0))
                h->fds[j].events |= POLLOUT;
            j++;
        }
    }
}

/**
 * Shutdown listeners and bound datagram sockets, leaving connected streams.
 */
PUBLIC void
r_io_shutdown_all(struct ccnr_handle *h)
{
    int i;
    for (i = 1; i < h->face_limit; i++) {
        r_io_shutdown_client_fd(h, i);
    }
}
