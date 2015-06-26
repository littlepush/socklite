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

#include <stdio.h>
#include <iostream>
#include <vector>
#include <list>
#include <map>
#include <stack>
#include <string>
#include "socket.h"
#include "poller.h"
#include "socks5.h"
#include "tcpsocket.h"
#include <thread>
#include <mutex>

using namespace std;

#define ref		shared_ptr
#define aoc		make_shared

void sl_printhex(const char *data, unsigned int length, FILE *output = stdout) {
	const static unsigned int _cPerLine = 16;
	const static unsigned int _addrSize = sizeof(intptr_t) * 2 + 2;
	const static unsigned int _bufferSize = _cPerLine * 4 + 3 + _addrSize + 2;
	unsigned int _lines = ( length / _cPerLine ) + (unsigned int)((length % _cPerLine) > 0);
	unsigned int _lastLineSize = (_lines == 1) ? length : length % _cPerLine;
	if ( _lastLineSize == 0 ) _lastLineSize = _cPerLine;
	char _bufferLine[_bufferSize];

	for ( unsigned int _l = 0; _l < _lines; ++_l ) {
		unsigned int _lineSize = (_l == _lines - 1) ? _lastLineSize : _cPerLine;
		memset( _bufferLine, 0x20, _bufferSize );	// all space
		if ( sizeof(intptr_t) == 4 ) {
			sprintf(_bufferLine, "%08x: ", (unsigned int)(intptr_t)(data + (_l * _cPerLine)));
		} else {
			sprintf(_bufferLine, "%016lx: ", (unsigned long)(intptr_t)(data + (_l * _cPerLine)));
		}

		for ( uint32_t _c = 0; _c < _lineSize; ++_c ) {
			sprintf( _bufferLine + _c * 3 + _addrSize, "%02x ",
					(unsigned char)data[_l * _cPerLine + _c]);
			_bufferLine[ (_c + 1) * 3 + _addrSize ] = ' ';
			_bufferLine[ _cPerLine * 3 + 1 + _c + _addrSize + 1 ] = 
				( (isprint((unsigned char)(data[_l * _cPerLine + _c])) ?
				   data[_l * _cPerLine + _c] : '.')
				);
		}
		_bufferLine[ _cPerLine * 3 + _addrSize ] = '\t';
		_bufferLine[ _cPerLine * 3 + _addrSize + 1] = '|';
		_bufferLine[ _bufferSize - 3 ] = '|';
		_bufferLine[ _bufferSize - 2 ] = '\0';

		fprintf(output, "%s\n", _bufferLine);
	}
}

// System Handler
static mutex& __global_mutex() {
	static mutex __gm;
	return __gm;
}

static void __handle_signal( int _sig ) {
	if ( SIGTERM == _sig || SIGINT == _sig || SIGQUIT == _sig ) {
		__global_mutex().unlock();
	}
}

static void __set_signal_handler() {
	__global_mutex().lock();

#if SL_TARGET_MAC
	signal(SIGINT, __handle_signal);
#elif SL_TARGET_LINUX
	sigset_t sgset, osgset;
	sigfillset(&sgset);
	sigdelset(&sgset, SIGTERM);
	sigdelset(&sgset, SIGINT);
	sigdelset(&sgset, SIGQUIT);
	sigdelset(&sgset, 11);
	sigprocmask(SIG_SETMASK, &sgset, &osgset);
	signal(SIGTERM, __handle_signal);
	signal(SIGINT, __handle_signal);
	signal(SIGQUIT, __handle_signal);
#endif
}

static void __wait_for_exit_signal() {
	__global_mutex().lock();
	__global_mutex().unlock();
}

// Check thread status
bool _tstatus(mutex *m, bool *st) {
	lock_guard<mutex> _l(*m);
	return *st;
}

// This is a tcp relay server, so we need a map to bind origin socket and the 
// destination socket
static map<SOCKET_T, SOCKET_T> &sl_somap() {
	static map<SOCKET_T, SOCKET_T> _m;
	return _m;
}

static void sl_bind_relay(SOCKET_T org, SOCKET_T dst) {
	sl_somap()[org] = dst;
	sl_somap()[dst] = org;
}

static void sl_unbind_relay(SOCKET_T so) {
	auto _it = sl_somap().find(so);
	if ( _it == end(sl_somap()) ) return;
	sl_somap().erase(so);
	sl_somap().erase(_it->second);
	close(so);
	close(_it->second);
}

