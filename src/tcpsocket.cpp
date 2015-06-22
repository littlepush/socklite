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

sl_tcpsocket::sl_tcpsocket(bool iswrapper)
:/*_svrfd(NULL),*/ m_iswrapper(iswrapper),
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

    char _buffer[3], _recv_buffer[2];

    // Set up sending information to the socks proxy
    _buffer[0] = 0x05;           /* VER */
    _buffer[1] = 0x01;           /* NMETHODS */
    _buffer[2] = 0x00;           /* METHODS */

    // Exchange version info
    if (write(m_socket, &_buffer, sizeof(_buffer)) < 0) {
        this->close();
        return false;
    }

    if (read(m_socket, &_recv_buffer, sizeof(_recv_buffer)) == -1) {
        this->close();
        return false;
    }

    // Now we just support NO AUTH for the socks proxy.
    // So if the server doesn't support it, we will disconnect
    // from the server.
    if (_recv_buffer[1] != 0x00) {
        cerr << "Unsupported Authentication Method";
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
        u_int8_t _buffer[256] = {0}, _recv_buffer[256] = {0};
        // Socks info
        u_int8_t _host_len = (u_int8_t)ipaddr.size();
        u_int16_t _host_port = htons((u_int16_t)port); // the port must be uint16

        /* Assemble the request packet */
		_buffer[0] = 0x05;			// version
		_buffer[1] = 0x01;			// command
		_buffer[2] = 0x00;			// reserved;
		_buffer[3] = 0x03;			// address type
		unsigned int _pos = 4;
		memcpy(_buffer + _pos, &_host_len, sizeof(_host_len));
		_pos += sizeof(_host_len);
        memcpy(_buffer + _pos, ipaddr.c_str(), ipaddr.size());
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
        if (read(m_socket, &_recv_buffer, 10) == -1) {
            return false;
        }

        /* Check the server's version. */
        if (_recv_buffer[0] != 0x05) {
            (void)fprintf(stderr, "Unsupported SOCKS version: %x\n", _recv_buffer[0]);
            return false;
        }
        int _is_failed = 1;
        /* Check server's reply */
        switch (_recv_buffer[1]) {
            case 0x00:
                _is_failed = 0;
                break;
            case 0x01:
                fprintf(stderr, "General SOCKS server failure.\n");
                break;
            case 0x02:
                fprintf(stderr, "Connection not allowed by ruleset.\n");
                break;
            case 0x03:
                fprintf(stderr, "Network Unreachable.\n");
                break;
            case 0x04:
                fprintf(stderr, "Host unreachable.\n");
                break;
            case 0x05:
                fprintf(stderr, "Connection refused.\n");
                break;
            case 0x06:
                fprintf(stderr, "TTL expired.\n");
                break;
            case 0x07:
                fprintf(stderr, "Command not supported.\n");
                break;
            case 0x08:
                fprintf(stderr, "Address type not supported.\n");
                break;
            default:
                fprintf(stderr, "ssh-socks5-proxy: SOCKS Server reply not understood\n");
                break;
        }

        if (_is_failed == 1) return false;

        /* Check ATYP */
        if (_recv_buffer[3] != 0x01) {
            fprintf(stderr, "ssh-socks5-proxy: Address type not supported: %u\n", _recv_buffer[3]);
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
	/*
    _svrfd = (struct pollfd *)calloc(1, sizeof(struct pollfd));
    _svrfd->events = POLLIN | POLLPRI;
    _svrfd->fd = m_socket;
	*/
    this->set_reusable();
    return true;
}
// Close the connection
void sl_tcpsocket::close()
{
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return;
    SL_NETWORK_CLOSESOCK(m_socket);
    m_socket = INVALIDATE_SOCKET;
	/*
    if ( _svrfd != NULL ) {
        free( _svrfd );
        _svrfd = NULL;
    }
	*/
}
// When the socket is a listener, use this method 
// to accept client's connection.
/*
sl_socket *sl_tcpsocket::get_client( u_int32_t timeout )
{
    if ( _svrfd == NULL ) return NULL;
    size_t _nfds = 1;   // number of fd
    int _ret = 0;
    _ret = poll( _svrfd, _nfds, timeout );
    if ( _ret == -1 ) {
        this->close();
        return NULL;
    }

    if ( _svrfd->revents == 0 ) {
        // No incoming socket
        return NULL;
    }
    if ( _svrfd->revents & POLLIN ) {
        // PINFO("New incoming socket.");
        struct sockaddr_in _sockInfoClient;
        int _len = 0;
        SOCKET_T _clt = accept(m_socket, (struct sockaddr *)&_sockInfoClient, (socklen_t *)&_len);

        // Accept and create new client socket.
        if ( _clt == -1 ) return NULL;
        sl_tcpsocket *_client_socket = new sl_tcpsocket;
        _client_socket->m_socket = _clt;
        _client_socket->set_reusable( );

        return _client_socket;
    }
    return NULL;
}
void sl_tcpsocket::release_client( sl_socket *client )
{
    if ( client == NULL ) return;
    delete client;
}
*/

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

// Read data from the socket until timeout or get any data.
bool sl_tcpsocket::read_data( string &buffer, u_int32_t timeout )
{
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return false;

    buffer = "";
    struct timeval _tv = { (long)timeout / 1000, 
        static_cast<int>(((long)timeout % 1000) * 1000) };
    fd_set recvFs;
    FD_ZERO( &recvFs );
    FD_SET( m_socket, &recvFs );

    // Buffer
    char _buffer[512] = { 0 };
    int _idleLoopCount = 5;

    do {
        // Wait for the incoming
        int _retCode = 0;
        do {
            _retCode = ::select( m_socket + 1, &recvFs, NULL, NULL, &_tv );
        } while ( _retCode < 0 && errno == EINTR );

        if ( _retCode < 0 ) // Error
            return false;
        if ( _retCode == 0 )    // TimeOut
            return true;

        // Get data from the socket cache
        _retCode = ::recv( m_socket, _buffer, 512, 0 );
        // Error happen when read data, means the socket has become invalidate
        if ( _retCode < 0 ) return false;
        if ( _retCode == 0 ) break; // Get EOF
        buffer.append( _buffer, _retCode );

        do {
            // Check if the socket has more data to read
            SOCKETSTATUE _status = socket_check_status(m_socket, SO_CHECK_READ);
            // Socket become invalidate
            if (_status == SO_INVALIDATE) 
            {
                if ( buffer.size() > 0 ) return true;
                return false;
            }
            // Socket become idle, and already read some data, means the peer finish sending one
            // package. We return the buffer and make the up-level to process the package.
            //PDUMP(_idleLoopCount);
            if (_status == SO_IDLE) {
                //PDUMP( _idleLoopCount );
				if ( buffer.size() > 0 ) return true;
                if ( _idleLoopCount > 0 ) _idleLoopCount -= 1;
                else return true;
            } else break;
        } while ( _idleLoopCount > 0 );
    } while ( true );

    // Useless
    return true;
}
// Write data to peer.
bool sl_tcpsocket::write_data( const string &data )
{
    if ( data.size() == 0 ) return false;
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return false;
    u_int32_t _write_timeout = 1000;

#if defined WIN32 || defined _WIN32 || defined WIN64 || defined _WIN64
    setsockopt( m_socket, SOL_SOCKET, SO_SNDTIMEO,
        (const char *)&_write_timeout, sizeof(Uint32) );
#else
    struct timeval wtv = { _write_timeout / 1000, 
        static_cast<int>((_write_timeout % 1000) * 1000) };
    setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, 
        (const char *)&wtv, sizeof(struct timeval));
#endif

    int _allSent = 0;
    int _lastSent = 0;

    u_int32_t _length = data.size();
    const char *_data = data.c_str();

    while ( (unsigned int)_allSent < _length )
    {
        _lastSent = ::send( m_socket, _data + _allSent, 
            (_length - (unsigned int)_allSent), 0 | SL_NETWORK_NOSIGNAL );
        if( _lastSent < 0 ) {
            // Failed to send
            return false;
        }
        _allSent += _lastSent;
    }
    return true;
}

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
