/*
    socklite -- a C++ socket library for Linux/Windows/iOS
    Copyright (C) 2014  Push Chen

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

    You can connect me by email: littlepush@gmail.com, 
    or @me on twitter: @littlepush
*/

#pragma once

#ifndef __SOCK_LITE_RAW_H__
#define __SOCK_LITE_RAW_H__

#include "socket.h"
#include "poller.h"
#include "events.h"
#include "socks5.h"
#include "dns.h"
#include "string_format.hpp"

// Async to get the dns resolve result
typedef std::function<void(const vector<sl_ip> &)>      async_dns_handler;

/*!
    Try to get the dns result async
    @Description
    Use async udp/tcp socket to send a dns query request to the domain name server.
    If has multiple nameserver set in the system, will try all the sever in order
    till the first have a no-error response.
    The method will use a UDP socket at first, if the answer is a TC package, then
    force to send TCP request to the same server again.
    If the server does not response after timeout(5s), will try to use the next
    server in the list.
    If all server failed to answer the query, then will return 255.255.255.255 
    as the IP address of the host to query in the result.

    This method will always return an IP address.
*/
void sl_async_gethostname(const string& host, async_dns_handler fp);

/*
    Try to get the dns result async via specified name servers
*/
void sl_async_gethostname(
    const string& host, 
    const vector<sl_peerinfo>& nameserver_list, 
    async_dns_handler fp
);

/*
    Try to get the dns result via specified name servers through a socks5 proxy.
    THis will force to use tcp connection to the nameserver
*/
void sl_async_gethostname(
    const string& host, 
    const vector<sl_peerinfo>& nameserver_list, 
    const sl_peerinfo &socks5, 
    async_dns_handler fp
);

// Async to redirect the dns query request.
typedef std::function<void(const sl_dns_packet&)>       async_dns_redirector;

/*!
    Redirect a dns query packet to the specified nameserver, and return the 
    dns response packet from the server.
    If specified the socks5 proxy, will force to use tcp redirect.
*/
void sl_async_redirect_dns_query(
    const sl_dns_packet & dpkt,
    const sl_peerinfo &nameserver,
    const sl_peerinfo &socks5,
    async_dns_redirector fp
);

/*
    Bind Default Failed Handler for a Socket

    @Description
    Bind the default handler for SL_EVENT_FAILED of a socket.
    In any case if the socket receive a SL_EVENT_FAILED event, will
    invoke this handler.
    Wether set this handler or not, system will close the socket
    automatically. Which means, if you receive a SL_EVENT_FAILED
    event, the socket assigned in the sl_event structure has
    already been closed.
*/
void sl_socket_bind_event_failed(SOCKET_T so, sl_socket_event_handler handler);

/*
    Bind Default TimedOut Handler for a Socket

    @Description
    Bind the default timedout handler for SL_EVENT_TIMEOUT of a socket.
    If a socket receive a timedout event, the system will invoke this
    handler.
    If not bind this handler, system will close the socket automatically,
    otherwise, a timedout socket will NOT be closed.
*/
void sl_socket_bind_event_timeout(SOCKET_T so, sl_socket_event_handler handler);

/*!
    Close the socket and release the handler set 

    @Description
    This method will close the socket(udp or tcp) and release all cache/buffer
    associalate with it.
*/
void sl_socket_close(SOCKET_T so);

/*
    Monitor the socket for incoming data.

    @Description
    As reading action will block current thread if there is no data right now,
    this method will add an EPOLLIN(Linux)/EVFILT_READ(BSD) event to the queue.

    In Linux, as epoll will combine read and write flag in one set, this method
    will always monitor both EPOLLIN and EPOLLOUT.
    For a BSD based system use kqueue, will only add a EVFILT_READ to the queue.
*/
void sl_socket_monitor(
    SOCKET_T tso, 
    uint32_t timedout,
    sl_socket_event_handler callback
);

/*
    Async connect to the host via a socks5 proxy

    @Description
    Connect to host:port via a socks5 proxy.
    If the socks5 proxy is not set(like sl_peerinfo::nan()), will try to
    connect to the host in directly connection.
    If the host is not an sl_ip, then will invoke <sl_async_gethostname>
    to resolve the host first.

    If the host is connected syncized, this method will add a SL_EVENT_CONNECT
    to the events runloop and the caller will be noticed at the next
    timepiece.

    The default timeout time is 30 seconds(30000ms).
*/
void sl_tcp_socket_connect(
    const sl_peerinfo& socks5, 
    const string& host, 
    uint16_t port,
    uint32_t timedout,
    sl_socket_event_handler callback
);