static sl_tcpsocket &sl_socks5svr() {
	static sl_tcpsocket _svr;
	return _svr;
}
static sl_tcpsocket &sl_backdoor() {
	static sl_tcpsocket _svr;
	return _svr;
}
static map<SOCKET_T, bool> &sl_bdmap() {
	static map<SOCKET_T, bool> _m;
	return _m;
}
static void sl_backdoor_add(SOCKET_T so) {
	sl_bdmap()[so] = true;
	sl_poller::server().monitor_socket(so);
}
static void sl_backdoor_del(SOCKET_T so) {
	sl_bdmap().erase(so);
	close(so);
}
static bool sl_isbackdoor(SOCKET_T so) {
	return sl_bdmap().find(so) != sl_bdmap().end();
}

static bool sl_listen(sl_tcpsocket &svr, uint16_t port) {
	for ( int i = 0; i < 30; ++i ) {
		if ( svr.listen(port) ) {
			sl_poller::server().bind_tcp_server(svr.m_socket);
			return true;
		}
		cout << ">";
		sleep(1);
	}
	return false;
}

// Accept new socks5 request
static void sl_socks5_handshake(SOCKET_T so) {
	sl_methods _m = sl_socks5_handshake_handler(so);
	if ( _m == sl_method_nomethod ) {
		close(so); return;
	}

	if ( _m == sl_method_userpwd ) {
		bool _auth = sl_socks5_auth_by_username(so, [](const string &u, const string &p){ return true; });
		if ( _auth == false ) {
			close(so); return;
		}
	}
	string _addr;
	uint16_t _port;
	if ( !sl_socks5_get_connect_info(so, _addr, _port) ) {
		close(so); return;
	}

	sl_tcpsocket _wdst(true);
	if ( _wdst.connect(_addr, _port) == false ) {
		sl_socks5_failed_connect_to_peer(so, sl_socks5rep_unreachable);
		close(so); return;
	}
	sl_bind_relay(so, _wdst.m_socket);
	sl_poller::server().monitor_socket(so);
	sl_poller::server().monitor_socket(_wdst.m_socket);

	// Send response package
	sl_socks5_did_connect_to_peer(so, network_domain_to_inaddr(_addr.c_str()), _port);
}

void loop_worker(mutex *m, bool *st) {
	// event list
	vector<sl_event> _event_list;
	while ( _tstatus(m, st) ) {
		_event_list.clear();
		sl_poller::server().fetch_events(_event_list);

		for ( auto & _e : _event_list ) {
			if ( _e.event == SL_EVENT_FAILED ) {
				if ( sl_isbackdoor(_e.so) ) {
					sl_backdoor_del(_e.so);
				} else {
					sl_unbind_relay(_e.so);
				}
			} else if ( _e.event == SL_EVENT_ACCEPT ) {
				if ( _e.source == sl_socks5svr().m_socket ) {
					sl_socks5_handshake(_e.so);
				} else {
					// Now we get a backdoor connection
					sl_backdoor_add(_e.so);
				}
			} else {
				string _buf;
				sl_tcpsocket _wso(_e.so);
				if ( _wso.read_data(_buf) ) {
					if ( sl_isbackdoor(_e.so) ) {
						// nothing
					} else {
						sl_tcpsocket _wrso(sl_somap()[_e.so]);
						_wrso.write_data(_buf);

						// Redirect all data to backdoor
						for ( auto _it : sl_bdmap() ) {
							sl_tcpsocket _wbdso(_it.first);
							_wbdso.write_data(_buf);
						}
					}
				}
			}
		}
	}
}

int main( int argc, char * argv[] ) {

	pid_t _pid = fork();
	if ( _pid < 0 ) {
		cerr << "Failed to create child process." << endl;
		return 1;
	}
	if ( _pid > 0 ) {
		return 0;
	}
	if ( setsid() < 0 ) {
		cerr << "Failed to session leader for child process." << endl;
		return 3;
	}

	unsigned int _port = 4001;
	if ( argc >= 2 ) {
		sscanf(argv[1], "%u", &_port);
	}
	if ( _port > 65535 || _port <= 0 ) {
		_port = 4001;
	}
	// Listen on sock5 proxy
	if ( !sl_listen(sl_socks5svr(), _port) ) {
		cerr << "Failed to listen on 4001" << endl;
		return 1;
	}

	if ( !sl_listen(sl_backdoor(), _port + 1) ) {
		cerr << "Failed to listen on 4002" << endl;
		return 2;
	}

	// Hang up current process, and quit till receive Ctrl+C
	__set_signal_handler();

	// Start the main run loop
	mutex _mloop;
	bool _mstatus = true;
	thread _loop(loop_worker, &_mloop, &_mstatus);

	__wait_for_exit_signal();

	// Kill main run loop thread
	do {
		lock_guard<mutex> _l(_mloop);
		_mstatus = false;
	} while ( false );
	_loop.join();

	return 0;
}

// sock.lite.tinydst.cpp

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
