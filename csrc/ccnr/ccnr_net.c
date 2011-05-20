#include "common.h"

PUBLIC char *
r_net_get_local_sockname(void)
{
    struct sockaddr_un sa;
    ccn_setup_sockaddr_un(NULL, &sa);
    return(strdup(sa.sun_path));
}

PUBLIC void
r_net_setsockopt_v6only(struct ccnr_handle *h, int fd)
{
    int yes = 1;
    int res = 0;
#ifdef IPV6_V6ONLY
    res = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));
#endif
    if (res == -1)
        ccnr_msg(h, "warning - could not set IPV6_V6ONLY on fd %d: %s",
                 fd, strerror(errno));
}

static const char *
af_name(int family)
{
    switch (family) {
        case AF_INET:
            return("ipv4");
        case AF_INET6:
            return("ipv6");
        default:
            return("");
    }
}

PUBLIC int
r_net_listen_on_wildcards(struct ccnr_handle *h)
{
    int fd;
    int res;
    int whichpf;
    struct addrinfo hints = {0};
    struct addrinfo *addrinfo = NULL;
    struct addrinfo *a;
    
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    for (whichpf = 0; whichpf < 2; whichpf++) {
        hints.ai_family = whichpf ? PF_INET6 : PF_INET;
        res = getaddrinfo(NULL, h->portstr, &hints, &addrinfo);
        if (res == 0) {
            for (a = addrinfo; a != NULL; a = a->ai_next) {
                fd = socket(a->ai_family, SOCK_STREAM, 0);
                if (fd != -1) {
                    int yes = 1;
                    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
                    if (a->ai_family == AF_INET6)
                        r_net_setsockopt_v6only(h, fd);
                    res = bind(fd, a->ai_addr, a->ai_addrlen);
                    if (res != 0) {
                        close(fd);
                        continue;
                    }
                    res = listen(fd, 30);
                    if (res == -1) {
                        close(fd);
                        continue;
                    }
                    r_io_record_connection(h, fd,
                                      a->ai_addr, a->ai_addrlen,
                                      CCN_FACE_PASSIVE);
                    ccnr_msg(h, "accepting %s connections on fd %d",
                             af_name(a->ai_family), fd);
                }
            }
            freeaddrinfo(addrinfo);
        }
    }
    return(0);
}

PUBLIC int
r_net_listen_on_address(struct ccnr_handle *h, const char *addr)
{
    int fd;
    int res;
    struct addrinfo hints = {0};
    struct addrinfo *addrinfo = NULL;
    struct addrinfo *a;
    int ok = 0;
    
    ccnr_msg(h, "listen_on %s", addr);
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    res = getaddrinfo(addr, h->portstr, &hints, &addrinfo);
    if (res == 0) {
        for (a = addrinfo; a != NULL; a = a->ai_next) {
            fd = socket(a->ai_family, SOCK_STREAM, 0);
            if (fd != -1) {
                int yes = 1;
                setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
                if (a->ai_family == AF_INET6)
                    r_net_setsockopt_v6only(h, fd);
                res = bind(fd, a->ai_addr, a->ai_addrlen);
                if (res != 0) {
                    close(fd);
                    continue;
                }
                res = listen(fd, 30);
                if (res == -1) {
                    close(fd);
                    continue;
                }
                r_io_record_connection(h, fd,
                                  a->ai_addr, a->ai_addrlen,
                                  CCN_FACE_PASSIVE);
                ccnr_msg(h, "accepting %s connections on fd %d",
                         af_name(a->ai_family), fd);
                ok++;
            }
        }
        freeaddrinfo(addrinfo);
    }
    return(ok > 0 ? 0 : -1);
}

PUBLIC int
r_net_listen_on(struct ccnr_handle *h, const char *addrs)
{
    unsigned char ch;
    unsigned char dlm;
    int res = 0;
    int i;
    struct ccn_charbuf *addr = NULL;
    
    if (addrs == NULL || !*addrs || 0 == strcmp(addrs, "*"))
        return(r_net_listen_on_wildcards(h));
    addr = ccn_charbuf_create();
    for (i = 0, ch = addrs[i]; addrs[i] != 0;) {
        addr->length = 0;
        dlm = 0;
        if (ch == '[') {
            dlm = ']';
            ch = addrs[++i];
        }
        for (; ch > ' ' && ch != ',' && ch != ';' && ch != dlm; ch = addrs[++i])
            ccn_charbuf_append_value(addr, ch, 1);
        if (ch && ch == dlm)
            ch = addrs[++i];
        if (addr->length > 0) {
            res |= r_net_listen_on_address(h, ccn_charbuf_as_string(addr));
        }
        while ((0 < ch && ch <= ' ') || ch == ',' || ch == ';')
            ch = addrs[++i];
    }
    ccn_charbuf_destroy(&addr);
    return(res);
}
