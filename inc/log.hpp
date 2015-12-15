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

#ifndef __CPP_UTILITY_LOG_H__
#define __CPP_UTILITY_LOG_H__

#include <iostream>
#include <ctime>
#include <chrono>
#include <fstream>
#include <syslog.h>
#include <mutex>
#include <assert.h>
#include <sstream>
#include <cstdarg>

#include "thread.hpp"

using namespace std;
using namespace std::chrono;

namespace cpputility {

    typedef enum {
    	log_emergancy		= LOG_EMERG,	// 0
    	log_alert			= LOG_ALERT,	// 1
    	log_critical		= LOG_CRIT,		// 2
    	log_error			= LOG_ERR,		// 3
    	log_warning			= LOG_WARNING,	// 4
    	log_notice			= LOG_NOTICE,	// 5
    	log_info			= LOG_INFO,		// 6
    	log_debug			= LOG_DEBUG		// 7
    } cp_log_level;

    typedef pair<cp_log_level, string>  log_item_t;

    static inline const char *cp_log_lv_to_string(cp_log_level lv) {
        static const char *_lvname[8] = {
            "emergancy",
            "critical",
            "alert",
            "error",
            "warning",
            "notice",
            "info",
            "debug"
        };
        return _lvname[lv];
    }

    static inline void cp_log_get_time(string & logline) {
        auto _now = system_clock::now();
        auto _time_t = system_clock::to_time_t(_now);
        auto _time_point = _now.time_since_epoch();
        _time_point -= duration_cast<seconds>(_time_point); 
        auto _tm = localtime(&_time_t);
        size_t _current_sz = logline.size();
        logline.resize(_current_sz + 25);   // '[yyyy-MM-dd hh:mm:ss.zzz]'

        sprintf(&logline[_current_sz], "[%04d-%02d-%02d %02d:%02d:%02d.%03d]", 
                _tm->tm_year + 1900, _tm->tm_mon + 1, _tm->tm_mday,
                _tm->tm_hour, _tm->tm_min, _tm->tm_sec,
                static_cast<unsigned>(_time_point / milliseconds(1)));
    }


    class log_arguments {
    private:
        cp_log_level            log_lv;
        bool                    log_to_sys;
        string                  log_path;
        FILE*                   log_fp;
        thread*                 log_thread;
        event_pool<log_item_t>  log_pool;
        bool                    log_status;
        mutex                   log_mutex;

        log_arguments() : 
            log_lv(log_info), 
            log_to_sys(false),
            log_fp(NULL),
            log_thread(NULL),
            log_status(false)
        {
            log_thread = new thread([&](){
                thread_agent _ta;

                log_item_t _logitem;
                while ( this_thread_is_running() ) {
                    if ( !this->log_pool.wait_for(milliseconds(10), [&](log_item_t&& line){
                                _logitem.swap(line);
                            }) ) continue;
                    lock_guard<mutex> _(log_mutex);
                    if ( log_status == false ) continue;

                    if ( this->log_fp != NULL ) {
                        fprintf(this->log_fp, "%s\n", _logitem.second.c_str());
                    } else if ( this->log_to_sys ) {
                        // Syslog
                        syslog(_logitem.first, "%s\n", _logitem.second.c_str());
                    } else if ( this->log_path.size() > 0 ) {
                        // To file
                        do {
                            this->log_fp = fopen(this->log_path.c_str(), "a+");
                        } while ( this->log_fp == NULL && this_thread_is_running() );
                        fprintf(this->log_fp, "%s\n", _logitem.second.c_str());
                        fclose(this->log_fp);
                        this->log_fp = NULL;
                    }
                }
            });
        }

    public:
        ~log_arguments()
        {
            // Check and stop the log system
            //cp_log_stop();
            if ( log_thread != NULL ) {
                if ( log_thread->joinable() ) {
                    log_thread->join();
                }
                delete log_thread;
                log_thread = NULL;
            }

            if ( log_to_sys ) {
                closelog();
            }
            log_to_sys = false;
        }

        void start(const string &logpath, cp_log_level lv) {
            log_fp = NULL;
            log_path = logpath;
            log_lv = lv;
            log_to_sys = false;

            lock_guard<mutex> _(log_mutex);
            log_status = true;
        }

        void start(FILE *fp, cp_log_level lv) {
            log_fp = fp;
            log_lv = lv;
            log_path = "";
            log_to_sys = false;

            lock_guard<mutex> _(log_mutex);
            log_status = true;
        }

        void start(cp_log_level lv, const string& logname) {
            setlogmask(LOG_UPTO(lv));
            openlog(logname.c_str(), LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);

            log_to_sys = true;
            log_lv = lv;
            log_fp = NULL;
            log_path = "";

            lock_guard<mutex> _(log_mutex);
            log_status = true;
        }

