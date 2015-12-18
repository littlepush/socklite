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

#ifndef __SOCK_LITE_EVENTS_H__
#define __SOCK_LITE_EVENTS_H__

#include "socket.h"
#include "poller.h"
#include <unordered_map>

// The socket event handler
//typedef void (*sl_socket_event_handler)(sl_event);
typedef std::function<void(sl_event)>   sl_socket_event_handler;

// The callback function for each run loop
//typedef void (*sl_runloop_callback)(void);
typedef std::function<void(void)>       sl_runloop_callback;

typedef struct tag_sl_handler_set {
    sl_socket_event_handler         on_accept;
    sl_socket_event_handler         on_data;
    sl_socket_event_handler         on_failed;
    sl_socket_event_handler         on_write;
} sl_handler_set;

class sl_events
{
public:
    // Return an empty handler set
    static sl_handler_set empty_handler();
    typedef map<SOCKET_T, sl_handler_set>   shsmap_t;       // SOCKET_HANDLER_SET_MAP_TYPE
    typedef unordered_map<uint64_t, bool>   pemap_t;        // Pending Events Map Type
protected:
    mutable mutex           handler_mutex_;
    shsmap_t                event_map_;
    pemap_t                 pending_map_;

    // Protected constructure
    sl_events();

    // Internal Run Loop Properties.
    mutable mutex           running_lock_;
    uint32_t                timepiece_;
    sl_runloop_callback     rl_callback_;

    // Running status
    bool                    is_running_;
    thread *                runloop_thread_;

    // Manager Thread Info
    event_pool<sl_event>    events_pool_;
    vector<thread*>         thread_pool_;
    thread *                thread_pool_manager_;

    void _internal_start_runloop();
    void _internal_runloop();

    void _internal_add_worker();
    void _internal_remove_worker();
    void _internal_worker();

    sl_socket_event_handler _replace_handler(SOCKET_T so, SL_EVENT_ID eid, sl_socket_event_handler h);
    sl_socket_event_handler _fetch_handler(SOCKET_T so, SL_EVENT_ID eid);
    bool _has_handler(SOCKET_T so, SL_EVENT_ID eid);
public:
    ~sl_events();
    // return the singleton instance of sl_events
    static sl_events& server();

    unsigned int pending_socket_count();

    void bind( SOCKET_T so, sl_handler_set&& hset );
    void unbind( SOCKET_T so );
    void update_handler( SOCKET_T so, SL_EVENT_ID eid, sl_socket_event_handler&& h);
    bool has_handler(SOCKET_T so, SL_EVENT_ID eid);

    bool is_running() const;
    void run( uint32_t timepiece = 10, sl_runloop_callback cb = NULL );
    void stop_run();

    void add_event(sl_event && e);
    void add_tcpevent(SOCKET_T so, SL_EVENT_ID eid);
    void add_udpevent(SOCKET_T so, struct sockaddr_in addr, SL_EVENT_ID eid);
};

#endif
// events.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
