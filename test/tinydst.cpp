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

static bool sl_getstring(const char *buf, unsigned int len, string &s) {
	if ( len <= 1 ) return false;
	uint8_t _sz = buf[0];
	if ( len - 1 < _sz ) return false;
	s.append(buf + 1, _sz);
	return true;
}

// Accept new socks5 request
static void sl_socks5_handshake(SOCKET_T so) {
	sl_tcpsocket _wrapso(so);
	string _buffer;
	if ( _wrapso.read_data(_buffer) == false ) {
		_wrapso.close();
		return;
	}

	if ( _buffer.size() < sizeof(sl_socks5_noauth_request) ) {
		_wrapso.close();
		return;
	}

	sl_socks5_handshake_request *_req = (sl_socks5_handshake_request *)_buffer.data();
	sl_socks5_handshake_response _resp(sl_method_nomethod);
	string _respdata;
	if ( _req->nmethods != 1 ) {
		_respdata.append((char *)&_resp, sizeof(_resp));
		_wrapso.write_data(_respdata);
		_wrapso.close();
		return;
	}

	uint8_t _method = (_buffer.data() + sizeof(sl_socks5_handshake_request))[0];
	bool _should_close = false;
	if ( _method == sl_method_noauth ) {
		// Noauth
		_resp.method = sl_method_noauth;
	} else if ( _method == sl_method_userpwd ) {
		// user name password
		// Check the user name & password
		size_t _s = _buffer.size() - sizeof(sl_socks5_userpwd_request);
		const char *_data = _buffer.data() + sizeof(sl_socks5_userpwd_request);
		string _username, _password;
		if ( !sl_getstring(_data, _s, _username) ) {
			_should_close = true;
		} else {
			_data += (_username.size() + 1);
			_s -= (_username.size() + 1);
			if ( !sl_getstring(_data, _s, _password) ) {
				_should_close = true;
			}
		}
		if ( !_should_close ) {
			_resp.method = sl_method_userpwd;
			cout << "New connect, auth by username/password: " << _username << "/" << _password << endl;
		}
	} else {
		// not support yet
		_should_close = true;
	}
	_respdata.append((char *)&_resp, sizeof(_resp));
	_wrapso.write_data(_respdata);
	if ( _should_close ) {
		_wrapso.close();
		return;
	}

	// Wait for connect command
	if ( !_wrapso.read_data(_buffer) ) {
		_wrapso.close();
		return;
	}
	if ( _buffer.size() < sizeof(sl_socks5_connect_request) ) {
		_wrapso.close();
		return;
	}
	sl_socks5_connect_request *_connect_req = (sl_socks5_connect_request *)_buffer.data();
	sl_socks5_ipv4_response _connect_resp(0, 0);
	_respdata = "";
	if ( _connect_req->cmd != sl_socks5cmd_connect ) {
		_connect_resp.rep = sl_socks5rep_notsupport;
		_should_close = true;
	}
	// Get connection info
	string _addr;
	uint16_t _port;
	if ( !_should_close ) {
		if ( _connect_req->atyp == sl_socks5atyp_ipv4 ) {
			// Get ip address 
			uint32_t _ip = *(uint32_t *)(_buffer.data() + sizeof(sl_socks5_connect_request));
			_ip = ntohl(_ip);
			network_int_to_ipaddress(_ip, _addr);
			_port = *(uint16_t *)(_buffer.data() + sizeof(sl_socks5_connect_request) + sizeof(uint32_t));
		} else if ( _connect_req->atyp == sl_socks5atyp_dname ) {
			// Get domain
			const char *_d = _buffer.data() + sizeof(sl_socks5_connect_request);
			size_t _ds = _buffer.size() - sizeof(sl_socks5_connect_request);
			if ( ! sl_getstring(_d, _ds, _addr) ) {
				_connect_resp.rep = sl_socks5rep_erroraddress;
				_should_close = true;
			} else {
				_port = *(uint16_t *)(_d + 1 + _addr.size());
			}
		} else {
			_connect_resp.rep = sl_socks5rep_erroraddress;
			_should_close = true;
		}
	}
	if ( !_should_close ) {
		// Connect to dst 
		sl_tcpsocket _wrapdst(true);
		if ( _wrapdst.connect(_addr, ntohs(_port)) == false ) {
			_connect_resp.rep = sl_socks5rep_unreachable;
			_should_close = true;
		} else {
			// Bind relay map
			sl_bind_relay(_wrapso.m_socket, _wrapdst.m_socket);
			_connect_resp.ip = network_domain_to_inaddr(_addr.c_str());
			_connect_resp.port = _port;

			// Add both socket to monitor
			sl_poller::server().monitor_socket(_wrapso.m_socket);
			sl_poller::server().monitor_socket(_wrapdst.m_socket);
		}
	}
	_respdata.append((char *)&_connect_resp, sizeof(_connect_resp));
	_wrapso.write_data(_respdata);
	if ( _should_close ) _wrapso.close();
}

void loop_worker(mutex *m, bool *st) {
	// event list
	vector<sl_event> _event_list;
	while ( _tstatus(m, st) ) {
		_event_list.clear();
		sl_poller::server().fetch_events(_event_list);

		for ( auto & _e : _event_list ) {
			if ( _e.event == SL_EVENT_FAILED ) {
				if ( _e.so != _e.source ) {
					sl_unbind_relay(_e.so);
				}
			} else if ( _e.event == SL_EVENT_ACCEPT ) {
				thread _handshake(sl_socks5_handshake, _e.so);
				_handshake.join();
			} else {
				string _buf;
				sl_tcpsocket _wso(_e.so);
				if ( _wso.read_data(_buf) ) {
					sl_tcpsocket _wrso(sl_somap()[_e.so]);
					_wrso.write_data(_buf);
				}
			}
		}
	}
}

int main( int argc, char * argv[] ) {
	// Listen on sock5 proxy
	if ( !sl_listen(sl_socks5svr(), 4001) ) {
		cerr << "Failed to listen on 4001" << endl;
		return 1;
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
