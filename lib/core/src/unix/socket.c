#define TRACE_MODULE _sock

#include "core_pool.h"
#include "core_debug.h"
#include "core_pkbuf.h"

#include "core_arch_network.h"

#define MAX_SOCK_POOL_SIZE              512
#define MAX_SOCK_NODE_POOL_SIZE         512

static int max_fd;
static list_t fd_list;
static fd_set read_fds;

pool_declare(sock_pool, sock_t, MAX_SOCK_POOL_SIZE);
pool_declare(sock_node_pool, sock_node_t, MAX_SOCK_NODE_POOL_SIZE);

static status_t sononblock(int sd);
static status_t soblock(int sd);
static void set_fds(fd_set *fds);
static void fd_dispatch(fd_set *fds);

/*
 * Init/Final
 */
status_t network_init(void)
{
    pool_init(&sock_pool, MAX_SOCK_POOL_SIZE);
    pool_init(&sock_node_pool, MAX_SOCK_NODE_POOL_SIZE);

    max_fd = 0;
    list_init(&fd_list);
    memset(&read_fds, 0, sizeof(fd_set));

    return CORE_OK;
}
status_t network_final(void)
{
    if (pool_size(&sock_pool) != pool_avail(&sock_pool))
        d_error("%d not freed in sock_pool[%d]",
            pool_size(&sock_pool) - pool_avail(&sock_pool),
            pool_size(&sock_pool));
    d_trace(3, "%d not freed in sock_pool[%d]\n",
            pool_size(&sock_pool) - pool_avail(&sock_pool),
            pool_size(&sock_pool));
    if (pool_size(&sock_node_pool) != pool_avail(&sock_node_pool))
        d_error("%d not freed in sock_node_pool[%d]",
            pool_size(&sock_node_pool) - pool_avail(&sock_node_pool),
            pool_size(&sock_node_pool));
    d_trace(3, "%d not freed in sock_node_pool[%d]\n",
            pool_size(&sock_node_pool) - pool_avail(&sock_node_pool),
            pool_size(&sock_node_pool));
    pool_final(&sock_pool);
    pool_final(&sock_node_pool);

    return CORE_OK;
}

/*
 * Socket
 */
status_t sock_create(sock_id *new)
{
    sock_t *sock = NULL;

    pool_alloc_node(&sock_pool, &sock);
    d_assert(sock, return CORE_ENOMEM,);
    memset(sock, 0, sizeof(sock_t));

    sock->fd = -1;

    *new = (sock_id)sock;

    return CORE_OK;
}

status_t sock_delete(sock_id id)
{
    sock_t *sock = (sock_t *)id;
    d_assert(id, return CORE_ERROR,);

    if (sock_is_registered(id))
        sock_unregister(id);
    if (sock->fd >= 0)
        close(sock->fd);
    sock->fd = -1;

    pool_free_node(&sock_pool, sock);
    return CORE_OK;
}

status_t sock_socket(sock_id *new, int family, int type, int protocol)
{
    status_t rv;
    sock_t *sock = NULL;

    rv = sock_create(new);
    d_assert(rv == CORE_OK, return CORE_ERROR,);

    sock = (sock_t *)(*new);

    sock->family = family;
    sock->fd = socket(sock->family, type, protocol);
    if (sock->fd < 0)
    {
        d_warn("socket create(%d:%d:%d) failed(%d:%s)",
                sock->family, type, protocol, errno, strerror(errno));
        return CORE_ERROR;
    }

    d_trace(1, "socket create(%d:%d:%d)\n", sock->family, type, protocol);

    return CORE_OK;
}

status_t sock_setsockopt(sock_id id, c_int32_t opt, c_int32_t on)
{
    sock_t *sock = (sock_t *)id;
    int one;
    status_t rv;

    d_assert(sock, return CORE_ERROR,);
    if (on)
        one = 1;
    else
        one = 0;

    switch(opt)
    {
        case SOCK_O_REUSEADDR:
            if (on != sock_is_option_set(sock, SOCK_O_REUSEADDR))
            {
                if (setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR,
                            (void *)&one, sizeof(int)) == -1)
                {
                    return errno;
                }
                sock_set_option(sock, SOCK_O_REUSEADDR, on);
            }
            break;
        case SOCK_O_NONBLOCK:
            if (sock_is_option_set(sock, SOCK_O_NONBLOCK) != on)
            {
                if (on)
                {
                    if ((rv = sononblock(sock->fd)) != CORE_OK) 
                        return rv;
                }
                else
                {
                    if ((rv = soblock(sock->fd)) != CORE_OK)
                        return rv;
                }
                sock_set_option(sock, SOCK_O_NONBLOCK, on);
            }
            break;
        default:
            d_error("Not implemented(%d)", opt);
            return CORE_EINVAL;
    }

    return CORE_OK; 
}         

