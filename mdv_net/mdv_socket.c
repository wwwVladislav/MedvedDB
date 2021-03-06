#include "mdv_socket.h"
#include <mdv_log.h>
#include <mdv_limits.h>
#include <unistd.h>

#ifdef MDV_PLATFORM_LINUX
    #include <linux/tcp.h>
    #include <sys/types.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <arpa/inet.h>
#endif


static volatile int mdv_netlib_init_count = 0;


mdv_errno mdv_netlib_init()
{
    if (mdv_netlib_init_count++ == 0)
    {
        // TODO
    }

    return MDV_OK;
}


void mdv_netlib_uninit()
{
    if (--mdv_netlib_init_count == 0)
    {
        // TODO
    }
}


mdv_descriptor mdv_socket(mdv_socket_type type)
{
    if (mdv_netlib_init() != MDV_OK)
        return MDV_INVALID_DESCRIPTOR;

    static const int types[] = { SOCK_STREAM, SOCK_DGRAM };

    int s = socket(AF_INET, types[type], 0);

    if (s == -1)
    {
        int err = mdv_error();
        char err_msg[128];
        MDV_LOGE("Socket creation failed with error: '%s' (%d)",
                    mdv_strerror(err, err_msg, sizeof err_msg), err);
        return MDV_INVALID_DESCRIPTOR;
    }

    MDV_LOGD("Socket %d opened", s);

    mdv_descriptor sock = 0;

    *(int*)&sock = s;

    return sock;
}


void mdv_socket_close(mdv_descriptor sock)
{
    if (sock != MDV_INVALID_DESCRIPTOR)
    {
        int s = *(int*)&sock;
        close(s);
        MDV_LOGD("Socket %d closed", s);
        mdv_netlib_uninit();
    }
}


void mdv_socket_shutdown(mdv_descriptor sock, int how)
{
    int s = *(int*)&sock;

    if ((how & MDV_SOCK_SHUT_RD) && (how & MDV_SOCK_SHUT_WR))
        shutdown(s, SHUT_RDWR);
    else if (how & MDV_SOCK_SHUT_RD)
        shutdown(s, SHUT_RD);
    else
        shutdown(s, SHUT_WR);
}


mdv_errno mdv_socket_reuse_addr(mdv_descriptor sock)
{
    int optval = 1;

    int s = *(int*)&sock;

    int err = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (err == -1)
    {
        MDV_LOGE("Unable to reuse address");
        return mdv_error();
    }

    return MDV_OK;
}


mdv_errno mdv_socket_reuse_port(mdv_descriptor sock)
{
    int optval = 1;

    int s = *(int*)&sock;

    int err = setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    if (err == -1)
    {
        MDV_LOGE("Unable to reuse port");
        return mdv_error();
    }

    return MDV_OK;
}

mdv_errno mdv_socket_nonblock(mdv_descriptor sock)
{
    int s = *(int*)&sock;

    int err = fcntl(s, F_SETFL, O_NONBLOCK);

    if (err == -1)
    {
        MDV_LOGE("Switching socket %d to nonblocking mode failed", s);
        return mdv_error();
    }

    return MDV_OK;
}



mdv_errno mdv_socket_tcp_keepalive(mdv_descriptor sock, int keepidle, int keepcnt, int keepintvl)
{
    int optval = 1;

    int s = *(int*)&sock;

    if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof optval) == -1 ||
        setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof keepcnt) == -1 ||
        setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof keepintvl) == -1 ||
        setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof keepidle) == -1)
    {
        MDV_LOGE("Unable to enable KEEPALIVE option for TCP socket");
        return mdv_error();
    }

    return MDV_OK;
}


mdv_errno mdv_socket_tcp_defer_accept(mdv_descriptor sock)
{
    int optval = 1;

    int s = *(int*)&sock;

    if (setsockopt(s, IPPROTO_TCP, TCP_DEFER_ACCEPT, &optval, sizeof optval) == -1)
    {
        MDV_LOGE("Unable to enable TCP_DEFER_ACCEPT option for TCP socket");
        return mdv_error();
    }

    return MDV_OK;
}


