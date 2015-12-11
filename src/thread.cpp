/*
    socklite -- a C++ Utility Library for Linux/Windows/iOS
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

#include "thread.h"
#include <map>
#include <iostream>

#ifdef SOCK_LITE_INTEGRATION_THREAD

namespace cpputility {
    static mutex& __g_mutex() {
        static mutex _m;
        return _m;
    }
    static mutex& __g_infom_mutex() {
        static mutex _im;
        return _im;
    }
    static void __h_signal( int sig ) {
        if ( SIGTERM == sig || SIGINT == sig || SIGQUIT == sig ) {
            __g_mutex().unlock();
        }
    }
    void set_signal_handler() {
    #ifdef __APPLE__
        signal(SIGINT, __h_signal);
        signal(SIGINT, __h_signal);
        signal(SIGQUIT, __h_signal);
    #elif ( defined WIN32 | defined _WIN32 | defined WIN64 | defined _WIN64 )
        // nothing
    #else
        sigset_t sgset, osgset;
        sigfillset(&sgset);
        sigdelset(&sgset, SIGTERM); 
        sigdelset(&sgset, SIGINT);
        sigdelset(&sgset, SIGQUIT);
        sigdelset(&sgset, 11);
        sigprocmask(SIG_SETMASK, &sgset, &osgset);
        signal(SIGTERM, __h_signal);
        signal(SIGINT, __h_signal);
        signal(SIGQUIT, __h_signal);
    #endif
        __g_mutex().lock();
    }

    void wait_for_exit_signal() {
        __g_mutex().lock();
        __g_mutex().unlock();
    }

    static map< thread::id, pair< shared_ptr<mutex>, shared_ptr<bool> > >& __g_threadinfo() {
        static map< thread::id, pair< shared_ptr<mutex>, shared_ptr<bool> > > _m;
        return _m;
    }
    void register_this_thread() {
        lock_guard<mutex> _(__g_infom_mutex());
        __g_threadinfo()[this_thread::get_id()] = make_pair(make_shared<mutex>(), make_shared<bool>(true));
    }
    void unregister_this_thread() {
        lock_guard<mutex> _(__g_infom_mutex());
        __g_threadinfo().erase(this_thread::get_id());
    }
    void join_all_threads() {
        lock_guard<mutex> _(__g_infom_mutex());
        for ( auto &_kv : __g_threadinfo() ) {
            lock_guard<mutex>(*_kv.second.first);
            *_kv.second.second = false;
        }
    }
    // join specified thread
    void safe_join_thread(thread::id tid) {
        lock_guard<mutex> _(__g_infom_mutex());
        auto _it = __g_threadinfo().find(this_thread::get_id());
        if ( _it == end(__g_threadinfo()) ) return;
        lock_guard<mutex>(*_it->second.first);
        *_it->second.second = false;
    }

    bool this_thread_is_running() {
        lock_guard<mutex> _(__g_infom_mutex());
        auto _it = __g_threadinfo().find(this_thread::get_id());
        if ( _it == end(__g_threadinfo()) ) { return false; }
        lock_guard<mutex>(*_it->second.first);
        return *_it->second.second;
    }

    thread_agent::thread_agent() { register_this_thread(); }
    thread_agent::~thread_agent() { unregister_this_thread(); }

    signal_agent::signal_agent(signal_agent::before_exit_t cb) : exit_callback_(cb) { 
        set_signal_handler();
    }
    signal_agent::~signal_agent() {
        wait_for_exit_signal();
        join_all_threads();
        if ( exit_callback_ ) exit_callback_() ;
        usleep(100000); // sleep 100ms before exit
    }
}

#endif

// tinydst.thread.cpp

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
