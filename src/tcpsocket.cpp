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

#include "tcpsocket.h"
#include <memory>
#if SL_TARGET_LINUX
#include <limits.h>
#include <linux/netfilter_ipv4.h>
#endif

#include "socks5.h"

sl_tcpsocket::sl_tcpsocket(bool iswrapper)
: m_iswrapper(iswrapper),
	m_is_connected_to_proxy(false), 
	m_socket(INVALIDATE_SOCKET)
{
    // Nothing
}
sl_tcpsocket::sl_tcpsocket(SOCKET_T so, bool iswrapper)
	: m_iswrapper(iswrapper),
	m_is_connected_to_proxy(false),
	m_socket(so)
{
	// Nothing
}
sl_tcpsocket::~sl_tcpsocket()
{
	if ( m_iswrapper == false ) {
   		this->close();
	}
}

// Connect to peer
bool sl_tcpsocket::_internal_connect( const string &ipaddr, u_int32_t port )
{
    if ( ipaddr.size() == 0 || port == 0 || port >= 65535 ) return false;
    
    const char *_addr = ipaddr.c_str();
    u_int32_t _timeout = 1000;

    // Try to nslookup the host
    unsigned int _in_addr = network_domain_to_inaddr( _addr );
    if ( _in_addr == (unsigned int)(-1) ) {
        return false;
    }

    // Create Socket Handle
    m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    // SOCKET_T hSo = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if ( SOCKET_NOT_VALIDATE(m_socket) ) {
        return false;
    }
    
    // Set With TCP_NODELAY
    int flag = 1;
    if( setsockopt( m_socket, IPPROTO_TCP, 
        TCP_NODELAY, (const char *)&flag, sizeof(int) ) == -1 )
    {
        SL_NETWORK_CLOSESOCK( m_socket );
        return false;
    }

    struct sockaddr_in _sock_addr; 
    memset( &_sock_addr, 0, sizeof(_sock_addr) );
    _sock_addr.sin_addr.s_addr = _in_addr;
    _sock_addr.sin_family = AF_INET;
    _sock_addr.sin_port = htons(port);

    // Async Socket Connecting
    unsigned long _u = 1;
    SL_NETWORK_IOCTL_CALL(m_socket, FIONBIO, &_u);

    // Connect
    if ( ::connect( m_socket, (struct sockaddr *)&_sock_addr, 
            sizeof(_sock_addr) ) == -1 )
    {
        struct timeval _tm = { _timeout / 1000, 
            static_cast<int>((_timeout % 1000) * 1000) };
        fd_set _fs;
        int _error = 0, len = sizeof(_error);
        FD_ZERO( &_fs );
        FD_SET( m_socket, &_fs );

        // Wait until timeout
        do {
            _error = ::select( m_socket + 1, NULL, &_fs, NULL, &_tm );
        } while( _error < 0 && errno == EINTR );

        // _error > 0 means writable, then check if has any error.
        if ( _error > 0 ) {
            getsockopt( m_socket, SOL_SOCKET, SO_ERROR, 
                (char *)&_error, (socklen_t *)&len);
            if ( _error != 0 ) {
                // Failed to connect
                SL_NETWORK_CLOSESOCK( m_socket );
                return false;
            }
        } else {
            // Failed to connect
            SL_NETWORK_CLOSESOCK( m_socket );
            return false;
        }
    }
    // Reset Socket Statue
    _u = 0;
    SL_NETWORK_IOCTL_CALL(m_socket, FIONBIO, &_u);
    this->set_reusable();
    return true;
}

bool sl_tcpsocket::setup_proxy( const string &socks5_addr, u_int32_t socks5_port )
{
    // Build a connection to the proxy server
    if ( ! this->_internal_connect( socks5_addr, socks5_port ) ) {
		fprintf(stderr, "failed to connect to the socks5 proxy server\n");
		return false;
	}
	
	sl_socks5_noauth_request _req;
    // Exchange version info
    if (write(m_socket, (char *)&_req, sizeof(_req)) < 0) {
        this->close();
        return false;
    }

	sl_socks5_handshake_response _resp;
    if (read(m_socket, (char *)&_resp, sizeof(_resp)) == -1) {
        this->close();
        return false;
    }

	// This api is for no-auth proxy
	if ( _resp.ver != 0x05 && _resp.method != sl_method_noauth ) {
		fprintf(stderr, "unsupported authentication method\n");
        this->close();
        return false;
    }

    // Now we has connected to the proxy server.
    m_is_connected_to_proxy = true;
    return true;
}

