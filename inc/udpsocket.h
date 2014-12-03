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
protected:
    char m_buffer[1024];
public:
    // The socket handler
    SOCKET_T m_socket;
    SOCKET_T m_parent; 
    struct sockaddr_in m_sock_addr;
    socklen_t m_so_len;
    string m_data;

    sl_udpsocket();
    virtual ~sl_udpsocket();

    // Connect to peer
    virtual bool connect( const string &ipaddr, u_int32_t port );
    // Listen on specified port and address, default is 0.0.0.0
    virtual bool listen( u_int32_t port, u_int32_t ipaddr = INADDR_ANY );
    // Close the connection
    virtual void close();
    // When the socket is a listener, use this method 
    // to accept client's connection.
    virtual sl_socket *get_client( u_int32_t timeout = 100 );
    virtual void release_client( sl_socket *client );

    // Set current socket reusable or not
    virtual bool set_reusable( bool reusable = true );

    // Read data from the socket until timeout or get any data.
    virtual bool read_data( string &buffer, u_int32_t timeout = 1000 );
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