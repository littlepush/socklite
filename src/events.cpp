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
: timepiece_(10), rl_callback_(NULL)
{
    lock_guard<mutex> _(running_lock_);
    this->_internal_start_runloop();
}

sl_events::~sl_events()
{
    // Delete the main runloop thread
    if ( runloop_thread_->joinable() ) {
        runloop_thread_->join();
    }
    delete runloop_thread_;
    runloop_thread_ = NULL;

    // Delete the worker thread manager
    if ( thread_pool_manager_->joinable() ) {
        thread_pool_manager_->join();
    }
    delete thread_pool_manager_;
    thread_pool_manager_ = NULL;

    // Remove all worker thread
    // Close all worker in thread pool
    while ( thread_pool_.size() > 0 ) {
        this->_internal_remove_worker();
    }
}

sl_events& sl_events::server()
{
    static sl_events _ge;
    return _ge;
}

void sl_events::_internal_start_runloop()
{
    // If already running, just return
    runloop_thread_ = new thread([this]{
        _internal_runloop();
    });

    // ldebug << "in internal start runloop method, will add a new worker" << lend;
    // Add a worker
    this->_internal_add_worker();

    // Start the worker manager thread
    thread_pool_manager_ = new thread([this]{
        thread_agent _ta;

        while ( this_thread_is_running() ) {
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

        // Combine all pending events
        do {
            lock_guard<mutex> _(event_mutex_);
            for ( 
                auto _ermit = begin(event_remonitor_map_); 
                _ermit != end(event_remonitor_map_);
                ++_ermit
                )
            {
                if ( _ermit->second.unsaved == 0 ) continue;
                if ( _ermit->second.eventid == 0 ) continue;
                #if DEBUG
                ldebug 
                    << "re-monitor on socket " << _ermit->first
                    << " for event " << sl_event_name(_ermit->second.eventid) 
                << lend;
                #endif
                sl_poller::server().monitor_socket(
                    _ermit->first, true, 
                    _ermit->second.eventid, 
                    _ermit->second.timeout
                    );
                _ermit->second.unsaved = 0;
            }
        } while ( false );
        //ldebug << "current pending events: " << _event_list.size() << lend;
        size_t _ecount = sl_poller::server().fetch_events(_event_list, _tp);
        if ( _ecount != 0 ) {
            //ldebug << "fetch some events, will process them" << lend;
            events_pool_.notify_lots(_event_list, &event_mutex_, [this](const sl_event&& e){
                if ( e.event != SL_EVENT_WRITE && e.event != SL_EVENT_DATA ) return;
                auto _ermit = event_remonitor_map_.find(e.so);
                if ( _ermit == end(event_remonitor_map_) ) return;
                _ermit->second.unsaved = 0;
            });
        }
        // Invoke the callback
        if ( _fp != NULL ) {
            _fp();
        }
    }

    linfo << "internal runloop will terminated" << lend;
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
            #if DEBUG
            ldebug << "processing " << e << lend;
            #endif
            _local_event = e;
            SOCKET_T _s = ((_local_event.event == SL_EVENT_ACCEPT) && 
                            (_local_event.socktype == IPPROTO_TCP)) ? 
                            _local_event.source : _local_event.so;

            lock_guard<mutex> _(event_mutex_);

            if ( e.event != SL_EVENT_WRITE && e.event != SL_EVENT_DATA ) {
                _handler = this->_fetch_handler(_s, e.event);
            } else {
                _handler = this->_replace_handler(_s, e.event, NULL);
                auto _ermit = event_remonitor_map_.find(e.so);
                if ( _ermit == end(event_remonitor_map_) ) return;
                _ermit->second.eventid &= (~e.event);
                _ermit->second.unsaved = 1;
            }
        }) ) continue;

        if ( _handler ) {
            _handler(_local_event);
        } else {
            lwarning << "no handler for " << _local_event << lend;
        }
    }

    linfo << "the worker " << this_thread::get_id() << " will exit" << lend;
}

sl_socket_event_handler sl_events::_replace_handler(SOCKET_T so, uint32_t eid, sl_socket_event_handler h)
{
    sl_socket_event_handler _h = NULL;
    auto _hit = handler_map_.find(so);
    if ( _hit == end(handler_map_) ) return _h;
    _h = (&_hit->second.on_accept)[SL_MACRO_LAST_1_INDEX(eid)];
    if ( eid & SL_EVENT_ACCEPT ) {
        _hit->second.on_accept = h;
    }
    if ( eid & SL_EVENT_DATA ) {
        _hit->second.on_data = h;
    }
    if ( eid & SL_EVENT_FAILED ) {
        _hit->second.on_failed = h;
    }
    if ( eid & SL_EVENT_WRITE ) {
        _hit->second.on_write = h;
    }
    if ( eid & SL_EVENT_TIMEOUT ) {
        _hit->second.on_timedout = h;
    }
    return _h;
}
sl_socket_event_handler sl_events::_fetch_handler(SOCKET_T so, SL_EVENT_ID eid)
{
    sl_socket_event_handler _h = NULL;
    auto _hit = handler_map_.find(so);
    if ( _hit == end(handler_map_) ) return _h;
    _h = (&_hit->second.on_accept)[SL_MACRO_LAST_1_INDEX(eid)];
    return _h;
}

