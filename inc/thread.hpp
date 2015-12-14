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

#pragma once

#ifndef __CPP_UTILITY_THREAD_H__
#define __CPP_UTILITY_THREAD_H__
    
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <csignal>
#include <map>
#include <iostream>
#include <unistd.h>

using namespace std;

namespace cpputility {

    // Internal Mutex
    inline mutex& __g_thread_mutex() {
        static mutex _m;
        return _m;
    }

    inline void __h_thread_signal( int sig ) {
        if ( SIGTERM == sig || SIGINT == sig || SIGQUIT == sig ) {
            __g_thread_mutex().unlock();
        }
    }

    // Hang up current process, and wait for exit signal
    inline void set_signal_handler() {
    #ifdef __APPLE__
        signal(SIGINT, __h_thread_signal);
        signal(SIGINT, __h_thread_signal);
        signal(SIGQUIT, __h_thread_signal);
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
        signal(SIGTERM, __h_thread_signal);
        signal(SIGINT, __h_thread_signal);
        signal(SIGQUIT, __h_thread_signal);
    #endif
        __g_thread_mutex().lock();
    }

    // Wait until we receive exit signal, this function will block
    // current thread.
    inline void wait_for_exit_signal() {
        __g_thread_mutex().lock();
        __g_thread_mutex().unlock();
    }

    class thread_info
    {
        typedef map< thread::id, pair< shared_ptr<mutex>, shared_ptr<bool> > > info_map_t;
    private:
        mutex               info_mutex_;
        info_map_t          info_map_;
    public:

        static thread_info& instance() {
            static thread_info _ti;
            return _ti;
        }

        // Register current thread
        void register_this_thread() {
            lock_guard<mutex> _(info_mutex_);
            info_map_[this_thread::get_id()] = make_pair(make_shared<mutex>(), make_shared<bool>(true));
        }
        // Unregister current thread and release the resource
        // then can join the thread.
        void unregister_this_thread() {
            lock_guard<mutex> _(info_mutex_);
            info_map_.erase(this_thread::get_id());
        }
        // Stop all thread registered
        void join_all_threads() {
            do {
                lock_guard<mutex> _(info_mutex_);
                for ( auto &_kv : info_map_ ) {
                    lock_guard<mutex>(*_kv.second.first);
                    *_kv.second.second = false;
                }
            } while( false );
            do {
                if ( true ) {
                    lock_guard<mutex> _(info_mutex_);
                    if ( info_map_.size() == 0 ) return;
                }
                usleep(1000);   // wait for 1ms
            } while ( true );
        }
        // join specified thread
        void safe_join_thread(thread::id tid) {
            lock_guard<mutex> _(info_mutex_);
            auto _it = info_map_.find(tid);
            if ( _it == end(info_map_) ) {
                return;
            }
            lock_guard<mutex>(*_it->second.first);
            *_it->second.second = false;
        }
        // Check this thread's status
        bool this_thread_is_running() {
            lock_guard<mutex> _(info_mutex_);
            auto _it = info_map_.find(this_thread::get_id());
            if ( _it == end(info_map_) ) { return false; }
            lock_guard<mutex> __(*_it->second.first);
            return *_it->second.second;
        }
    };

    inline void register_this_thread() {
        thread_info::instance().register_this_thread();
    }

    // Unregister current thread and release the resource
    // then can join the thread.
    inline void unregister_this_thread() {
        thread_info::instance().unregister_this_thread();
    }

    // Stop all thread registered
    inline void join_all_threads() {
        thread_info::instance().join_all_threads();
    }

    // join specified thread
    inline void safe_join_thread(thread::id tid) {
        thread_info::instance().safe_join_thread(tid);
    }

    // Check this thread's status
    inline bool this_thread_is_running() {
        return thread_info::instance().this_thread_is_running();
    }

    // Thread Agent to auto register and unregister the thread to the thread info map
    class thread_agent
    {
    public:
        thread_agent() { register_this_thread(); }
        ~thread_agent() { unregister_this_thread(); }
    };

    // The global server signal agent. should be the first line in any application
    class signal_agent
    {
    public:
        typedef function<void(void)>        before_exit_t;
    protected:
        before_exit_t                       exit_callback_;
    public:
        signal_agent(before_exit_t cb) : exit_callback_(cb) { set_signal_handler(); };
        ~signal_agent() {
            wait_for_exit_signal();
            if ( exit_callback_ ) exit_callback_() ;
            join_all_threads();
        }
    };

    template < class Item > class event_pool
    {
    public:
    	typedef function<void(Item&&)>	get_event_t;
    protected:
    	mutex					mutex_;
    	condition_variable		cv_;
    	queue<Item>				pool_;

    public:

    	bool wait( get_event_t get_event ) {
    		unique_lock<mutex> _l(mutex_);
    		cv_.wait(_l, [this](){ return pool_.size() > 0; });
    		get_event(move(pool_.front()));
    		pool_.pop();
    		return true;
    	}

    	template< class Rep, class Period >
    	bool wait_for(const chrono::duration<Rep, Period>& rel_time, get_event_t get_event) {
    		unique_lock<mutex> _l(mutex_);
    		bool _result = cv_.wait_for(_l, rel_time, [this](){ return pool_.size() > 0; });
    		if ( _result == true ) {
    			get_event(move(pool_.front()));
    			pool_.pop();
    		}
    		return _result;
    	}

    	template< class Clock, class Duration >
    	bool wait_until(const chrono::time_point<Clock, Duration>& timeout_time, get_event_t get_event) {
    		unique_lock<mutex> _l(mutex_);
    		bool _result = cv_.wait_until(_l, timeout_time, [this](){ return pool_.size() > 0; });
    		if ( _result == true ) {
    			get_event(move(pool_.front()));
    			pool_.pop();
    		}
    		return _result;
    	}

    	void notify_one(Item&& item) {
    		unique_lock<mutex> _l(mutex_);
    		pool_.emplace(item);
    		cv_.notify_one();
    	};

    	void clear() {
    		lock_guard<mutex> _l(mutex_);
    		pool_.clear();
    	}

        size_t size() {
            lock_guard<mutex> _l(mutex_);
            return pool_.size();
        }
    };
}

#endif

// tinydst.thread.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
