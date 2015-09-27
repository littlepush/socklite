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

#ifndef __SOCK_LITE_UDPSOCKET_H__
#define __SOCK_LITE_UDPSOCKET_H__

#include "socket.h"

// UDP socket
class sl_udpsocket : public sl_socket
{
public:
    struct sockaddr_in m_sock_addr;

    sl_udpsocket(bool iswrapper = false);
    sl_udpsocket(SOCKET_T so);
    sl_udpsocket(SOCKET_T so, struct sockaddr_in addr);

    virtual ~sl_udpsocket();

    // The IP Address information for peer socket
    string & ipaddress( string & ipstr ) const;
    // The Port of peer socket
    uint32_t port() const;

    // Connect to peer
    virtual bool connect( const string &ipaddr, uint32_t port, uint32_t timeout = 1000 );
    // Listen on specified port and address, default is 0.0.0.0
    virtual bool listen( uint32_t port, uint32_t ipaddr = INADDR_ANY );

    // Read data from the socket until timeout or get any data.
    virtual SO_READ_STATUE read_data( string &buffer, uint32_t timeout = 1000 );
    // Only try to read data once, the socket must receive SL_EVENT_DATA by the poller
    SO_READ_STATUE recv(string &buffer, unsigned int max_buffer_len = 512);
    // Write data to peer.
    virtual bool write_data( const string &data );
};

#endif

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
