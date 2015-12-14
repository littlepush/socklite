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
//typedef void (*async_dns_handler)(const vector<sl_ip>& ipaddr);
typedef std::function<void(const vector<sl_ip> &)>      async_dns_handler;

// Try to get the dns result async
void sl_async_gethostname(const string& host, async_dns_handler fp);

// Close the socket and release the handler set
void sl_socket_close(SOCKET_T so);

// TCP Methods
SOCKET_T sl_tcp_socket_init();
// Async connect to the peer
bool sl_tcp_socket_connect(SOCKET_T tso, const sl_peerinfo& peer, sl_socket_event_handler callback);
// Async connect to the host via a socks5 proxy
bool sl_tcp_socket_connect(SOCKET_T tso, const sl_peerinfo& socks5, const string& host, uint16_t port, sl_socket_event_handler callback);
bool sl_tcp_socket_send(SOCKET_T tso, const string &pkg);
bool sl_tcp_socket_monitor(SOCKET_T tso, sl_socket_event_handler callback, bool new_incoming = false);
bool sl_tcp_socket_read(SOCKET_T tso, string& buffer, size_t max_buffer_size = 4098);
bool sl_tcp_socket_listen(SOCKET_T tso, const sl_peerinfo& bind_port, sl_socket_event_handler accept_callback);
sl_peerinfo sl_tcp_get_original_dest(SOCKET_T tso);

// UDP Methods
SOCKET_T sl_udp_socket_init(const sl_peerinfo& bind_addr = sl_peerinfo::nan());
bool sl_udp_socket_send(SOCKET_T uso, const string &pkg, const sl_peerinfo& peer);
bool sl_udp_socket_monitor(SOCKET_T uso, const sl_peerinfo& peer, sl_socket_event_handler callback);
bool sl_udp_socket_read(SOCKET_T uso, struct sockaddr_in addr, string& buffer, size_t max_buffer_size = 512);
bool sl_udp_socket_listen(SOCKET_T uso, sl_socket_event_handler accept_callback);

#endif 
// sock.raw.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