/*
    Async send a packet to the peer via current socket.

    @Description
    This method will append the packet to the write queue of the socket,
    then check if current socket is writing or not.
    If is now writing, the method will return directly. Otherwise,
    this method will make the socket to monitor SL_EVENT_WRITE.

    In Linux, this method will always monitor both EPOLLIN and EPOLLOUT
*/
void sl_tcp_socket_send(
    SOCKET_T tso, 
    const string &pkt, 
    sl_socket_event_handler callback = NULL
);

/*
    Read incoming data from the socket.

    @Description
    This is a block method to read data from the socket.
    
    The socket must be NON_BLOCKING. This method will use a loop
    to fetch all data on the socket till two conditions:
    1. the buffer is not full after current recv action
    2. receive a EAGAIN or EWOULDBLOCK signal

    The method will increase the buffer's size after each loop 
    until reach the max size of string, which should be the size
    of machine memory in default.
*/
bool sl_tcp_socket_read(
    SOCKET_T tso, 
    string& buffer, 
    size_t min_buffer_size = 1024   // 1K
);

/*
    Listen on a tcp port

    @Description
    Listen on a specified tcp port on sepcified interface.
    The bind_port is the listen port info of the method.
    If you want to listen on port 4040 on all interface, set 
    <bind_port> as "0.0.0.0:4040" or sl_peerinfo(INADDR_ANY, 4040).
    If you want to listen only the internal network, like 192.168.1.0/24
    set the <bind_port> like "192.168.1.1:4040"

    The accept callback will return a new incoming socket, which
    has not been monited on any event.
*/
SOCKET_T sl_tcp_socket_listen(
    const sl_peerinfo& bind_port, 
    sl_socket_event_handler accept_callback
);

/*
    Get original peer info of a socket.

    @Description
    This method will return the original connect peerinfo of a socket
    in Linux with iptables redirect by fetch the info with SO_ORIGINAL_DST
    flag.

    In a BSD(like Mac OS X), will return 0.0.0.0:0
*/
sl_peerinfo sl_tcp_get_original_dest(SOCKET_T tso);

/*
    Redirect a socket's data to another peer via socks5 proxy.

    @Description
    This method will continuously redirect the data between from_so and the 
    peer side socket. 
    When one side close or drop the connection, this method will close
    both side's sockets.
*/
void sl_tcp_socket_redirect(
    SOCKET_T from_so,
    const sl_peerinfo& peer,
    const sl_peerinfo& socks5
);

// UDP Methods

/*
    Initialize a UDP socket

    @Description
    This method will create a UDP socket and bind to the <bind_addr>
    The ipaddress in bind_addr should always be INADDR_ANY.

    As the UDP socket is connectionless, if you want to receive any
    data on specified port, you must set the port at this time.

    In order to get the local infomation of the udp socket,
    the method will bind port 0 to this socket in default.
*/
SOCKET_T sl_udp_socket_init(
    const sl_peerinfo& bind_addr = sl_peerinfo::nan(),
    sl_socket_event_handler failed = NULL, 
    sl_socket_event_handler timedout = NULL
);

/*
    Send packet to the peer.

    @Description
    This method is an async send method.
    It will push the packet to the end of the write queue and 
    try to monitor the SL_EVENT_WRITE flag of the socket.
*/
void sl_udp_socket_send(
    SOCKET_T uso,
    const sl_peerinfo& peer,
    const string &pkt,
    sl_socket_event_handler callback = NULL
);

/*
    Listen on a UDP port and wait for any incoming data.

    @Description
    As a UDP socket is connectionless, the only different between
    listen and monitor is 'listen' will auto re-monitor the socket
    after a data incoming message has been processed.
*/
void sl_udp_socket_listen(
    SOCKET_T uso, 
    sl_socket_event_handler accept_callback
);

/*
    Block and read data from the UDP socket.

    @Description
    Same as tcp socket read method.
*/
bool sl_udp_socket_read(
    SOCKET_T uso, 
    struct sockaddr_in addr, 
    string& buffer, 
    size_t min_buffer_size = 512
);

#endif 
// sock.raw.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