status_t sock_bind(sock_id id, c_sockaddr_t *addr)
{
    sock_t *sock = (sock_t *)id;
    char buf[CORE_ADDRSTRLEN];
    socklen_t addrlen;

    d_assert(sock, return CORE_ERROR,);
    d_assert(addr, return CORE_ERROR,);

    addrlen = sockaddr_len(addr);
    d_assert(addrlen, return CORE_ERROR,);

    if (bind(sock->fd, &addr->sa, addrlen) != 0)
    {
        d_error("socket bind(%s:%d) failed(%d:%s)",
                CORE_ADDR(addr, buf), CORE_PORT(addr), errno, strerror(errno));
        return CORE_ERROR;
    }

    memcpy(&sock->local_addr, addr, sizeof(sock->local_addr));

    d_trace(1, "socket bind %s:%d\n", CORE_ADDR(addr, buf), CORE_PORT(addr));

    return CORE_OK;
}

status_t sock_connect(sock_id id, c_sockaddr_t *addr)
{
    sock_t *sock = (sock_t *)id;
    char buf[CORE_ADDRSTRLEN];
    socklen_t addrlen;

    d_assert(sock, return CORE_ERROR,);
    d_assert(addr, return CORE_ERROR,);

    addrlen = sockaddr_len(addr);
    d_assert(addrlen, return CORE_ERROR,);

    if (connect(sock->fd, &addr->sa, addrlen) != 0)
    {
        d_error("socket connect(%s:%d) failed(%d:%s)",
                CORE_ADDR(addr, buf), CORE_PORT(addr), errno, strerror(errno));
        return CORE_ERROR;
    }

    memcpy(&sock->remote_addr, addr, sizeof(sock->remote_addr));

    d_trace(1, "socket connect %s:%d\n", CORE_ADDR(addr, buf), CORE_PORT(addr));

    return CORE_OK;
}

status_t sock_listen(sock_id id)
{
    int rc;
    sock_t *sock = (sock_t *)id;
    d_assert(sock, return CORE_ERROR,);

    rc = listen(sock->fd, 5);
    if (rc < 0)
    {
        d_error("listen failed(%d:%s)", errno, strerror(errno));
        return CORE_ERROR;
    }

    return CORE_OK;
}

status_t sock_accept(sock_id *new, sock_id id)
{
    sock_t *sock = (sock_t *)id;
    sock_t *new_sock = NULL;

    int new_fd = -1;
    c_sockaddr_t addr;
    socklen_t addrlen;

    memset(&addr, 0, sizeof(addr));
    addrlen = sizeof(addr.ss);

    d_assert(id, return CORE_ERROR,);

    new_fd = accept(sock->fd, &addr.sa, &addrlen);
    if (new_fd < 0)
    {
        d_error("accept failed(%d:%s)", errno, strerror(errno));
        return CORE_ERROR;
    }

    pool_alloc_node(&sock_pool, &new_sock);
    d_assert(new_sock, return CORE_ENOMEM,);
    memset(new_sock, 0, sizeof(sock_t));

    new_sock->family = sock->family;
    new_sock->fd = new_fd;

    memcpy(&new_sock->remote_addr, &addr, sizeof(new_sock->remote_addr));

    *new = (sock_id)new_sock;

    return CORE_OK;
}

int sock_family(sock_id id)
{
    sock_t *sock = (sock_t *)id;
    d_assert(id, return -1,);

    return sock->family;
}
c_sockaddr_t *sock_local_addr(sock_id id)
{
    sock_t *sock = (sock_t *)id;
    d_assert(id, return NULL,);

    return &sock->local_addr;
}
c_sockaddr_t *sock_remote_addr(sock_id id)
{
    sock_t *sock = (sock_t *)id;
    d_assert(id, return NULL,);

    return &sock->remote_addr;
}

/*
 * Socket Address
 */

sock_node_t *sock_add_node(list_t *list, c_sockaddr_t *sa_list)
{
    sock_node_t *node = NULL;

    d_assert(list, return NULL,);
    d_assert(sa_list, return NULL,);

    pool_alloc_node(&sock_node_pool, &node);
    d_assert(node, return NULL,);
    memset(node, 0, sizeof(sock_node_t));

    node->list = sa_list;

    list_append(list, node);

    return node;
}

