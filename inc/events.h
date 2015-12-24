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
    sl_socket_event_handler         on_timedout;
} sl_handler_set;

/*
    Event Run Loop Class
    This class is a singleton. It will fetch the Poller every 
    <timespice> milleseconds.

    The class has at least one worker thread, and will auto increase
    or decrease according to the pending unprocessed events.
*/
class sl_events
{
public:
    typedef union {
        struct {
            uint32_t    timeout;
            uint32_t    eventid;
        } flags;
        uint64_t        event_info;
    } event_mask;

    // Return an empty handler set 
    static sl_handler_set empty_handler();

    // Socket Handler Set Map Type
    typedef map<SOCKET_T, sl_handler_set>       shsmap_t;

    // Socket Event Mask Map Type
    typedef unordered_map<SOCKET_T, event_mask> semmap_t;

protected:
    // Protected constructure
    sl_events();

    // Any action associates with the events or event handler
    // need to lock this mutex.
    mutable mutex           handler_mutex_;
    // Any validate socket need to bind en empty handler set to
    // sl_events, this map is used to store the relation between
    // a socket fd and a handler set.
    shsmap_t                handler_map_;

    // Any action associates with the event mask need to lock this mutex
    mutable mutex           event_mutex_;
    // Before monitor, before fetching, and after fetching, 
    // will re-order this map for all monitoring events.
    semmap_t                event_unprocessed_map_;
    semmap_t                event_unfetching_map_;

    // Internal Run Loop Properties.
    // Change of time piece and runloop callback should lock this
    // mutex at the first line
    mutable mutex           running_lock_;
    // Fetching Epoll/Kqueue's timeout setting
    uint32_t                timepiece_;
    // Callback method after each fetching action.
    sl_runloop_callback     rl_callback_;

    // Internal Runloop Working Thread.
    // This is the main thread object of Event System.
    thread *                runloop_thread_;

    // Manager Thread Info
    // All pending events are in this pool.
    event_pool<sl_event>    events_pool_;
    // Working thread poll
    vector<thread*>         thread_pool_;
    // Working thread monitor manager thread.
    thread *                thread_pool_manager_;

    // Start the Internal Run Loop Thread use the method: _internal_runloop
    void _internal_start_runloop();
    // The working thread method of the event runloop.
    void _internal_runloop();

    // Add a new worker to the thread pool and fetch pending
    // event from event_pool_
    void _internal_add_worker();
    // Remove the last worker from the thread pool
    void _internal_remove_worker();
    // The worker thread method.
    void _internal_worker();

    // Replace a hander of a socket's specified Event ID, return the old handler
    sl_socket_event_handler _replace_handler(SOCKET_T so, uint32_t eid, sl_socket_event_handler h);
    // Fetch the handler of a socket's specified Event ID, remine the old handler unchanged.
    sl_socket_event_handler _fetch_handler(SOCKET_T so, SL_EVENT_ID eid);
    // Check if the socket has the handler of specified Event ID
    bool _has_handler(SOCKET_T so, SL_EVENT_ID eid);
public:

    ~sl_events();

    // return the singleton instance of sl_events
    static sl_events& server();

    // Bind a handler set to a socket
    void bind( SOCKET_T so, sl_handler_set&& hset );
    // Remove the handler set of a socket
    void unbind( SOCKET_T so );
    // Update the handler of a specified event id.
    void update_handler( SOCKET_T so, uint32_t eid, sl_socket_event_handler&& h);
    // Append a handler to current handler set
    void append_handler( SOCKET_T so, uint32_t eid, sl_socket_event_handler h);
    // Check if the socket has specified event id's handler.    
    bool has_handler(SOCKET_T so, SL_EVENT_ID eid);

    // Monitor the socket for specified event.
    void monitor(SOCKET_T so, SL_EVENT_ID eid, sl_socket_event_handler handler, uint32_t timedout = 30);

    // Add an event to the socket's pending event pool.
    void add_event(sl_event && e);
    // Add a tcp socket's event, everything else in sl_event struct will be remined un-defined.
    void add_tcpevent(SOCKET_T so, SL_EVENT_ID eid);
    // Add a udp socket's event, evenything else in sl_event struct will be remined un-defined.
    void add_udpevent(SOCKET_T so, struct sockaddr_in addr, SL_EVENT_ID eid);

    // Setup the timepiece and callback method.
    void setup( uint32_t timepiece = 10, sl_runloop_callback cb = NULL );
};

#endif
// events.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
