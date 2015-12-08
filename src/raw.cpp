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
#include <errno.h>

void sl_socket_close(SOCKET_T so)
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    //sl_event_unbind_handler(so);
    sl_events::server().unbind(so);
    close(so);
}

// TCP Methods
SOCKET_T sl_tcp_socket_init()
{
    SOCKET_T _so = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if ( SOCKET_NOT_VALIDATE(_so) ) {
        lerror << "failed to init a tcp socket: " << ::strerror( errno ) << lend;
        return _so;
    }
    // Set With TCP_NODELAY
    int flag = 1;
    if( setsockopt( _so, IPPROTO_TCP, 
        TCP_NODELAY, (const char *)&flag, sizeof(int) ) == -1 )
    {
        lerror << "failed to set the tcp socket(" << _so << ") to be TCP_NODELAY: " << ::strerror( errno ) << lend;
        SL_NETWORK_CLOSESOCK( _so );
        return INVALIDATE_SOCKET;
    }

    int _reused = 1;
    if ( setsockopt( _so, SOL_SOCKET, SO_REUSEADDR,
        (const char *)&_reused, sizeof(int) ) == -1)
    {
        lerror << "failed to set the tcp socket(" << _so << ") to be SO_REUSEADDR: " << ::strerror( errno ) << lend;
        SL_NETWORK_CLOSESOCK( _so );
        return INVALIDATE_SOCKET;
    }

    unsigned long _u = 1;
    if ( SL_NETWORK_IOCTL_CALL(_so, FIONBIO, &_u) < 0 ) 
    {
        lerror << "failed to set the tcp socket(" << _so << ") to be Non Blocking: " << ::strerror( errno ) << lend;
        SL_NETWORK_CLOSESOCK( _so );
        return INVALIDATE_SOCKET;
    }

    sl_events::server().bind(_so, sl_event_empty_handler());
    return _so;
}
bool sl_tcp_socket_connect(SOCKET_T tso, const sl_peerinfo& peer, function<void(SOCKET_T)> callback)
{
    if ( SOCKET_NOT_VALIDATE(tso) ) return false;

    struct sockaddr_in _sock_addr;
    memset(&_sock_addr, 0, sizeof(_sock_addr));
    _sock_addr.sin_addr.s_addr = peer.ipaddress;
    _sock_addr.sin_family = AF_INET;
    _sock_addr.sin_port = htons(peer.port_number);

    // Update the on connect event callback
    sl_events::server().update_handler(tso, SL_EVENT_CONNECT, [callback](sl_event e){
        if ( e.event != SL_EVENT_CONNECT ) return;
        callback(e.so);
    });

    // Update the failed handler
    sl_events::server().update_handler(tso, SL_EVENT_FAILED, [callback](sl_event e){
        if ( e.event != SL_EVENT_FAILED ) return;
        callback(INVALIDATE_SOCKET);
    });

    if ( ::connect( tso, (struct sockaddr *)&_sock_addr, sizeof(_sock_addr)) == -1 ) {
        int _error = 0, _len = sizeof(_error);
        getsockopt( tso, SOL_SOCKET, SO_ERROR, (char *)&_error, (socklen_t *)&_len);
        if ( _error != 0 ) {
            lerror << "failed to connect to " << peer << " on tcp socket: " << tso << ", " << ::strerror( _error ) << lend;
            return false;
        } else {
            // Monitor the socket, the poller will invoke on_connect when the socket is connected or failed.
            sl_poller::server().monitor_socket(tso, true);
        }
    } else {
        // Add to next run loop to process the connect event.
        sl_events::server().add_tcpevent(tso, SL_EVENT_CONNECT);
    }
    return true;
}
bool sl_tcp_socket_send(SOCKET_T tso, const string &pkg)
{
    if ( pkg.size() == 0 ) return false;
    if ( SOCKET_NOT_VALIDATE(tso) ) return false;

    int _lastSent = 0;

    unsigned int _length = pkg.size();
    const char *_data = pkg.c_str();

    while ( _length > 0 )
    {
        _lastSent = ::send( tso, _data, 
            _length, 0 | SL_NETWORK_NOSIGNAL );
        if( _lastSent <= 0 ) {
            // Failed to send
            lerror << "failed to send data on tcp socket: " << tso << ", " << ::strerror(errno) << lend;
            return false;
        }
        _data += _lastSent;
        _length -= _lastSent;
    }
    return true;
}
bool sl_tcp_socket_monitor(SOCKET_T tso, function<void(SOCKET_T)> callback)
{
    if ( SOCKET_NOT_VALIDATE(tso) ) return false;
    if ( !callback ) return false;
    auto _fp = [callback](sl_event e) {
        if ( e.event != SL_EVENT_READ ) return;
        callback(e.so);
    };
    sl_events::server().update_handler(tso, SL_EVENT_READ, _fp);

    // Update the failed callback
    sl_events::server().update_handler(tso, SL_EVENT_FAILED, [callback](sl_event e){
        if ( e.event != SL_EVENT_FAILED ) return;
        callback(INVALIDATE_SOCKET);
    });

    sl_poller::server().monitor_socket(tso, true);
    return true;
}
bool sl_tcp_socket_read(SOCKET_T tso, string& buffer, size_t max_buffer_size)
{
    if ( SOCKET_NOT_VALIDATE(tso) ) return false;
    
    // Socket must be nonblocking
    buffer.clear();
    buffer.resize(max_buffer_size);
    do {
        int _retCode = ::recv(tso, &buffer[0], max_buffer_size, 0 );
        if ( _retCode < 0 ) {
            int _error = 0, _len = sizeof(int);
            getsockopt( tso, SOL_SOCKET, SO_ERROR,
                    (char *)&_error, (socklen_t *)&_len);
            if ( _error == EINTR ) continue;    // signal 7, retry
            // Other error
            buffer.resize(0);
            lerror << "failed to receive data on tcp socket: " << tso << ", " << ::strerror( _error ) << lend;
            return false;
        } else if ( _retCode == 0 ) {
            // Peer Close
            buffer.resize(0);
            return false;
        } else {
            buffer.resize(_retCode);
            return true;
        }
    } while ( true );
    return true;
}

