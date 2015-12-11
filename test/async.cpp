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

#include "raw.h"

void dump_iplist(const vector<sl_ip> & iplist) {
    for ( auto ip : iplist ) {
        linfo << "get A record: " << ip << lend;
    }
}

int main( int argc, char * argv[] )
{
    cp_logger::start(stderr, log_debug);
    signal_agent _sa([](void){
        linfo << "the async server receive exit signal, ready to kill all working threads." << lend;
    });

    sl_async_gethostname("www.tmall.com", dump_iplist);
    sl_async_gethostname("www.baidu.com", dump_iplist);
    sl_async_gethostname("www.dianping.com", dump_iplist);

    string _test_domain = "www.baidu.com";
    SOCKET_T _tso = sl_tcp_socket_init();
    if ( SOCKET_NOT_VALIDATE(_tso) ) {
        lerror << "failed to init a tcp socket" << lend;
    } else {
        ldebug << "before connect to " << _test_domain << lend;
        sl_tcp_socket_connect(_tso, sl_peerinfo::nan(), _test_domain, 80, [_test_domain](sl_event e) {
            if ( e.event == SL_EVENT_FAILED ) {
                lerror << "failed to connect to " << _test_domain << lend;
                sl_socket_close(e.so);
                return;
            }
            ldebug << "connected to " << _test_domain << lend;
            linfo << "we now connect to " << _test_domain << lnewl << "try to send basic http request" << lend;
            string _http_pkg = "GET / HTTP/1.1\r\n\r\n";
            sl_tcp_socket_send(e.so, _http_pkg);
            sl_tcp_socket_monitor(e.so, [=](sl_event e) {
                if ( e.event == SL_EVENT_FAILED ) {
                    lerror << "no response get from the server." << lend;
                    sl_socket_close(e.so);
                    return;
                }
                string _http_resp;
                sl_tcp_socket_read(e.so, _http_resp, 1024000);
                dump_hex(_http_resp);
                sl_socket_close(e.so);
            });
        });     
    }
    return 0;
}

// sock.lite.async.cpp

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
