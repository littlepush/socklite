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

#include "socks5.h"

static bool sl_supported_method[3] = {false, false, false};

void sl_socks5_set_supported_method(sl_methods m) {
	if ( m > sl_method_userpwd ) return;
	sl_supported_method[m] = true;
}

string sl_socks5_get_string(const char *buffer, uint32_t length) {
	string _result = "";
	if ( length <= sizeof(uint8_t) ) return _result;
	uint8_t _string_size = buffer[0];
	if ( length < (sizeof(uint8_t) + _string_size) ) return _result;
	_result.append(buffer + 1, _string_size);
	return _result;
}

sl_methods sl_socks5_handshake_handler(const string &req_pkt, string &resp_pkt) {
	sl_socks5_handshake_request *_req = (sl_socks5_handshake_request *)req_pkt.data();
	sl_socks5_handshake_response _resp(sl_method_nomethod);

	const char *_methods = req_pkt.data() + sizeof(sl_socks5_handshake_request);
	for ( uint8_t i = 0; i < _req->nmethods; ++i ) {
		if ( _methods[i] == sl_method_noauth ) {
			if ( sl_supported_method[sl_method_noauth] ) {
				_resp.method = sl_method_noauth;
				break;
			}
		} else if ( _methods[i] == sl_method_userpwd ) {
			if ( sl_supported_method[sl_method_userpwd] ) {
				_resp.method = sl_method_userpwd;
				break;
			}
		}
	}

	string _respdata((char *)&_resp, sizeof(_resp));
	resp_pkt.swap(_respdata);
	return (sl_methods)_resp.method;
}

bool sl_socks5_auth_by_username(const string &req_pkt, string &resp_pkt, sl_auth_method auth) {
	if ( req_pkt.data()[0] != 1 ) return false;		// version error

	const char *_b = req_pkt.data() + 1;
	uint32_t _l = req_pkt.size() - 1;
	string _username = sl_socks5_get_string(_b, _l);
	if ( _username.size() == 0 ) return false;
	_b += (_username.size() + sizeof(uint8_t));
	_l -= (_username.size() + sizeof(uint8_t));
	string _password = sl_socks5_get_string(_b, _l);
	if ( _password.size() == 0 ) return false;

	uint8_t _result = (auth(_username, _password) ? 0 : 1);
	char _resp[2] = {1, (char)_result};
	string _respdata(_resp, 2);
	resp_pkt.swap(_respdata);
	return _result == 0;
}

bool sl_socks5_get_connect_info(const string &req_pkt, string &addr, uint16_t& port) {
	sl_socks5_connect_request *_req = (sl_socks5_connect_request *)req_pkt.data();
	sl_socks5_ipv4_response _resp(0, 0);

	for ( int _dummy = 0; _dummy == 0; _dummy++ ) {
		if ( _req->cmd != sl_socks5cmd_connect ) {
			_resp.rep = sl_socks5rep_notsupport;
			break;
		}
		const char *_data = req_pkt.data() + sizeof(sl_socks5_connect_request);
		if ( _req->atyp == sl_socks5atyp_ipv4 ) {
			uint32_t _ip = *(uint32_t *)_data;
			network_int_to_ipaddress(_ip, addr);
			port = *(uint16_t *)(_data + sizeof(uint32_t));
			break;
		}
		if ( _req->atyp == sl_socks5atyp_dname ) {
			uint32_t _l = req_pkt.size() - sizeof(sl_socks5_connect_request);
			addr = sl_socks5_get_string(_data, _l);
			if ( addr.size() == 0 ) {
				_resp.rep = sl_socks5rep_erroraddress;
				break;
			}
			port = *(uint16_t *)(_data + addr.size() + 1);
			break;
		}
		_resp.rep = sl_socks5rep_notsupport;
	}
	
	port = ntohs(port);
	return _resp.rep == sl_socks5rep_successed;
}

void sl_socks5_generate_failed_connect_to_peer(sl_socks5rep rep, string &resp_pkt) {
	sl_socks5_ipv4_response _resp(0, 0);
	_resp.rep = rep;
	string _respstring((char *)&_resp, sizeof(_resp));
	resp_pkt.swap(_respstring);
}
void sl_socks5_generate_did_connect_to_peer(const sl_peerinfo &peer, string &resp_pkt) {
	sl_socks5_ipv4_response _resp(peer.ipaddress, htons(peer.port_number));
	string _respstring((char *)&_resp, sizeof(_resp));
	resp_pkt.swap(_respstring);
}

// socks5.cpp

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