bool sl_tcpsocket::setup_proxy(
		const string &socks5_addr, u_int32_t socks5_port,
		const string &username, const string &password) 
{
	// Connect to socks 5 proxy
	if ( ! this->_internal_connect( socks5_addr, socks5_port ) ) {
		fprintf(stderr, "failed to connect to the socks5 proxy server\n");
		return false;
	}

	sl_socks5_userpwd_request _req;
	char *_buf = (char *)malloc(sizeof(_req) + username.size() + password.size() + 2);
	memcpy(_buf, (char *)&_req, sizeof(_req));
	int _index = sizeof(_req);
	_buf[_index] = (uint8_t)username.size();
	_index += 1;
	memcpy(_buf + _index, username.data(), username.size());
	_index += username.size();
	_buf[_index] = (uint8_t)password.size();
	_index += 1;
	memcpy(_buf + _index, password.data(), password.size());
	_index += password.size();

	// Send handshake package
	if (write(m_socket, _buf, _index) < 0) {
		this->close();
		return false;
	}
	free(_buf);

	sl_socks5_handshake_response _resp;
	if (read(m_socket, (char *)&_resp, sizeof(_resp)) == -1 ) {
		this->close();
		return false;
	}

	// Check if server support username/password
	if ( _resp.ver != 0x05 && _resp.method != sl_method_userpwd ) {
		fprintf(stderr, "unspported username/password authentication method\n");
		this->close();
		return false;
	}

	// Now we has connected to the proxy server.
	m_is_connected_to_proxy = true;
	return true;
}

bool sl_tcpsocket::connect( const string &ipaddr, u_int32_t port )
{
    if ( m_is_connected_to_proxy == false ) {
        return this->_internal_connect( ipaddr, port );
    } else {
        // Establish a connection through the proxy server.
        u_int8_t _buffer[256] = {0};
        // Socks info
        u_int16_t _host_port = htons((u_int16_t)port); // the port must be uint16

        /* Assemble the request packet */
		sl_socks5_connect_request _req;
		_req.atyp = sl_socks5atyp_dname;
		memcpy(_buffer, (char *)&_req, sizeof(_req));

		unsigned int _pos = sizeof(_req);
		_buffer[_pos] = (uint8_t)ipaddr.size();
		_pos += 1;
		memcpy(_buffer + _pos, ipaddr.data(), ipaddr.size());
		_pos += ipaddr.size();
		memcpy(_buffer + _pos, &_host_port, sizeof(_host_port));
		_pos += sizeof(_host_port);
		
        if (write(m_socket, _buffer, _pos) == -1) {
            return false;
        }

        /*
         * The maximum size of the protocol message we are waiting for is 10
         * bytes -- VER[1], REP[1], RSV[1], ATYP[1], BND.ADDR[4] and
         * BND.PORT[2]; see RFC 1928, section "6. Replies" for more details.
         * Everything else is already a part of the data we are supposed to
         * deliver to the requester. We know that BND.ADDR is exactly 4 bytes
         * since as you can see below, we accept only ATYP == 1 which specifies
         * that the IPv4 address is in a binary format.
         */
		sl_socks5_ipv4_response _resp;
        if (read(m_socket, (char *)&_resp, sizeof(_resp)) == -1) {
            return false;
        }

        /* Check the server's version. */
		if ( _resp.ver != 0x05 ) {
            (void)fprintf(stderr, "Unsupported SOCKS version: %x\n", _resp.ver);
            return false;
        }
        if (_resp.rep != sl_socks5rep_successed) {
			fprintf(stderr, "%s\n", sl_socks5msg((sl_socks5rep)_resp.rep));
			return false;
		}

        /* Check ATYP */
		if ( _resp.atyp != sl_socks5atyp_ipv4 ) {
            fprintf(stderr, "ssh-socks5-proxy: Address type not supported: %u\n", _resp.atyp);
            return false;
        }
        return true;
    }
}
// Listen on specified port and address, default is 0.0.0.0
bool sl_tcpsocket::listen( u_int32_t port, u_int32_t ipaddr )
{
    struct sockaddr_in _sock_addr;
    m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return false;

	// Make the socket reusable
	this->set_reusable(true);

    memset((char *)&_sock_addr, 0, sizeof(_sock_addr));
    _sock_addr.sin_family = AF_INET;
    _sock_addr.sin_port = htons(port);
    _sock_addr.sin_addr.s_addr = htonl(ipaddr);

    if ( ::bind(m_socket, (struct sockaddr *)&_sock_addr, sizeof(_sock_addr)) == -1 ) {
        SL_NETWORK_CLOSESOCK( m_socket );
        return false;
    }
    if ( -1 == ::listen(m_socket, 100) ) {
        SL_NETWORK_CLOSESOCK( m_socket );
        return false;
    }
    return true;
}
// Close the connection
void sl_tcpsocket::close()
{
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return;
    SL_NETWORK_CLOSESOCK(m_socket);
    m_socket = INVALIDATE_SOCKET;
}

// Try to get the original destination
bool sl_tcpsocket::get_original_dest( string &address, u_int32_t &port )
{
#if SL_TARGET_LINUX
    struct sockaddr_in _dest_addr;
    socklen_t _socklen = sizeof(_dest_addr);
    int _error = getsockopt( m_socket, SOL_IP, SO_ORIGINAL_DST, &_dest_addr, &_socklen );
    if ( _error ) return false;
    u_int32_t _ipaddr = _dest_addr.sin_addr.s_addr;
    port = ntohs(_dest_addr.sin_port);
    network_int_to_ipaddress( _ipaddr, address );
    return true;
#else
    return false;
#endif
}