// UDP Methods
SOCKET_T sl_udp_socket_init()
{
    SOCKET_T _so = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if ( SOCKET_NOT_VALIDATE(_so) ) {
        lerror << "failed to init a udp socket: " << ::strerror( errno ) << lend;
        return _so;
    }
    // Bind to 0, so we can get the port number by getsockname
    struct sockaddr_in _usin = {};
    _usin.sin_family = AF_INET;
    _usin.sin_addr.s_addr = htonl(INADDR_ANY);
    _usin.sin_port = 0;
    bind(_so, (struct sockaddr *)&_usin, sizeof(_usin));

    // Bind the empty handler set
    //sl_event_bind_handler(_so, sl_event_empty_handler());
    sl_events::server().bind(_so, sl_event_empty_handler());
    return _so;
}

bool sl_udp_socket_send(SOCKET_T uso, const string &pkg, const sl_peerinfo& peer)
{
    if ( pkg.size() == 0 ) return false;
    if ( SOCKET_NOT_VALIDATE(uso) ) return false;

    int _allSent = 0;
    int _lastSent = 0;
    struct sockaddr_in _sock_addr = {};
    _sock_addr.sin_family = AF_INET;
    _sock_addr.sin_port = htons(peer.port_number);
    _sock_addr.sin_addr.s_addr = (uint32_t)peer.ipaddress;

    uint32_t _length = pkg.size();
    const char *_data = pkg.c_str();

    // Get the local port for debug usage.
    uint32_t _lport;
    network_sock_info_from_socket(uso, _lport);

    while ( (unsigned int)_allSent < _length )
    {
        _lastSent = ::sendto(uso, _data + _allSent, 
            (_length - (unsigned int)_allSent), 0, 
            (struct sockaddr *)&_sock_addr, sizeof(_sock_addr));
        if ( _lastSent < 0 ) {
            lerror << "failed to write data via udp socket(" << uso << ", 127.0.0.1:" << _lport << "): " << ::strerror(errno) << lend;
            return false;
        }
        _allSent += _lastSent;
    }
    return true;
}
bool sl_udp_socket_monitor(SOCKET_T uso, function<void(SOCKET_T, struct sockaddr_in)> callback)
{
    if ( SOCKET_NOT_VALIDATE(uso) ) return false;
    if ( !callback ) return false;
    auto _fp = [callback](sl_event e) {
        if ( e.event != SL_EVENT_READ ) return;
        callback(e.so, e.address);
    };
    //sl_event_update_handler(uso, SL_EVENT_READ, _fp);
    sl_events::server().update_handler(uso, SL_EVENT_READ, _fp);

    sl_events::server().update_handler(uso, SL_EVENT_FAILED, [callback](sl_event e){
        if ( e.event != SL_EVENT_FAILED ) return;
        callback(INVALIDATE_SOCKET, e.address);
    });

    sl_poller::server().monitor_socket(uso, true);
    return true;
}
bool sl_udp_socket_read(SOCKET_T uso, struct sockaddr_in addr, string& buffer, size_t max_buffer_size)
{
    if ( SOCKET_NOT_VALIDATE(uso) ) return false;

    buffer.clear();
    buffer.resize(max_buffer_size);

    do {
        unsigned _so_len = sizeof(addr);
        int _retCode = ::recvfrom( uso, &buffer[0], max_buffer_size, 0,
            (struct sockaddr *)&addr, &_so_len);
        if ( _retCode < 0 ) {
            int _error = 0, _len = sizeof(int);
            getsockopt( uso, SOL_SOCKET, SO_ERROR,
                    (char *)&_error, (socklen_t *)&_len);
            if ( _error == EINTR ) continue;    // signal 7, retry
            // Other error
            lerror << "failed to receive data on udp socket: " << uso << ", " << ::strerror( _error ) << lend;
            buffer.resize(0);
            return false;
        } else if ( _retCode == 0 ) {
            // Peer Close
            buffer.resize(0);
            return false;
        } else {
            buffer.resize(_retCode);
            return true;
        }
    } while ( true );
    return true;
}

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
