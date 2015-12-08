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

#define SOCK_LITE_INTEGRATION_LOG

#include <iostream>
#include <ctime>
#include <chrono>
#include <fstream>
#include <syslog.h>
#include <mutex>
#include <assert.h>
#include <sstream>

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

    // Start & Stop log server
    // Log to file
    void cp_log_start(const string &logpath, cp_log_level lv);
    // Log to specified fp, like stderr, stdout
    void cp_log_start(FILE *fp, cp_log_level lv);
    // Log to syslog
    void cp_log_start(cp_log_level lv);

    // Stop the log thread
    void cp_log_stop();

    // Write the loc
    void cp_log(cp_log_level lv, const char *format, ...);

    // The end line struct is just a place hold
    class cp_logger_specifical_character
    {
    protected:
        char                _sc;
        friend ostream & operator << (ostream &os, const cp_logger_specifical_character & sc );
    public:
        // True : the character should be add to the log line
        // False: this is the end of log line.
        operator bool () const;

        cp_logger_specifical_character( char c = '\n' );
        cp_logger_specifical_character( bool eol );

        // Get the char as a function object
        char operator() (void) const;
    };
    // Output the specifical chararcter
    ostream & operator << (ostream &os, const cp_logger_specifical_character & sc );

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
        static cp_logger                            emergancy;
        static cp_logger                            alert;
        static cp_logger                            critical;
        static cp_logger                            error;
        static cp_logger                            warning;
        static cp_logger                            notice;
        static cp_logger                            info;
        static cp_logger                            debug;

        // End of current log line
        static cp_logger_specifical_character       endl;
        static cp_logger_specifical_character       newline;
        static cp_logger_specifical_character       backspace;
        static cp_logger_specifical_character       tab;

    public:
        static void start(const string &logpath, cp_log_level lv) {
            cp_log_start(logpath, lv);
        }
        static void start(FILE *fp, cp_log_level lv) {
            cp_log_start(fp, lv);
        }
        static void start(cp_log_level lv) {
            cp_log_start(lv);
        }
        static void stop() {
            cp_log_stop();
        }
    };

    template < class T > 
    cp_logger & operator << ( cp_logger& logger, const T & item ) {
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
    cp_logger & operator << <cp_logger_specifical_character> ( cp_logger& logger, const cp_logger_specifical_character & item );

    #define lend                cp_logger::endl
    #define lnewl               cp_logger::newline
    #define lbackspace          cp_logger::backspace
    #define ltab                cp_logger::tab
    #define lemergancy          cp_logger::emergancy
    #define lalert              cp_logger::alert
    #define lcritical           cp_logger::critical
    #define lerror              cp_logger::error
    #define lwarning            cp_logger::warning
    #define lnotice             cp_logger::notice
    #define linfo               cp_logger::info
    #define ldebug              cp_logger::debug
}

#endif // cpputility.log.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