        void log(cp_log_level lv, const char *format, ...) {
            // Check log level
            if ( lv > log_lv ) return;
            if ( log_thread == NULL ) return;

            string _logline;
            if ( log_to_sys == false ) {
                cp_log_get_time(_logline);
            }
            _logline += "[";
            _logline += cp_log_lv_to_string(lv);
            _logline += "] ";

            va_list _va;
            va_start(_va, format);
            size_t _fmtsize = vsnprintf(NULL, 0, format, _va);
            va_end(_va);
            size_t _crtsize = _logline.size();
            _logline.resize(_crtsize + _fmtsize + 1);
            va_start(_va, format);
            vsnprintf(&_logline[_crtsize], _fmtsize + 1, format, _va);
            va_end(_va);

            log_pool.notify_one(make_pair(lv, _logline));
        }

        static log_arguments& instance() {
            static log_arguments _arg;
            return _arg;
        }
    };

    // The end line struct is just a place hold
    class cp_logger_specifical_character
    {
    protected:
        char                _sc;
        friend ostream & operator << (ostream &os, const cp_logger_specifical_character & sc );
    public:
        // True : the character should be add to the log line
        // False: this is the end of log line.
        operator bool () const {
            return (_sc != '\0');
        }

        cp_logger_specifical_character( char c = '\n' ) : _sc(c) { }
        cp_logger_specifical_character( bool eol ) : _sc('\0') {
            if ( eol == false ) _sc = '\n';
        }

        // Get the char as a function object
        char operator() (void) const {
            return _sc;
        }
    };

    // Output the specifical chararcter
    inline ostream & operator << (ostream &os, const cp_logger_specifical_character & sc ) {
        if ( sc ) {
            os << sc._sc;
        }
        return os;
    }

    // Stream logger
    class cp_logger
    {
    protected:
        cp_log_level            lv_;
        recursive_mutex         mutex_;
        ostringstream           oss_;
        uint32_t                lock_level_;

        template< class T >
        friend cp_logger & operator << (cp_logger & logger, const T & item);
    public:
        cp_logger(cp_log_level lv) : lv_(lv), lock_level_(0) { }

    public:
        // Log object
        static cp_logger& emergancy() {
            static cp_logger _obj(log_emergancy);
            return _obj;
        }
        static cp_logger& alert() {
            static cp_logger _obj(log_alert);
            return _obj;
        }
        static cp_logger& critical() {
            static cp_logger _obj(log_critical);
            return _obj;
        }
        static cp_logger& error() {
            static cp_logger _obj(log_error);
            return _obj;
        }
        static cp_logger& warning() {
            static cp_logger _obj(log_warning);
            return _obj;
        }
        static cp_logger& notice() {
            static cp_logger _obj(log_notice);
            return _obj;
        }
        static cp_logger& info() {
            static cp_logger _obj(log_info);
            return _obj;
        }
        static cp_logger& debug() {
            static cp_logger _obj(log_debug);
            return _obj;
        }

        // End of current log line
        static cp_logger_specifical_character& endl() {
            static cp_logger_specifical_character _obj('\0');
            return _obj;
        }
        static cp_logger_specifical_character& newline() {
            static cp_logger_specifical_character _obj('\n');
            return _obj;
        }
        static cp_logger_specifical_character& backspace() {
            static cp_logger_specifical_character _obj('\b');
            return _obj;
        }
        static cp_logger_specifical_character& tab() {
            static cp_logger_specifical_character _obj('\t');
            return _obj;
        }

    public:
        static void start(const string &logpath, cp_log_level lv) {
            log_arguments::instance().start(logpath, lv);
        }
        static void start(FILE *fp, cp_log_level lv) {
            log_arguments::instance().start(fp, lv);
        }
        static void start(cp_log_level lv, const string& logname) {
            log_arguments::instance().start(lv, logname);
        }
    };

    template < class T > 
    inline cp_logger & operator << ( cp_logger& logger, const T & item ) {
        // Lock the log object
        logger.mutex_.lock();
        logger.lock_level_ += 1;

        // Append the buffer
        logger.oss_ << item;

        if ( logger.lock_level_ > 1 ) {
            logger.lock_level_ -=1;
            logger.mutex_.unlock();
        }
        return logger;
    }

    template < >
    inline cp_logger & operator << <cp_logger_specifical_character> ( 
        cp_logger& logger, const cp_logger_specifical_character & item ) {
        if ( item ) {
            return logger << item();
        }
        // Write the log
        log_arguments::instance().log(logger.lv_, "%s", logger.oss_.str().c_str());
        logger.oss_.str("");
        logger.oss_.clear();

        logger.lock_level_ -= 1;
        assert(logger.lock_level_ == 0);
        logger.mutex_.unlock();
        return logger;
    }

    #define lend                cp_logger::endl()
    #define lnewl               cp_logger::newline()
    #define lbackspace          cp_logger::backspace()
    #define ltab                cp_logger::tab()
    #define lemergancy          cp_logger::emergancy()
    #define lalert              cp_logger::alert()
    #define lcritical           cp_logger::critical()
    #define lerror              cp_logger::error()
    #define lwarning            cp_logger::warning()
    #define lnotice             cp_logger::notice()
    #define linfo               cp_logger::info()
    #define ldebug              cp_logger::debug()
}

#endif // cpputility.log.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