status_t sock_remove_node(list_t *list, sock_node_t *node)
{
    d_assert(node, return CORE_ERROR,);

    list_remove(list, node);

    core_freeaddrinfo(node->list);
    pool_free_node(&sock_node_pool, node);

    return CORE_OK;
}

status_t sock_remove_all_nodes(list_t *list)
{
    sock_node_t *node = NULL, *next_node = NULL;
    
    node = list_first(list);
    while(node)
    {
        next_node = list_next(node);

        sock_remove_node(list, node);

        node = next_node;
    }

    return CORE_OK;
}

status_t sock_probe_node(list_t *list, list_t *list6, c_uint16_t port)
{
    sock_node_t *node = NULL;
	struct ifaddrs *iflist, *cur;
    int rc;

	rc = getifaddrs(&iflist);
    if (rc != 0)
    {
        d_error("getifaddrs failed(%d:%s)", errno, strerror(errno));
        return CORE_ERROR;
    }

	for (cur = iflist; cur != NULL; cur = cur->ifa_next)
    {
        c_sockaddr_t *addr = NULL;

		if (cur->ifa_addr == NULL) /* may happen with ppp interfaces */
			continue;

        addr = (c_sockaddr_t *)cur->ifa_addr;
        if (cur->ifa_addr->sa_family == AF_INET)
        {
#ifndef IN_IS_ADDR_LOOPBACK
#define IN_IS_ADDR_LOOPBACK(a) \
  ((((long int) (a)->s_addr) & ntohl(0xff000000)) == ntohl(0x7f000000))
#endif /* IN_IS_ADDR_LOOPBACK */

/* An IP equivalent to IN6_IS_ADDR_UNSPECIFIED */
#ifndef IN_IS_ADDR_UNSPECIFIED
#define IN_IS_ADDR_UNSPECIFIED(a) \
  (((long int) (a)->s_addr) == 0x00000000)
#endif /* IN_IS_ADDR_UNSPECIFIED */
            if (IN_IS_ADDR_UNSPECIFIED(&addr->sin.sin_addr) ||
                IN_IS_ADDR_LOOPBACK(&addr->sin.sin_addr))
                continue;
        }
        else if (cur->ifa_addr->sa_family == AF_INET6)
        {
            if (IN6_IS_ADDR_UNSPECIFIED(&addr->sin6.sin6_addr) ||
                IN6_IS_ADDR_LOOPBACK(&addr->sin6.sin6_addr) ||
                IN6_IS_ADDR_MULTICAST(&addr->sin6.sin6_addr) ||
                IN6_IS_ADDR_LINKLOCAL(&addr->sin6.sin6_addr) ||
                IN6_IS_ADDR_SITELOCAL(&addr->sin6.sin6_addr))
                continue;
        }
        else
            continue;

        addr = core_calloc(1, sizeof(c_sockaddr_t));
        d_assert(addr, return CORE_ERROR,);
        memcpy(&addr->sa, cur->ifa_addr, sockaddr_len(cur->ifa_addr));
        addr->c_sa_port = htons(port);

        pool_alloc_node(&sock_node_pool, &node);
        d_assert(node, return CORE_ERROR,);
        memset(node, 0, sizeof(sock_node_t));

        node->list = addr;

        if (addr->c_sa_family == AF_INET)
        {
            if (list) list_append(list, node);
        }
        else if (addr->c_sa_family == AF_INET6)
        {
            if (list6) list_append(list6, node);
        }
        else
            d_assert(0, return CORE_ERROR,);

	}

	freeifaddrs(iflist);

    return CORE_OK;
}

status_t core_getaddrinfo(c_sockaddr_t **sa_list, 
        int family, const char *hostname, c_uint16_t port, int flags)
{
    *sa_list = NULL;
    return core_addaddrinfo(sa_list, family, hostname, port, flags);
}

