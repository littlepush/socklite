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

void sl_socket_close(SOCKET_T so);

// TCP Methods
SOCKET_T sl_tcp_socket_init();
bool sl_tcp_socket_connect(SOCKET_T tso, const sl_peerinfo& peer, function<void(SOCKET_T)> callback);
bool sl_tcp_socket_send(SOCKET_T tso, const string &pkg);
bool sl_tcp_socket_monitor(SOCKET_T tso, function<void(SOCKET_T)> callback);
bool sl_tcp_socket_read(SOCKET_T tso, string& buffer, size_t max_buffer_size = 4098);

// UDP Methods
SOCKET_T sl_udp_socket_init();
bool sl_udp_socket_send(SOCKET_T uso, const string &pkg, const sl_peerinfo& peer);
bool sl_udp_socket_monitor(SOCKET_T uso, function<void(SOCKET_T, struct sockaddr_in)> callback);
bool sl_udp_socket_read(SOCKET_T uso, struct sockaddr_in addr, string& buffer, size_t max_buffer_size = 512);

#endif 
// sock.raw.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
