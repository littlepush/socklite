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

#include "events.h"

static const int __sl_bitorder[32] = {
    0, 1, 2, 6, 3, 11, 7, 16,
    4, 14, 12, 21, 8, 23, 17, 26,
    31, 5, 10, 15, 13, 20, 22, 25,
    30, 9, 19, 24, 29, 18, 28, 27
};

// Get the index of the last bit which is 1
#define SL_MACRO_LAST_1_INDEX(x)     (__sl_bitorder[((unsigned int)(((x) & -(x)) * 0x04653ADFU)) >> 27])

std::map<SOCKET_T, sl_handler_set> & _sl_event_map() {
    static std::map<SOCKET_T, sl_handler_set> _g_emap;
    return _g_emap;
}

mutex & _sl_event_mutex() {
    static mutex _egm;
    return _egm;
}

sl_handler_set sl_event_empty_handler()
{
    sl_handler_set _s;
    memset((void *)&_s, 0, sizeof(sl_handler_set));
    return _s;
}

// Bind the event handler set
void sl_event_bind_handler(SOCKET_T so, sl_handler_set&& hset)
{
    lock_guard<mutex> _(_sl_event_mutex());
    _sl_event_map()[so] = hset;
}
// Unbind the event handler set
void sl_event_unbind_handler(SOCKET_T so)
{
    lock_guard<mutex> _(_sl_event_mutex());
    _sl_event_map().erase(so);
}
// Search for the handler set
sl_handler_set&& sl_event_find_handler(SOCKET_T so)
{
    lock_guard<mutex> _(_sl_event_mutex());
    if ( _sl_event_map().find(so) == end(_sl_event_map()) ) {
        return move(sl_event_empty_handler());
    }
    return move(_sl_event_map()[so]);
}
// Update the handler for specifial event
void sl_event_update_handler(SOCKET_T so, SL_EVENT_ID eid, sl_socket_event_handler&& h)
{
    if ( eid == 0 ) return;
    if ( eid & 0xFFFFFFE0 ) return; // Invalidate event flag
    lock_guard<mutex> _(_sl_event_mutex());
    auto _hit = _sl_event_map().find(so);
    if ( _hit == end(_sl_event_map()) ) return;
    (&_hit->second.on_accept)[31 - SL_MACRO_LAST_1_INDEX(eid)] = h;
}

// sl_events member functions
sl_events::sl_events()
: timepiece_(10), rl_callback_(NULL), is_running_(false)
{
    lock_guard<mutex> _(running_lock_);
    this->_internal_start_runloop();
}

sl_events::~sl_events()
{
    this->stop_run();
}

sl_events& sl_events::server()
{
    static sl_events _ge;
    return _ge;
}

void sl_events::_internal_start_runloop()
{
    // If already running, just return
    if ( is_running_ ) return;
    is_running_ = true;

    runloop_thread_ = new thread([this]{
        _internal_runloop();
    });
}

void sl_events::_internal_runloop()
{
    thread_agent();

    while ( this_thread_is_running() ) {
        sl_poller::earray _event_list;
        uint32_t _tp = 10;
        sl_runloop_callback _fp = NULL;
        do {
            lock_guard<mutex> _(running_lock_);
            _tp = timepiece_;
            _fp = rl_callback_;
        } while(false);

        do {
            lock_guard<mutex> _(events_lock_);
            _event_list = move(pending_events_);
        } while(false);

        size_t _ecount = sl_poller::server().fetch_events(_event_list, _tp) + _event_list.size();
        if ( _ecount != 0 ) {
            for ( auto &e : _event_list ) {
                sl_handler_set _hs = sl_event_find_handler(e.so);
                switch(e.event) {
                case SL_EVENT_ACCEPT: _hs.on_accept(e); break;
                case SL_EVENT_DATA: _hs.on_data(e); break;
                case SL_EVENT_FAILED: _hs.on_failed(e); break;
                case SL_EVENT_WRITE: _hs.on_write(e); break;
                case SL_EVENT_CONNECT: _hs.on_connect(e); break;
                default: break;
                };
            }
        }
        // Invoke the callback
        if ( _fp != NULL ) {
            _fp();
        }
    }

    do {
        lock_guard<mutex> _(running_lock_);
        is_running_ = false;
    } while( false );
}

unsigned int sl_events::pending_socket_count()
{
    lock_guard<mutex> _(events_lock_);
    return pending_events_.size();
}

void sl_events::bind(sl_socket *pso, sl_handler_set&& hset) {
    if ( pso == NULL || SOCKET_NOT_VALIDATE(pso->m_socket) ) return;
    sl_event_bind_handler(pso->m_socket, move(hset));
}

void sl_events::unbind( sl_socket *pso ) {
    if ( pso == NULL || SOCKET_NOT_VALIDATE(pso->m_socket) ) return;
    sl_event_unbind_handler(pso->m_socket);
}
void sl_events::update_handler(sl_socket *pso, SL_EVENT_ID eid, sl_socket_event_handler&& h)
{
    if ( pso == NULL || SOCKET_NOT_VALIDATE(pso->m_socket) ) return;
    sl_event_update_handler(pso->m_socket, eid, move(h));
}
void sl_events::bind( SOCKET_T so, sl_handler_set&& hset )
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    sl_event_bind_handler(so, move(hset));
}
void sl_events::unbind( SOCKET_T so )
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    sl_event_unbind_handler(so);
}
void sl_events::update_handler( SOCKET_T so, SL_EVENT_ID eid, sl_socket_event_handler&& h)
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    sl_event_update_handler(so, eid, move(h));
}

bool sl_events::is_running() const
{
    lock_guard<mutex> _(running_lock_);
    return is_running_;
}

void sl_events::run(uint32_t timepiece, sl_runloop_callback cb)
{
    lock_guard<mutex> _(running_lock_);
    timepiece_ = timepiece;
    rl_callback_ = cb;

    this->_internal_start_runloop();
}

void sl_events::stop_run()
{
    do {
        lock_guard<mutex> _(running_lock_);
        if ( is_running_ == false ) return;
        safe_join_thread(runloop_thread_->get_id());
    } while ( false );
    if ( runloop_thread_->joinable() ) runloop_thread_->join();
    delete runloop_thread_;
    runloop_thread_ = NULL;
}

void sl_events::add_event(sl_event && e)
{
    lock_guard<mutex> _(events_lock_);
    pending_events_.emplace_back(e);
}
void sl_events::add_tcpevent(SOCKET_T so, SL_EVENT_ID eid)
{
    lock_guard<mutex> _(events_lock_);
}
void sl_events::add_udpevent(SOCKET_T so, struct sockaddr_in addr, SL_EVENT_ID eid)
{
    lock_guard<mutex> _(events_lock_);
}

// events.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
