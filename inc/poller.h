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
#include <unordered_map>

#define CO_MAX_SO_EVENTS		1024

// All Socket Event
enum SL_EVENT_ID {
    SL_EVENT_ACCEPT         = 0x01,
    SL_EVENT_DATA           = 0x02,     // READ
    SL_EVENT_READ           = 0x02,
    SL_EVENT_FAILED         = 0x04,
    SL_EVENT_CONNECT        = 0x08,
    SL_EVENT_WRITE          = 0x08,     // Write and connect is the same
    SL_EVENT_TIMEOUT        = 0x10,

    SL_EVENT_DEFAULT        = (SL_EVENT_ACCEPT | SL_EVENT_DATA | SL_EVENT_FAILED),
    SL_EVENT_ALL            = 0x1F
};

// Convert the EVENT_ID to string
const string sl_event_name(uint32_t eid);

/*
    The event structure for a system epoll/kqueue to set the info
    of a socket event.

    @so: the socket you should act some operator on it.
    @source: the tcp listening socket which accept current so, 
        in udp socket or other events of tcp socket, source will
        be an INVALIDATE_SOCKET
    @event: the event current socket get.
    @socktype: IPPROTO_TCP or IPPROTO_UDP
    @address: the address info of a udp socket when it gets some
        incoming data, otherwise it will be undefined.
*/
typedef struct tag_sl_event {
    SOCKET_T                so;
    SOCKET_T                source;
    SL_EVENT_ID             event;
    int                     socktype;
    struct sockaddr_in      address;    // For UDP socket usage.
} sl_event;

/*
    Output of the event
    The format will be:
        "event SL_EVENT_xxx SL_EVENT_xxx for xxx socket <so>"
*/
ostream & operator << (ostream &os, const sl_event & e);

// Create a failed or timedout event structure object
sl_event sl_event_make_failed(SOCKET_T so = INVALIDATE_SOCKET);
sl_event sl_event_make_timeout(SOCKET_T so = INVALIDATE_SOCKET);

/*
    Epoll|Kqueue Manager Class
    The class is a singleton class, the whole system should only
    create one epoll|kqueue file descriptor to monitor all sockets
    or file descriptors

*/
class sl_poller
{
public:
    // Event list type.
	typedef std::vector<sl_event>	earray;
protected:
	int 				m_fd;
#if SL_TARGET_LINUX
	struct epoll_event	*m_events;
#elif SL_TARGET_MAC
	struct kevent		*m_events;
#endif

    // TCP Listening Socket Map
	unordered_map<SOCKET_T, bool>       m_tcp_svr_map;

    // Timeout Info
    unordered_map<SOCKET_T, time_t>     m_timeout_map;
    mutex                               m_timeout_mutex;

protected:
    // Cannot create a poller object, it should be a Singleton instance
	sl_poller();
public:
	~sl_poller();
        
	// Bind the server side socket
	bool bind_tcp_server( SOCKET_T so );

	// Try to fetch new events(Only return SL_EVENT_DEFAULT)
	size_t fetch_events( earray &events,  unsigned int timedout = 1000 );

	// Start to monitor a socket hander
	// In default, the poller will maintain the socket infinite, if
	// `oneshot` is true, then will add the ONESHOT flag
    // Default time out of a socket in epoll/kqueue will be 30 seconds
	bool monitor_socket(  
        SOCKET_T so, 
        bool oneshot = false, 
        uint32_t eid = SL_EVENT_DEFAULT, 
        uint32_t timedout = 30 
    );

    /*
        When close a socket, remove the socket from the
        timeout map. The socket will auto be removed from epoll/kqueue.
    */
    void unmonitor_socket(SOCKET_T so);

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
