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

#include "socket.h"
#include "poller.h"
#include "socks5.h"
#include "tcpsocket.h"
#include "udpsocket.h"
#include "log.h"
#include "thread.h"

using namespace cpputility;

int main( int argc, char * argv[] ) {
    cp_logger::start_log(stderr);
    set_signal_handler();

    sl_tcpsocket _svr_so;
    if ( !_svr_so.listen(the_port, 10000) ) {   // Wait for 10s to listen on the_port
        cp_logger::error << "Failed to listen on " << the_port << ", error: " << _svr_so.last_error_msg() << cp_log::endl;
        return 1;
    }
    //sl_poller::server().monitor_tcp_server(_svr_so.m_socket);
    _svr_so.monitor();  // Internal to call poller to monitor self

    // Bind the socket with default handler
    sl_events::bind(&_svr_so, sl_tcpsocket::default_events_handler());

    // The main run loop
    sl_events::run(SL_EVENT_RUNMODE_ASYNC, 10, &sl_event_handlers_default_callback);

    wait_for_exit_signal();
    // Safily exit all worker
    join_all_threads();
    cp_logger::stop_log();

    return 0;
}

default_tcp_example_client_handler_on_connect(sl_event e) {
    sl_tcpsocket _so(e.so);
    _so.dump(); // <-- dump the socket's info to log

    _so.async_write_data(somedata);
}

default_tcp_example_client_handler_on_read(sl_event e) {
    sl_tcpsocket _so(e.so);
    _so.dump();

    string _buffer;
    _so.recv(_buffer);
    dump_hex(_buffer);

    sl_events::unbind(&_clt_so);
    _so.close();

    if ( sl_events::pending_socket_count() == 0 ) {
        sl_events::stop_run();
    }
}

int main2(int argc, char * argv[]) {
    sl_tcpsocket _clt_so;
    sl_events::bind(&_clt_so, sl_tcpsocket::example_client_handler());
    _clt_so.async_connect(the_peer_info);

    sl_events::run(SL_EVENT_RUNMODE_SYNC);
    return 0;
}