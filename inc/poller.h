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

#ifndef __SOCK_LITE_POLLER_H__
#define __SOCK_LITE_POLLER_H__

#include "socket.h"
    
#if SL_TARGET_LINUX
#include <sys/epoll.h>
#elif SL_TARGET_MAC
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>
#endif

#include <vector>
#include <map>

#define CO_MAX_SO_EVENTS		1024

// All Socket Event
enum SL_EVENT_ID {
    SL_EVENT_ACCEPT         = 0x01,
    SL_EVENT_DATA           = 0x02,     // READ
    SL_EVENT_READ           = 0x02,
    SL_EVENT_FAILED         = 0x04,
    SL_EVENT_CONNECT        = 0x08,
    SL_EVENT_WRITE          = 0x08,     // Write and connect is the same

    SL_EVENT_DEFAULT        = (SL_EVENT_ACCEPT | SL_EVENT_DATA | SL_EVENT_FAILED),
    SL_EVENT_ALL            = 0x1F
};

typedef struct tag_sl_event {
    SOCKET_T                so;
    SOCKET_T                source;
    SL_EVENT_ID             event;
    int                     socktype;
    struct sockaddr_in      address;    // For UDP socket usage.
} sl_event;

class sl_poller
{
public:
	typedef std::vector<sl_event>	earray;
protected:
	int 				m_fd;
#if SL_TARGET_LINUX
	struct epoll_event	*m_events;
#elif SL_TARGET_MAC
	struct kevent		*m_events;
#endif

	std::map<SOCKET_T, bool>	m_tcp_svr_map;
	std::map<SOCKET_T, bool>	m_udp_svr_map;

    bool                m_runloop_status;
    int                 m_runloop_ret;

protected:
	sl_poller();
public:
	~sl_poller();
        
	// Bind the server side socket
	void bind_tcp_server( SOCKET_T so );
	void bind_udp_server( SOCKET_T so );

	// Try to fetch new events(Only return SL_EVENT_DEFAULT)
	size_t fetch_events( earray &events,  unsigned int timedout = 1000 );

	// Start to monitor a socket hander
	// In default, the poller will maintain the socket infinite, if
	// `oneshot` is true, then will add the ONESHOT flag
	void monitor_socket( SOCKET_T so, bool oneshot = false, bool isreset = false );

	// Singleton Poller Item
	static sl_poller &server();
};

#endif

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