// Set current socket reusable or not
bool sl_tcpsocket::set_reusable( bool reusable )
{
    if ( m_socket == INVALIDATE_SOCKET ) return false;
    int _reused = reusable ? 1 : 0;
    return setsockopt( m_socket, SOL_SOCKET, SO_REUSEADDR,
        (const char *)&_reused, sizeof(int) ) != -1;
}

bool sl_tcpsocket::set_keepalive( bool keepalive )
{
    if ( m_socket == INVALIDATE_SOCKET ) return false;
    int _keepalive = keepalive ? 1 : 0;
    return setsockopt( m_socket, SOL_SOCKET, SO_KEEPALIVE, 
        (const char *)&_keepalive, sizeof(int) );
}

bool sl_tcpsocket::set_nonblocking(bool nonblocking) 
{
	if ( m_socket == INVALIDATE_SOCKET ) return false;
	unsigned long _u = (nonblocking ? 1 : 0);
	return SL_NETWORK_IOCTL_CALL(m_socket, FIONBIO, &_u) >= 0;
}

bool sl_tcpsocket::set_socketbufsize( unsigned int rmem, unsigned int wmem )
{
	if ( m_socket == INVALIDATE_SOCKET ) return false;
	if ( rmem != 0 ) {
		setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, 
				(char *)&rmem, sizeof(rmem));
	}
	if ( wmem != 0 ) {
		setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF,
				(char *)&wmem, sizeof(wmem));
	}
	return true;
}
// Read data from the socket until timeout or get any data.
SO_READ_STATUE sl_tcpsocket::read_data( string &buffer, u_int32_t timeout)
{
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return SO_READ_CLOSE;

	buffer.resize(0);
    struct timeval _tv = { (long)timeout / 1000, 
        static_cast<int>(((long)timeout % 1000) * 1000) };

    fd_set recvFs;
    FD_ZERO( &recvFs );
    FD_SET( m_socket, &recvFs );

    // Buffer
	SO_READ_STATUE _st = SO_READ_WAITING;

    // Wait for the incoming
    int _retCode = 0;
   	do {
    	_retCode = ::select( m_socket + 1, &recvFs, NULL, NULL, &_tv );
    } while ( _retCode < 0 && errno == EINTR );

    if ( _retCode < 0 ) // Error
        return (SO_READ_STATUE)(_st | SO_READ_CLOSE);
    if ( _retCode == 0 )// TimeOut
        return (SO_READ_STATUE)(_st | SO_READ_TIMEOUT);

	unsigned int _rmem = 0;
	socklen_t _optlen = sizeof(_rmem);
	getsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, &_rmem, &_optlen);
	buffer.resize(_rmem);

    // Get data from the socket cache
    _retCode = ::recv( m_socket, &buffer[0], _rmem, 0 );
    // Error happen when read data, means the socket has become invalidate
	// Or receive EOF, which should close the socket
    if ( _retCode <= 0 ) {
		buffer.resize(0);
		return (SO_READ_STATUE)(_st | SO_READ_CLOSE);
	}
	buffer.resize(_retCode);
	_st = SO_READ_DONE;
    return _st;
}

SO_READ_STATUE sl_tcpsocket::recv( string &buffer, unsigned int max_buffer_len ) {
	if ( SOCKET_NOT_VALIDATE(m_socket) ) return SO_READ_CLOSE;
	
	// Socket must be nonblocking
	buffer.clear();
	buffer.resize(max_buffer_len);
	do {
		int _retCode = ::recv(m_socket, &buffer[0], max_buffer_len, 0 );
		if ( _retCode < 0 ) {
			int _error = 0, _len = sizeof(int);
			getsockopt( m_socket, SOL_SOCKET, SO_ERROR,
					(char *)&_error, (socklen_t *)&_len);
			if ( _error == EINTR ) continue;	// signal 7, retry
			// Other error
			buffer.resize(0);
			return SO_READ_CLOSE;
		} else if ( _retCode == 0 ) {
			// Peer Close
			buffer.resize(0);
			return SO_READ_CLOSE;
		} else {
			buffer.resize(_retCode);
			return SO_READ_DONE;
		}
	} while ( true );
	return SO_READ_DONE;
}

// Write data to peer.
bool sl_tcpsocket::write_data( const string &data )
{
    if ( data.size() == 0 ) return false;
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return false;

    int _lastSent = 0;

    unsigned int _length = data.size();
    const char *_data = data.c_str();

    while ( _length > 0 )
    {
        _lastSent = ::send( m_socket, _data, 
           	_length, 0 | SL_NETWORK_NOSIGNAL );
        if( _lastSent <= 0 ) {
            // Failed to send
            return false;
        }
		_data += _lastSent;
		_length -= _lastSent;
    }
    return true;
}

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
