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

    // ldebug << "in internal start runloop method, will add a new worker" << lend;
    this->_internal_add_worker();
    thread_pool_manager_ = new thread([this]{
        thread_agent _ta;

        while ( true ) {
            usleep(10000);
            bool _has_broken = false;
            do {
                _has_broken = false;
                size_t _broken_thread_index = -1;
                for ( size_t i = 0; i < thread_pool_.size(); ++i ) {
                    if ( thread_pool_[i]->joinable() ) continue;
                    _broken_thread_index = i;
                    _has_broken = true;
                    break;
                }
                if ( _has_broken ) {
                    delete thread_pool_[_broken_thread_index];
                }
            } while( _has_broken );
            if ( !this_thread_is_running() ) break;
            if ( events_pool_.size() > (thread_pool_.size() * 10) ) {
                // ldebug << "event pending count: " << events_pool_.size() << ", worker thread pool size: " << thread_pool_.size() << lend;
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
            events_pool_.notify_lots(_event_list, &handler_mutex_, [&](const sl_event &&e) {
                pending_map_[((((uint64_t)e.so) << 4) | e.event)] = true;
            });
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
        thread_agent _ta;
        try {
            _internal_worker();
        } catch (exception e) {
            lcritical << "got exception in side the internal worker " << this_thread::get_id() << lend;
        }
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

    sl_event _local_event;
    sl_socket_event_handler _handler;
    while ( this_thread_is_running() ) {
        if ( !events_pool_.wait_for(milliseconds(10), [&](sl_event&& e){
            // ldebug << "processing " << e << lend;
            _local_event = e;
            SOCKET_T _s = ((_local_event.event == SL_EVENT_ACCEPT) && 
                            (_local_event.socktype == IPPROTO_TCP)) ? 
                            _local_event.source : _local_event.so;
            SL_EVENT_ID _e = ((_local_event.event == SL_EVENT_DATA) && 
                                (_local_event.so == _local_event.source) && 
                                (_local_event.socktype == IPPROTO_UDP)) ?
                                SL_EVENT_ACCEPT : _local_event.event;
            lock_guard<mutex> _(handler_mutex_);
            pending_map_.erase(((((uint64_t)_s) << 4) | _e));
            if ( _e != SL_EVENT_ACCEPT ) {
                _handler = this->_replace_handler(_s, _e, NULL);
#if SL_TARGET_LINUX
                if ( _e == SL_EVENT_FAILED ) return;
                SL_EVENT_ID _re_event = (_e == SL_EVENT_DATA) ? SL_EVENT_WRITE : SL_EVENT_DATA;
                // ldebug << "check if the socket " << _s << " has monitoring other event like " << sl_event_name(_re_event) << lend;
                if ( this->_has_handler(_s, _re_event) ) {
                    // ldebug << "socket " << _e << " do monitoring " << sl_event_name(_re_event) << ", try to re-monitor it" << lend;
                    sl_poller::server().monitor_socket(_s, true, _re_event, true);
                }
#endif
            } else {
                _handler = this->_fetch_handler(_s, _e);
            }
        }) ) continue;

        if ( _handler ) {
            _handler(_local_event);
        }
    }

    linfo << "the worker " << this_thread::get_id() << " will exit" << lend;
}

sl_socket_event_handler sl_events::_replace_handler(SOCKET_T so, SL_EVENT_ID eid, sl_socket_event_handler h)
{
    sl_socket_event_handler _h = NULL;
    auto _hit = event_map_.find(so);
    if ( _hit == end(event_map_) ) return _h;
    _h = (&_hit->second.on_accept)[SL_MACRO_LAST_1_INDEX(eid)];
    (&_hit->second.on_accept)[SL_MACRO_LAST_1_INDEX(eid)] = h;
    return _h;
}
sl_socket_event_handler sl_events::_fetch_handler(SOCKET_T so, SL_EVENT_ID eid)
{
    sl_socket_event_handler _h = NULL;
    auto _hit = event_map_.find(so);
    if ( _hit == end(event_map_) ) return _h;
    _h = (&_hit->second.on_accept)[SL_MACRO_LAST_1_INDEX(eid)];
    return _h;
}

bool sl_events::_has_handler(SOCKET_T so, SL_EVENT_ID eid)
{
    // ldebug << "in _has_handler, try to search socket " << so << " with event: " << sl_event_name(eid) << lend;
    uint64_t _pe_search_key = ((((uint64_t)so) << 4) | eid);
    if ( pending_map_.find(_pe_search_key) == end(pending_map_) ) {
        // Not in pending, then find the event map
        // ldebug << "the event " << sl_event_name(eid) << " for socket " << so << " is not in pending map, try to search handler map" << lend;
        auto _hit = event_map_.find(so);
        if ( _hit == end(event_map_) ) return false;
        // ldebug << "get the handler set of the socket " << so << lend;
        sl_socket_event_handler _seh = (&_hit->second.on_accept)[SL_MACRO_LAST_1_INDEX(eid)];
        return (bool)_seh;
    } else {
        // Already in pending, then no more handler
        return false;
    }
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
    return this->_has_handler(so, eid);
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
    // do {
    //     lock_guard<mutex> _(running_lock_);
    //     if ( is_running_ == false ) return;
    // } while ( false );

    if ( runloop_thread_ != NULL && runloop_thread_->joinable() )  {
        safe_join_thread(runloop_thread_->get_id());
        runloop_thread_->join();
    }
    if ( runloop_thread_ != NULL ) {
        delete runloop_thread_;
        runloop_thread_ = NULL;
    }

    // Close the thread pool manager
    if ( thread_pool_manager_ != NULL && thread_pool_manager_->joinable() ) {
        safe_join_thread(thread_pool_manager_->get_id());
        thread_pool_manager_->join();
    }
    if ( thread_pool_manager_ != NULL ) {
        delete thread_pool_manager_;
        thread_pool_manager_ = NULL;
    }

    // Close all worker in thread pool
    while ( thread_pool_.size() > 0 ) {
        this->_internal_remove_worker();
    }
}

void sl_events::add_event(sl_event && e)
{
    //lock_guard<mutex> _(events_lock_);
    lock_guard<mutex> _(handler_mutex_);
    pending_map_[((((uint64_t)e.so) << 4) | e.event)] = true;
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

    lock_guard<mutex> _(handler_mutex_);
    pending_map_[((((uint64_t)so) << 4) | eid)] = true;
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

    lock_guard<mutex> _(handler_mutex_);
    pending_map_[((((uint64_t)so) << 4) | eid)] = true;
    events_pool_.notify_one(move(_e));
}

// events.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
