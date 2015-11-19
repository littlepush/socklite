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

#include "udpsocket.h"

sl_udpsocket::sl_udpsocket(bool iswrapper)
: sl_socket(iswrapper)
{
    // nothing
}
sl_udpsocket::sl_udpsocket(SOCKET_T so)
: sl_socket(true)
{
    m_socket = so;
}
sl_udpsocket::sl_udpsocket(SOCKET_T so, struct sockaddr_in addr)
: sl_socket(true)
{
    m_socket = so;
    // Copy the address
    memcpy(&m_sock_addr, &addr, sizeof(addr));
}

sl_udpsocket::~sl_udpsocket()
{
}

// The IP Address information for peer socket
string & sl_udpsocket::ipaddress( string & ipstr ) const
{
    network_int_to_ipaddress(m_sock_addr.sin_addr.s_addr, ipstr);
    return ipstr;
}
// The Port of peer socket
uint32_t sl_udpsocket::port() const
{
    return ntohs(m_sock_addr.sin_port);
}

// Connect to peer
bool sl_udpsocket::connect( const string &ipaddr, uint32_t port, uint32_t timeout )
{
    memset( &m_sock_addr, 0, sizeof(m_sock_addr) );
    m_sock_addr.sin_family = AF_INET;
    m_sock_addr.sin_port = htons(port);
    char _ip[16];
    if ( inet_aton(network_domain_to_ip(ipaddr.c_str(), _ip, 16), &m_sock_addr.sin_addr) == 0 ) {
        return false;
    }

    // Create Socket Handle
    m_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if ( SOCKET_NOT_VALIDATE(m_socket) ) {
        return false;
    }
    // Bind to 0, so we can get the port number by getsockname
    struct sockaddr_in _usin = {};
    _usin.sin_family = AF_INET;
    _usin.sin_addr.s_addr = htonl(INADDR_ANY);
    _usin.sin_port = 0;
    bind(m_socket, (struct sockaddr *)&_usin, sizeof(_usin));

    return true;
}
// Listen on specified port and address, default is 0.0.0.0
bool sl_udpsocket::listen( uint32_t port, uint32_t ipaddr )
{
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return false;
    memset((char *)&m_sock_addr, 0, sizeof(m_sock_addr));

    m_sock_addr.sin_family = AF_INET;
    m_sock_addr.sin_port = htons(port);
    m_sock_addr.sin_addr.s_addr = htonl(ipaddr);
    if ( ::bind(m_socket, (struct sockaddr *)&m_sock_addr, sizeof(m_sock_addr)) == -1 ) {
        SL_NETWORK_CLOSESOCK(m_socket);
        return false;
    }
    return true;
}

// Read data from the socket until timeout or get any data.
SO_READ_STATUE sl_udpsocket::read_data( string &buffer, uint32_t timeout )
{
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return SO_READ_CLOSE;

    // Set the receive time out
    struct timeval _tv = { (int)timeout / 1000, (int)timeout % 1000 * 1000 };
    if ( setsockopt( m_socket, SOL_SOCKET, SO_RCVTIMEO, &_tv, sizeof(_tv) ) == -1)
        return SO_READ_CLOSE;

    buffer.clear();
    size_t _bsize = buffer.size();
    buffer.resize(_bsize + 1024);

    int _data_len = 0;
    do {
        unsigned _so_len = sizeof(m_sock_addr);
        _data_len = ::recvfrom( m_socket, &buffer[_bsize], 1024, 0,
            (struct sockaddr *)&m_sock_addr, &_so_len);
        if ( _data_len == 1024 ) {
            // m_buffer is full, so maybe still has data
            _bsize = buffer.size();
            buffer.resize(_bsize + 1024);
            continue;
        } else if ( _data_len < 0 ) {
            // Error Occurred
            buffer.resize(0);
        } else {
            _bsize += _data_len;
            buffer.resize(_bsize);
        }
        break;
    } while( true );

    return (_data_len >= 0) ? SO_READ_DONE : SO_READ_TIMEOUT;
}
SO_READ_STATUE sl_udpsocket::recv(string &buffer, unsigned int max_buffer_len)
{
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return SO_READ_CLOSE;

    buffer.clear();
    buffer.resize(max_buffer_len);

    do {
        unsigned _so_len = sizeof(m_sock_addr);
        int _retCode = ::recvfrom( m_socket, &buffer[0], max_buffer_len, 0,
            (struct sockaddr *)&m_sock_addr, &_so_len);
        if ( _retCode < 0 ) {
            int _error = 0, _len = sizeof(int);
            getsockopt( m_socket, SOL_SOCKET, SO_ERROR,
                    (char *)&_error, (socklen_t *)&_len);
            if ( _error == EINTR ) continue;    // signal 7, retry
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
}

// Write data to peer.
bool sl_udpsocket::write_data( const string &data )
{
    if ( data.size() == 0 ) return false;
    if ( SOCKET_NOT_VALIDATE(m_socket) ) return false;

    int _allSent = 0;
    int _lastSent = 0;

    uint32_t _length = data.size();
    const char *_data = data.c_str();

    while ( (unsigned int)_allSent < _length )
    {
        _lastSent = ::sendto(m_socket, _data + _allSent, 
            (_length - (unsigned int)_allSent), 0, 
            (struct sockaddr *)&m_sock_addr, sizeof(m_sock_addr));
        if ( _lastSent < 0 ) {
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
