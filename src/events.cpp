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

sl_handler_set sl_events::empty_handler() {
    sl_handler_set _s;
    memset((void *)&_s, 0, sizeof(sl_handler_set));
    return _s;
}
// sl_events member functions
sl_events::sl_events()
: timepiece_(10), rl_callback_(NULL), is_running_(false)
{
    lock_guard<mutex> _(running_lock_);
    this->_internal_start_runloop();
    this->_internal_add_worker();
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
    this->_internal_add_worker();
    thread_pool_manager_ = new thread([this]{
        thread_agent _ta;

        while ( true ) {
            usleep(10000);
            if ( !this_thread_is_running() ) break;
            if ( events_pool_.size() > (thread_pool_.size() * 10) ) {
                this->_internal_add_worker();
            } else if ( events_pool_.size() < (thread_pool_.size() * 2) && thread_pool_.size() > 1 ) {
                this->_internal_remove_worker();
            }
        }
    });  
}

void sl_events::_internal_runloop()
{
    thread_agent _ta;

    //ldebug << "internal runloop started" << lend;
    while ( this_thread_is_running() ) {
        //ldebug << "runloop is still running" << lend;
        sl_poller::earray _event_list;
        uint32_t _tp = 10;
        sl_runloop_callback _fp = NULL;
        do {
            lock_guard<mutex> _(running_lock_);
            //ldebug << "copy the timpiece and callback" << lend;
            _tp = timepiece_;
            _fp = rl_callback_;
        } while(false);

        //ldebug << "current pending events: " << _event_list.size() << lend;
        size_t _ecount = sl_poller::server().fetch_events(_event_list, _tp);
        if ( _ecount != 0 ) {
            //ldebug << "fetch some events, will process them" << lend;
            for ( auto &e : _event_list ) {
                events_pool_.notify_one(move(e));
            }
        }
        // Invoke the callback
        if ( _fp != NULL ) {
            _fp();
        }
    }

    linfo << "internal runloop will terminated" << lend;

    do {
        lock_guard<mutex> _(running_lock_);
        is_running_ = false;
    } while( false );
}

void sl_events::_internal_add_worker()
{
    thread *_worker = new thread([this](){
        _internal_worker();
    });
    thread_pool_.push_back(_worker);
}
void sl_events::_internal_remove_worker()
{
    if ( thread_pool_.size() == 0 ) return;
    thread *_last_worker = *thread_pool_.rbegin();
    thread_pool_.pop_back();
    if ( _last_worker->joinable() ) {
        safe_join_thread(_last_worker->get_id());
        _last_worker->join();
    }
    delete _last_worker;
}
void sl_events::_internal_worker()
{
    linfo << "strat a new worker thread " << this_thread::get_id() << lend;
    thread_agent _ta;

    sl_event _local_event;
    while ( this_thread_is_running() ) {
        if ( !events_pool_.wait_for(milliseconds(10), [&](sl_event&& e){
            _local_event = e;
        }) ) continue;
        SOCKET_T _s = ((_local_event.event == SL_EVENT_ACCEPT) && 
                        (_local_event.socktype == IPPROTO_TCP)) ? 
                        _local_event.source : _local_event.so;
        SL_EVENT_ID _e = ((_local_event.event == SL_EVENT_DATA) && 
                            (_local_event.so == _local_event.source) && 
                            (_local_event.socktype == IPPROTO_UDP)) ?
                            SL_EVENT_ACCEPT : _local_event.event;
        //ldebug << "processing socket " << _s << " for event " << _e << lend;
        sl_handler_set _hs = this->_find_handler(_s);
        // Remove current event handler
        if ( _e != SL_EVENT_ACCEPT ) {
            //this->bind(_local_event.so, move(sl_events::empty_handler()));
            this->update_handler(_local_event.so, _e, NULL);
        }
        sl_socket_event_handler _seh = (&_hs.on_accept)[SL_MACRO_LAST_1_INDEX(_e)];
        if ( !_seh ) {
            lwarning << "No handler bind for event: " << 
                _e << " on socket " << _s << lend;
        } else {
            _seh(_local_event);
        }
    }

    linfo << "the worker " << this_thread::get_id() << " will exit" << lend;
}

unsigned int sl_events::pending_socket_count()
{
    return events_pool_.size();
}

void sl_events::bind( SOCKET_T so, sl_handler_set&& hset )
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    lock_guard<mutex> _(handler_mutex_);
    event_map_.emplace(so, move(hset));
}
void sl_events::unbind( SOCKET_T so )
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    lock_guard<mutex> _(handler_mutex_);
    event_map_.erase(so);
}
void sl_events::update_handler( SOCKET_T so, SL_EVENT_ID eid, sl_socket_event_handler&& h)
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    if ( eid == 0 ) return;
    if ( eid & 0xFFFFFFE0 ) return; // Invalidate event flag
    lock_guard<mutex> _(handler_mutex_);
    auto _hit = event_map_.find(so);
    if ( _hit == end(event_map_) ) return;
    (&_hit->second.on_accept)[SL_MACRO_LAST_1_INDEX(eid)] = h;
}
bool sl_events::has_handler(SOCKET_T so, SL_EVENT_ID eid)
{
    if ( SOCKET_NOT_VALIDATE(so) ) return false;
    if ( eid == 0 ) return false;
    if ( eid & 0xFFFFFFE0 ) return false;
    lock_guard<mutex> _(handler_mutex_);
    auto _hit = event_map_.find(so);
    if ( _hit == end(event_map_) ) return false;
    sl_socket_event_handler _seh = (&_hit->second.on_accept)[SL_MACRO_LAST_1_INDEX(eid)];
    return (bool)_seh;
}
sl_handler_set sl_events::_find_handler(SOCKET_T so) {
    lock_guard<mutex> _(handler_mutex_);
    auto _hit = event_map_.find(so);
    if ( _hit == end(event_map_) ) return sl_events::empty_handler();
    return _hit->second;
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
    } while ( false );

    if ( runloop_thread_->joinable() )  {
        safe_join_thread(runloop_thread_->get_id());
        runloop_thread_->join();
    }
    delete runloop_thread_;
    runloop_thread_ = NULL;

    // Close the thread pool manager
    if ( thread_pool_manager_->joinable() ) {
        safe_join_thread(thread_pool_manager_->get_id());
        thread_pool_manager_->join();
    }
    delete thread_pool_manager_;
    thread_pool_manager_ = NULL;

    // Close all worker in thread pool
    while ( thread_pool_.size() > 0 ) {
        this->_internal_remove_worker();
    }
}

void sl_events::add_event(sl_event && e)
{
    //lock_guard<mutex> _(events_lock_);
    events_pool_.notify_one(move(e));
}
void sl_events::add_tcpevent(SOCKET_T so, SL_EVENT_ID eid)
{
    //lock_guard<mutex> _(events_lock_);
    sl_event _e;
    _e.so = so;
    _e.source = INVALIDATE_SOCKET;
    _e.event = eid;
    _e.socktype = IPPROTO_TCP;
    events_pool_.notify_one(move(_e));
}
void sl_events::add_udpevent(SOCKET_T so, struct sockaddr_in addr, SL_EVENT_ID eid)
{
    //lock_guard<mutex> _(events_lock_);
    sl_event _e;
    _e.so = so;
    _e.source = INVALIDATE_SOCKET;
    _e.event = eid;
    _e.socktype = IPPROTO_UDP;
    memcpy(&_e.address, &addr, sizeof(addr));
    events_pool_.notify_one(move(_e));
}

// events.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