bool sl_events::_has_handler(SOCKET_T so, SL_EVENT_ID eid)
{
    auto _ermit = event_remonitor_map_.find(so);
    if ( _ermit == end(event_remonitor_map_) ) return false;
    return (_ermit->second.eventid & eid) > 0;
}

void sl_events::bind( SOCKET_T so, sl_handler_set&& hset )
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    lock_guard<mutex> _(handler_mutex_);
    handler_map_.emplace(so, move(hset));
}
void sl_events::unbind( SOCKET_T so )
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    lock_guard<mutex> _hl(handler_mutex_);
    lock_guard<mutex> _el(event_mutex_);
    handler_map_.erase(so);
    event_remonitor_map_.erase(so);
    sl_poller::server().unmonitor_socket(so);
}
void sl_events::update_handler( SOCKET_T so, uint32_t eid, sl_socket_event_handler&& h)
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    if ( eid == 0 ) return;
    if ( eid & 0xFFFFFFE0 ) return; // Invalidate event flag
    lock_guard<mutex> _(handler_mutex_);
    auto _hit = handler_map_.find(so);
    if ( _hit == end(handler_map_) ) return;
    if ( eid & SL_EVENT_ACCEPT ) {
        _hit->second.on_accept = h;
    }
    if ( eid & SL_EVENT_DATA ) {
        _hit->second.on_data = h;
    }
    if ( eid & SL_EVENT_FAILED ) {
        _hit->second.on_failed = h;
    }
    if ( eid & SL_EVENT_WRITE ) {
        _hit->second.on_write = h;
    }
    if ( eid & SL_EVENT_TIMEOUT ) {
        _hit->second.on_timedout = h;
    }
}
void sl_events::append_handler( SOCKET_T so, uint32_t eid, sl_socket_event_handler h)
{
    lock_guard<mutex> _(handler_mutex_);
    sl_socket_event_handler _oldh = this->_fetch_handler(so, (SL_EVENT_ID)eid);
    auto _newh = [_oldh, h](sl_event e) {
        if ( _oldh ) _oldh(e);
        if ( h ) h(e);
    };
    this->_replace_handler(so, eid, _newh);
}
bool sl_events::has_handler(SOCKET_T so, SL_EVENT_ID eid)
{
    if ( SOCKET_NOT_VALIDATE(so) ) return false;
    if ( eid == 0 ) return false;
    if ( eid & 0xFFFFFFE0 ) return false;

    lock_guard<mutex> _(event_mutex_);
    return this->_has_handler(so, eid);
}

void sl_events::monitor(SOCKET_T so, SL_EVENT_ID eid, sl_socket_event_handler handler, uint32_t timedout)
{
    lock_guard<mutex> _(event_mutex_);

    bool _has_event = _has_handler(so, eid);
    if ( _has_event ) {
        #if DEBUG
        ldebug 
            << "socket " << so << " has already pending the handler for event " 
            << sl_event_name(eid) << ", ignore this monitoring request" 
        << lend;
        #endif
        return;
    }

    // Add the mask
    auto _ermit = event_remonitor_map_.find(so);
    if ( _ermit == end(event_remonitor_map_) ) {
        event_remonitor_map_[so] = {{{timedout, eid}}, 1};
    } else {
        if ( _ermit->second.timeout != 0 ) {
            if ( timedout == 0 ) {
                _ermit->second.timeout = 0;
            } else {
                timedout = max(_ermit->second.timeout, timedout);
                _ermit->second.timeout = timedout;
            }
        } else {
            timedout = 0;
        }
        _ermit->second.eventid |= eid;
    }
    // Update the handler
    this->update_handler(so, eid, move(handler));

    // Update the monitor status
    if ( !sl_poller::server().monitor_socket(so, true, eid, timedout) ) {
        #if DEBUG
        ldebug 
            << "failed to monitor the socket " << so << " for event " 
            << sl_event_name(eid) << ", add a FAILED event" 
        << lend;
        #endif
        events_pool_.notify_one(move(sl_event_make_failed(so)));
    }
}

void sl_events::setup(uint32_t timepiece, sl_runloop_callback cb)
{
    lock_guard<mutex> _(running_lock_);
    timepiece_ = timepiece;
    rl_callback_ = cb;
}

void sl_events::add_event(sl_event && e)
{
    //lock_guard<mutex> _(events_lock_);
    lock_guard<mutex> _(event_mutex_);
    
    auto _ermit = event_remonitor_map_.find(e.so);
    if ( _ermit == end(event_remonitor_map_) ) {
        event_remonitor_map_[e.so] = {{{30, e.event}}, 0};
    } else {
        _ermit->second.eventid |= e.event;
    }
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

    this->add_event(move(_e));
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

    this->add_event(move(_e));
}

// events.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
