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

#pragma once

#ifndef __SOCKLITE_SOCKS_5_H__
#define __SOCKLITE_SOCKS_5_H__

#include "socket.h"

enum sl_methods {
	sl_method_noauth		= 0,
	sl_method_gssapi		= 1,
	sl_method_userpwd		= 2,
	sl_method_nomethod		= 0xff
};

enum sl_socks5cmd {
	sl_socks5cmd_connect	= 1,
	sl_socks5cmd_bind		= 2,
	sl_socks5cmd_udp		= 3
};

enum sl_socks5atyp {
	sl_socks5atyp_ipv4		= 1,
	sl_socks5atyp_dname		= 3,
	sl_socks5atyp_ipv6		= 4,
};

enum sl_socks5rep {
	sl_socks5rep_successed			= 0,	// successed
	sl_socks5rep_failed				= 1,	// general SOCKS server failure
	sl_socks5rep_connectnotallow	= 2,	// connection not allowed by ruleset
	sl_socks5rep_unreachable		= 3,	// Network unreachable
	sl_socks5rep_hostunreachable	= 4,	// Host unreachable
	sl_socks5rep_refused			= 5,	// Connection refused
	sl_socks5rep_expired			= 6,	// TTL expired
	sl_socks5rep_notsupport			= 7,	// Command not supported
	sl_socks5rep_erroraddress		= 8,	// Address type not supported
};

static inline const char *sl_socks5msg(sl_socks5rep rep) {
	static const char * _gmsg[] = {
		"successed",
		"general SOCKS server failure",
		"connection not allowed by ruleset",
		"Network unreachable",
		"Host unreachable",
		"Connection refused",
		"TTL expired",
		"Command not supported",
		"Address type not supported",
		"Unknow Error Code"
	};
	if ( rep > sl_socks5rep_erroraddress ) return _gmsg[sl_socks5rep_erroraddress + 1];
	return _gmsg[rep];
};

#pragma pack(push, 1)
struct sl_socks5_package {
	uint8_t 	ver;

	// Default we only support version 5
	sl_socks5_package() : ver(5) {}
};

struct sl_socks5_handshake_request : public sl_socks5_package {
	uint8_t		nmethods;
};

struct sl_socks5_noauth_request : public sl_socks5_handshake_request {
	uint8_t 	method;

	sl_socks5_noauth_request(): 
		sl_socks5_handshake_request(), method(sl_method_noauth) {
		nmethods = 1;
		}
};

struct sl_socks5_gssapi_request : public sl_socks5_handshake_request {
	uint8_t		method;

	sl_socks5_gssapi_request():
		sl_socks5_handshake_request(), method(sl_method_gssapi) {
		nmethods = 1;
		}
};

struct sl_socks5_userpwd_request : public sl_socks5_handshake_request {
	uint8_t		method;

	sl_socks5_userpwd_request():
		sl_socks5_handshake_request(), method(sl_method_userpwd) {
		nmethods = 1;
		}
};

struct sl_socks5_handshake_response : public sl_socks5_package {
	uint8_t		method;

	sl_socks5_handshake_response() {}
	sl_socks5_handshake_response(sl_methods m) : method(m) { }
};

struct sl_socks5_connect_request : public sl_socks5_package {
	uint8_t		cmd;
	uint8_t		rsv;	// reserved
	uint8_t		atyp;	// address type

	sl_socks5_connect_request():
		sl_socks5_package(), cmd(sl_socks5cmd_connect), rsv(0) {}
};

struct sl_socks5_ipv4_request : public sl_socks5_connect_request {
	uint32_t	ip;
	uint16_t	port;

	sl_socks5_ipv4_request(uint32_t ipv4, uint16_t p):
		sl_socks5_connect_request(), ip(ipv4), port(p) {
		atyp = sl_socks5atyp_ipv4;
		}
};

struct sl_socks5_connect_response : public sl_socks5_package {
	uint8_t		rep;
	uint8_t		rsv;
	uint8_t		atyp;
};

struct sl_socks5_ipv4_response : public sl_socks5_connect_response {
	uint32_t	ip;
	uint16_t	port;

	sl_socks5_ipv4_response() {}
	sl_socks5_ipv4_response(uint32_t addr, uint16_t p):
		sl_socks5_connect_response(), 
		ip(addr),
		port(p)
	{
	rep = sl_socks5rep_successed;
	atyp = sl_socks5atyp_ipv4;
	}
};
#pragma pack(pop)

#endif // socklite.socks5.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