status_t core_addaddrinfo(c_sockaddr_t **sa_list, 
        int family, const char *hostname, c_uint16_t port, int flags)
{
    int rc;
    char service[NI_MAXSERV];
    struct addrinfo hints, *ai, *ai_list;
    c_sockaddr_t *prev;
    char buf[CORE_ADDRSTRLEN];

    d_assert(sa_list, return CORE_ERROR,);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = flags;

    snprintf(service, sizeof(service), "%u", port);

    rc = getaddrinfo(hostname, service, &hints, &ai_list);
    if (rc != 0)
    {
        d_error("getaddrinfo(%d:%s:%d:0x%x) failed(%d:%s)",
                family, hostname, port, flags, errno, strerror(errno));
        return CORE_ERROR;
    }

    prev = NULL;
    if (*sa_list)
    {
        prev = *sa_list;
        while(prev->next) prev = prev->next;
    }
    ai = ai_list;
    while(ai)
    {
        c_sockaddr_t *new;
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
        {
            ai = ai->ai_next;
            continue;
        }

        new = core_calloc(1, sizeof(c_sockaddr_t));
        memcpy(&new->sa, ai->ai_addr, ai->ai_addrlen);
        new->c_sa_port = htons(port);
        d_trace(3, "addr:%s, port:%d\n", CORE_ADDR(new, buf), port);

        if (!prev)
            *sa_list = new;
        else
            prev->next = new;

        prev = new;
        ai = ai->ai_next;
    }
    freeaddrinfo(ai_list);

    if (prev == NULL)
    {
        d_error("core_getaddrinfo(%d:%s:%d:%d) failed(%d:%s)",
                family, hostname, port, flags, errno, strerror(errno));
        return CORE_ERROR;
    }

    return CORE_OK;
}

status_t core_filteraddrinfo(c_sockaddr_t **sa_list, int family)
{
    c_sockaddr_t *addr = NULL, *prev = NULL, *next = NULL;

    d_assert(sa_list, return CORE_ERROR,);

    prev = NULL;
    addr = *sa_list;
    while(addr)
    {
        next = addr->next;

        if (addr->c_sa_family != family)
        {
            if (prev)
                prev->next = addr->next;
            else
                *sa_list = addr->next;
            core_free(addr);

        }
        else
        {
            prev = addr;
        }

        addr = next;
    }

    return CORE_OK;
}

status_t core_copyaddrinfo(c_sockaddr_t **dst, const c_sockaddr_t *src)
{
    c_sockaddr_t *d;
    const c_sockaddr_t *s;

    for (*dst = d = NULL, s = src; s; s = s->next)
    {
        if (!d)
        {
            d = core_calloc(1, sizeof *s);
            *dst = memcpy(d, s, sizeof *s);
        }
        else
        {
            d->next = core_calloc(1, sizeof(c_sockaddr_t));
            d = memcpy(d->next, s, sizeof *s);
        }
    }
    return CORE_OK;
}

status_t core_sortaddrinfo(c_sockaddr_t **sa_list, int family)
{
    c_sockaddr_t *head = NULL, *addr = NULL, *new = NULL, *old = NULL;

    d_assert(sa_list, return CORE_ERROR,);

    old = *sa_list;
    while(old)
    {
        addr = old;

        old = old->next;

        if (head == NULL || addr->c_sa_family == family)
        {
            addr->next = head;
            head = addr;
        }
        else
        {
            new = head;
            while(new->next != NULL && new->next->c_sa_family != family)
            {
                new = new->next;
            }
            addr->next = new->next;
            new->next = addr;
        }
    }
    
    *sa_list = head;

    return CORE_OK;
}

status_t core_freeaddrinfo(c_sockaddr_t *sa_list)
{
    c_sockaddr_t *next = NULL, *addr = NULL;

    addr = sa_list;
    while(addr)
    {
        next = addr->next;
        core_free(addr);
        addr = next;
    }

    return CORE_OK;
}

const char *core_inet_ntop(void *sa, char *buf, int buflen)
{
    int family;
    c_sockaddr_t *sockaddr = NULL;

    d_assert(buf, return NULL,);
    sockaddr = sa;
    d_assert(sockaddr, return NULL,);

    family = sockaddr->c_sa_family;
    switch(family)
    {
        case AF_INET:
            d_assert(buflen >= INET_ADDRSTRLEN, return NULL,);
            return inet_ntop(family,
                    &sockaddr->sin.sin_addr, buf, INET_ADDRSTRLEN);
        case AF_INET6:
            d_assert(buflen >= CORE_ADDRSTRLEN, return NULL,);
            return inet_ntop(family,
                    &sockaddr->sin6.sin6_addr, buf, INET6_ADDRSTRLEN);
        default:
            d_assert(0, return NULL, "Unknown family(%d)", family);
    }
}

