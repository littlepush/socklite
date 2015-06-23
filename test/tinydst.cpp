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
	//cout << "Receive Handshake" << endl;
	//sl_printhex(_buffer.data(), _buffer.size());

	if ( _buffer.size() < sizeof(sl_socks5_noauth_request) ) {
		_wrapso.close();
		return;
	}

	sl_socks5_handshake_request *_req = (sl_socks5_handshake_request *)_buffer.data();
	sl_socks5_handshake_response _resp(sl_method_nomethod);
	string _respdata;
	const char *_methods = (_buffer.data() + sizeof(sl_socks5_handshake_request));
	bool _should_close = true;
	for ( uint8_t i = 0; i < _req->nmethods; ++i ) {
		if ( _methods[i] == sl_method_noauth ) {
			//cout << "Ask for noauth, we support" << endl;
			_resp.method = sl_method_noauth;
			_should_close = false;
			break;
		}
	}
	_respdata.append((char *)&_resp, sizeof(_resp));
	//cout << "Response: " << endl;
	//sl_printhex(_respdata.data(), _respdata.size());
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
	//cout << "Receive command: " << endl;
	//sl_printhex(_buffer.data(), _buffer.size());
	if ( _buffer.size() < sizeof(sl_socks5_connect_request) ) {
		_wrapso.close();
		return;
	}
	sl_socks5_connect_request *_connect_req = (sl_socks5_connect_request *)_buffer.data();
	sl_socks5_ipv4_response _connect_resp(0, 0);
	_respdata = "";
	if ( _connect_req->cmd != sl_socks5cmd_connect ) {
		//cout << "Command is not connect" << endl;
		_connect_resp.rep = sl_socks5rep_notsupport;
		_should_close = true;
	}
	// Get connection info
	string _addr;
	uint16_t _port;
	if ( !_should_close ) {
		if ( _connect_req->atyp == sl_socks5atyp_ipv4 ) {
			// Get ip address 
			//cout << "address type is ipv4" << endl;
			uint32_t _ip = *(uint32_t *)(_buffer.data() + sizeof(sl_socks5_connect_request));
			//_ip = ntohl(_ip);
			network_int_to_ipaddress(_ip, _addr);
			_port = *(uint16_t *)(_buffer.data() + sizeof(sl_socks5_connect_request) + sizeof(uint32_t));
		} else if ( _connect_req->atyp == sl_socks5atyp_dname ) {
			// Get domain
			//cout << "address type is domain name" << endl;
			const char *_d = _buffer.data() + sizeof(sl_socks5_connect_request);
			size_t _ds = _buffer.size() - sizeof(sl_socks5_connect_request);
			if ( ! sl_getstring(_d, _ds, _addr) ) {
				_connect_resp.rep = sl_socks5rep_erroraddress;
				_should_close = true;
			} else {
				//cout << "Failed to get domain" << endl;
				_port = *(uint16_t *)(_d + 1 + _addr.size());
			}
		} else {
			//cout << "address type is not supported" << endl;
			_connect_resp.rep = sl_socks5rep_erroraddress;
			_should_close = true;
		}
	}
	if ( !_should_close ) {
		// Connect to dst 
		sl_tcpsocket _wrapdst(true);
		//cout << "Try to connect to dst: " << _addr << ":" << ntohs(_port) << endl;
		if ( _wrapdst.connect(_addr, ntohs(_port)) == false ) {
			//cout << "failed to connect to dst" << endl;
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
	//cout << "Response: " << endl;
	//sl_printhex(_respdata.data(), _respdata.size());
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