mdv_errno mdv_socket_tcp_cork(mdv_descriptor sock)
{
    int optval = 1;

    int s = *(int*)&sock;

    if (setsockopt(s, IPPROTO_TCP, TCP_CORK, &optval, sizeof optval) == -1)
    {
        MDV_LOGE("Unable to enable TCP_CORK option for TCP socket");
        return mdv_error();
    }

    return MDV_OK;
}


mdv_errno mdv_socket_min_recv_size(mdv_descriptor sock, int size)
{
    int s = *(int*)&sock;

    if (setsockopt(s, SOL_SOCKET, SO_RCVLOWAT, &size, sizeof size) == -1)
    {
        MDV_LOGE("Unable to enable SO_RCVLOWAT option for TCP socket");
        return mdv_error();
    }

    return MDV_OK;

}


mdv_errno mdv_socket_min_send_size(mdv_descriptor sock, int size)
{
    int s = *(int*)&sock;

    if (setsockopt(s, SOL_SOCKET, SO_SNDLOWAT, &size, sizeof size) == -1)
    {
        MDV_LOGE("Unable to enable SO_SNDLOWAT option for TCP socket");
        return mdv_error();
    }

    return MDV_OK;
}


mdv_errno mdv_socket_listen(mdv_descriptor sock)
{
    int s = *(int*)&sock;

    int err = listen(s, MDV_LISTEN_BACKLOG);

    if (err == -1)
    {
        MDV_LOGE("Port listening failed (socket: %d)", s);
        return mdv_error();
    }

    return MDV_OK;
}


mdv_descriptor mdv_socket_accept(mdv_descriptor sock, mdv_sockaddr *peer)
{
    int s = *(int*)&sock;

    socklen_t addr_len = sizeof(mdv_sockaddr);

    int peer_sock = accept(s, (struct sockaddr *)peer, peer ? &addr_len : 0);

    if (peer_sock == -1)
    {
        mdv_errno err = mdv_error();

        if (err != MDV_EAGAIN)
        {
            char err_msg[128];
            MDV_LOGE("Peer accepting failed with error '%s' (%d) (socket: %d)",
                                mdv_strerror(err, err_msg, sizeof err_msg), err, s);
        }

        return MDV_INVALID_DESCRIPTOR;
    }

    MDV_LOGD("Socket %d accepted", peer_sock);

    return (mdv_descriptor)(size_t)peer_sock;
}


mdv_errno mdv_socket_bind(mdv_descriptor sock, mdv_sockaddr const *addr)
{
    int s = *(int*)&sock;

    const struct sockaddr *saddr = (const struct sockaddr *)addr;

    int err = bind(s, saddr, sizeof(struct sockaddr));

    if (err == -1)
    {
        MDV_LOGE("Socket %d binding failed", s);
        return mdv_error();
    }

    return MDV_OK;
}


mdv_errno mdv_socket_connect(mdv_descriptor sock, mdv_sockaddr const *addr)
{
    int s = *(int*)&sock;

    const struct sockaddr *saddr = (const struct sockaddr *)addr;

    if (connect(s, saddr, sizeof(mdv_sockaddr)) == -1)
    {
        mdv_errno err = mdv_error();

        if (err != MDV_INPROGRESS)
            MDV_LOGW("Connection failed (socket: %d)", s);

        return err;
    }

    return MDV_OK;
}


uint32_t mdv_hton32(uint32_t hostlong)
{
    return htonl(hostlong);
}

uint16_t mdv_hton16(uint16_t hostshort)
{
    return htons(hostshort);
}

uint32_t mdv_ntoh32(uint32_t netlong)
{
    return ntohl(netlong);
}

uint16_t mdv_ntoh16(uint16_t netshort)
{
    return ntohs(netshort);
}