status_t core_inet_pton(int family, const char *src, void *sa)
{
    c_sockaddr_t *dst = NULL;

    d_assert(src, return CORE_ERROR,);
    dst = sa;
    d_assert(dst, return CORE_ERROR,);

    dst->c_sa_family = family;
    switch(family)
    {
        case AF_INET:
            return inet_pton(family, src, &dst->sin.sin_addr) == 1 ?
                CORE_OK : CORE_ERROR;
        case AF_INET6:
            return inet_pton(family, src, &dst->sin6.sin6_addr) == 1 ?
                 CORE_OK : CORE_ERROR;
        default:
            d_assert(0, return CORE_ERROR, "Unknown family(%d)", family);
    }
}

socklen_t sockaddr_len(const void *sa)
{
    const c_sockaddr_t *sockaddr = sa;

    d_assert(sa, return 0,);

    switch(sockaddr->c_sa_family)
    {
        case AF_INET:
            return sizeof(struct sockaddr_in);
        case AF_INET6:
            return sizeof(struct sockaddr_in6);
        default:
            d_assert(0, return 0, "Unknown family(%d)", sockaddr->c_sa_family);
    }
}

int sockaddr_is_equal(void *p, void *q)
{
    c_sockaddr_t *a, *b;

    a = p;
    d_assert(a, return 0,);
    b = q;
    d_assert(b, return 0,);

    if (a->c_sa_family != b->c_sa_family)
        return 0;

    if (a->c_sa_family == AF_INET && memcmp(
        &a->sin.sin_addr, &b->sin.sin_addr, sizeof(struct in_addr)) == 0)
        return 1;
    else if (a->c_sa_family == AF_INET6 && memcmp(
        &a->sin6.sin6_addr, &b->sin6.sin6_addr, sizeof(struct in6_addr)) == 0)
        return 1;
    else
        d_assert(0, return 0, "Unknown family(%d)", a->c_sa_family);

    return 0;
}

/*
 * Send/Recv
 */
ssize_t sock_write(sock_id id, const void *buf, size_t len)
{
    sock_t *sock = (sock_t *)id;
    ssize_t size;

    d_assert(id, return -1,);

    size = write(sock->fd, buf, len);
    if (size < 0)
    {
        d_error("sock_write(len:%ld) failed(%d:%s)",
                len, errno, strerror(errno));
    }

    return size;
}

ssize_t sock_read(sock_id id, void *buf, size_t len)
{
    sock_t *sock = (sock_t *)id;
    ssize_t size;

    d_assert(id, return -1,);

    size = read(sock->fd, buf, len);
    if (size < 0)
    {
        d_error("sock_read(len:%ld) failed(%d:%s)",
                len, errno, strerror(errno));
    }

    return size;
}

ssize_t core_send(sock_id id, const void *buf, size_t len, int flags)
{
    sock_t *sock = (sock_t *)id;
    ssize_t size;

    d_assert(id, return -1,);

    size = send(sock->fd, buf, len, flags);
    if (size < 0)
    {
        d_error("core_send(len:%ld) failed(%d:%s)",
                len, errno, strerror(errno));
    }

    return size;
}

ssize_t core_sendto(sock_id id,
        const void *buf, size_t len, int flags, const c_sockaddr_t *to)
{
    sock_t *sock = (sock_t *)id;
    ssize_t size;
    socklen_t addrlen;

    d_assert(id, return -1,);
    d_assert(to, return -1,);

    addrlen = sockaddr_len(to);
    d_assert(addrlen, return CORE_ERROR,);

    size = sendto(sock->fd, buf, len, flags, &to->sa, addrlen);
    if (size < 0)
    {
        d_error("core_sendto(len:%ld) failed(%d:%s)",
                len, errno, strerror(errno));
    }

    return size;
}

ssize_t core_recv(sock_id id, void *buf, size_t len, int flags)
{
    sock_t *sock = (sock_t *)id;
    ssize_t size;

    d_assert(id, return -1,);

    size = recv(sock->fd, buf, len, flags);
    if (size < 0)
    {
        d_error("core_recv(len:%ld) failed(%d:%s)",
                len, errno, strerror(errno));
    }

    return size;
}

ssize_t core_recvfrom(sock_id id,
        void *buf, size_t len, int flags, c_sockaddr_t *from)
{
    sock_t *sock = (sock_t *)id;
    ssize_t size;
    socklen_t addrlen = sizeof(struct sockaddr_storage);

    d_assert(id, return -1,);
    d_assert(from, return -1,);

    size = recvfrom(sock->fd, buf, len, flags, &from->sa, &addrlen);
    if (size < 0)
    {
        d_error("corek_recvfrom(len:%ld) failed(%d:%s)",
                len, errno, strerror(errno));
    }

    return size;
}

/*
 * Select Loop
 */
status_t sock_register(sock_id id, sock_handler handler, void *data) 
{
    sock_t *sock = (sock_t *)id;

    d_assert(id, return CORE_ERROR,);

    if (sock_is_registered(id))
    {
        d_error("socket has already been registered");
        return CORE_ERROR;
    }

    if (sock_setsockopt(id, SOCK_O_NONBLOCK, 1) == CORE_ERROR)
    {
        d_error("cannot set socket to non-block");
        return CORE_ERROR;
    }

    if (sock->fd > max_fd)
    {
        max_fd = sock->fd;
    }
    sock->handler = handler;
    sock->data = data;

    list_append(&fd_list, sock);

    return CORE_OK;
}

status_t sock_unregister(sock_id id)
{
    d_assert(id, return CORE_ERROR,);

    list_remove(&fd_list, id);

    return CORE_OK;
}

int sock_is_registered(sock_id id)
{
    sock_t *sock = (sock_t *)id;
    sock_t *iter = NULL;

    d_assert(id, return CORE_ERROR,);
    for (iter = list_first(&fd_list); iter != NULL; iter = list_next(iter))
    {
        if (iter == sock)
        {
            return 1;
        }
    }

    return 0;
}

int sock_select_loop(c_time_t timeout)
{
    struct timeval tv;
    int rc;

    if (timeout > 0)
    {
        tv.tv_sec = time_sec(timeout);
        tv.tv_usec = time_usec(timeout);
    }

    set_fds(&read_fds);

    rc = select(max_fd + 1, &read_fds, NULL, NULL, timeout > 0 ? &tv : NULL);
    if (rc < 0)
    {
        if (errno != EINTR && errno != 0)
        {
            if (errno == EBADF)
            {
                d_error("[FIXME] socket should be closed here(%d:%s)", 
                        errno, strerror(errno));
            }
            else
            {
                d_error("select failed(%d:%s)", errno, strerror(errno));
            }
        }
        return rc;
    }

    /* Timeout */
    if (rc == 0)
    {
        return rc;
    }

    /* Dispatch Handler */
    fd_dispatch(&read_fds);

    return 0;
}

static status_t soblock(int sd)
{
/* BeOS uses setsockopt at present for non blocking... */
#ifndef BEOS
    int fd_flags;

    fd_flags = fcntl(sd, F_GETFL, 0);
#if defined(O_NONBLOCK)
    fd_flags &= ~O_NONBLOCK;
#elif defined(O_NDELAY)
    fd_flags &= ~O_NDELAY;
#elif defined(FNDELAY)
    fd_flags &= ~FNDELAY;
#else
#error Please teach CORE how to make sockets blocking on your platform.
#endif
    if (fcntl(sd, F_SETFL, fd_flags) == -1)
    {
        return errno;
    }
#else
    int on = 0;
    if (setsockopt(sd, SOL_SOCKET, SO_NONBLOCK, &on, sizeof(int)) < 0)
        return errno;
#endif /* BEOS */
    return CORE_OK;
}

static status_t sononblock(int sd)
{
#ifndef BEOS
    int fd_flags;

    fd_flags = fcntl(sd, F_GETFL, 0);
#if defined(O_NONBLOCK)
    fd_flags |= O_NONBLOCK;
#elif defined(O_NDELAY)
    fd_flags |= O_NDELAY;
#elif defined(FNDELAY)
    fd_flags |= FNDELAY;
#else
#error Please teach CORE how to make sockets non-blocking on your platform.
#endif
    if (fcntl(sd, F_SETFL, fd_flags) == -1)
    {
        return errno;
    }
#else
    int on = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_NONBLOCK, &on, sizeof(int)) < 0)
        return errno;
#endif /* BEOS */
    return CORE_OK;
}

static void set_fds(fd_set *fds)
{
    sock_t *sock = NULL;

    FD_ZERO(fds);
    for (sock = list_first(&fd_list); sock != NULL; sock = list_next(sock))
    {
        FD_SET(sock->fd, fds);
    }
}

static void fd_dispatch(fd_set *fds)
{
    sock_t *sock = NULL;

    for (sock = list_first(&fd_list); sock != NULL; sock = list_next(sock))
    {
        if (FD_ISSET(sock->fd, fds))
        {
            if (sock->handler)
            {
                sock->handler((sock_id)sock, sock->data);
            }
        }
    }
}
