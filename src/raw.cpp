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

#if SL_TARGET_LINUX
#include <limits.h>
#include <linux/netfilter_ipv4.h>
#endif

void sl_socket_close(SOCKET_T so)
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;
    //sl_event_unbind_handler(so);
    ldebug << "the socket " << so << " will be unbind and closed" << lend;
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
bool sl_tcp_socket_connect(SOCKET_T tso, const sl_peerinfo& peer, sl_socket_event_handler callback)
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
        //callback(e.so);
        callback(e);
    });

    // Update the failed handler
    sl_events::server().update_handler(tso, SL_EVENT_FAILED, [callback](sl_event e){
        if ( e.event != SL_EVENT_FAILED ) return;
        //callback(INVALIDATE_SOCKET);
        callback(e);
    });

    if ( ::connect( tso, (struct sockaddr *)&_sock_addr, sizeof(_sock_addr)) == -1 ) {
        int _error = 0, _len = sizeof(_error);
        getsockopt( tso, SOL_SOCKET, SO_ERROR, (char *)&_error, (socklen_t *)&_len);
        if ( _error != 0 ) {
            lerror << "failed to connect to " << peer << " on tcp socket: " << tso << ", " << ::strerror( _error ) << lend;
            return false;
        } else {
            // Monitor the socket, the poller will invoke on_connect when the socket is connected or failed.
            ldebug << "monitor tcp socket " << tso << " for connecting" << lend;
            if ( !sl_poller::server().monitor_socket(tso, true, SL_EVENT_CONNECT) ) {
                //sl_events::server().add_tcpevent(tso, SL_EVENT_FAILED);
                return false;
            }
        }
    } else {
        // Add to next run loop to process the connect event.
        sl_events::server().add_tcpevent(tso, SL_EVENT_CONNECT);
    }
    return true;
}
bool sl_tcp_socket_connect(SOCKET_T tso, const vector<sl_ip> &iplist, uint16_t port, uint32_t index, sl_socket_event_handler callback) {
    bool _retval = true;
    do {
        ldebug << "iplist count: " << iplist.size() << ", current index: " << index << lend;
        if ( iplist.size() <= index ) return false;
        sl_peerinfo _pi((const string &)iplist[index], port);
        ldebug << "try to connect to " << _pi << ", this is the " << index << " item in iplist" << lend;
        _retval = sl_tcp_socket_connect(tso, sl_peerinfo((const string &)iplist[index], port), [iplist, port, index, callback](sl_event e) {
            if ( e.event == SL_EVENT_FAILED ) {
                // go to next
                if ( !sl_tcp_socket_connect(e.so, iplist, port, index + 1, callback) ) {
                    e.event = SL_EVENT_FAILED;
                    callback(e);
                }
            } else {
                // Connected
                callback(e);
            }
        });
        if ( _retval == false ) ++index;
    } while ( _retval == false );
    return _retval;
}
bool sl_tcp_socket_connect(SOCKET_T tso, const sl_peerinfo& socks5, const string& host, uint16_t port, sl_socket_event_handler callback)
{
    if ( socks5 ) {
        ldebug << "try to connect via a socks5 proxy: " << socks5 << lend;
        return ( sl_tcp_socket_connect(tso, socks5, [socks5, host, port, callback](sl_event e){
            if ( e.event == SL_EVENT_FAILED ) {
                lerror << "the socks5 proxy cannot be connected" << socks5 << lend;
                callback(e); return;
            }
            sl_socks5_noauth_request _req;
            // Exchange version info
            if (write(e.so, (char *)&_req, sizeof(_req)) < 0) {
                e.event = SL_EVENT_FAILED; callback(e); return;
            }

            sl_tcp_socket_monitor(e.so, [host, port, callback](sl_event e) {
                if ( e.event == SL_EVENT_FAILED ) {
                    callback(e); return;
                }
                string _pkg;
                if ( !sl_tcp_socket_read(e.so, _pkg) ) {
                    e.event = SL_EVENT_FAILED; callback(e); return;
                }
                const sl_socks5_handshake_response* _resp = (const sl_socks5_handshake_response *)_pkg.c_str();
                // This api is for no-auth proxy
                if ( _resp->ver != 0x05 && _resp->method != sl_method_noauth ) {
                    lerror << "unsupported authentication method" << lend;
                    e.event = SL_EVENT_FAILED; callback(e); return;
                }

                // Send the connect request
                // Establish a connection through the proxy server.
                uint8_t _buffer[256] = {0};
                // Socks info
                uint16_t _host_port = htons(port); // the port must be uint16

                /* Assemble the request packet */
                sl_socks5_connect_request _req;
                _req.atyp = sl_socks5atyp_dname;
                memcpy(_buffer, (char *)&_req, sizeof(_req));

                unsigned int _pos = sizeof(_req);
                _buffer[_pos] = (uint8_t)host.size();
                _pos += 1;
                memcpy(_buffer + _pos, host.data(), host.size());
                _pos += host.size();
                memcpy(_buffer + _pos, &_host_port, sizeof(_host_port));
                _pos += sizeof(_host_port);
                
                if (write(e.so, _buffer, _pos) == -1) {
                    e.event = SL_EVENT_FAILED; callback(e); return;
                }

                // Wait for the socks5 server's response
                sl_tcp_socket_monitor(e.so, [callback](sl_event e) {
                    if ( e.event == SL_EVENT_FAILED ) {
                        callback(e); return;
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
                    string _pkg;
                    if (!sl_tcp_socket_read(e.so, _pkg)) {
                        e.event = SL_EVENT_FAILED; callback(e); return;
                    }
                    const sl_socks5_ipv4_response* _resp = (const sl_socks5_ipv4_response *)_pkg.c_str();

                    /* Check the server's version. */
                    if ( _resp->ver != 0x05 ) {
                        lerror << "Unsupported SOCKS version: " << _resp->ver << lend;
                        e.event = SL_EVENT_FAILED; callback(e); return;
                    }
                    if (_resp->rep != sl_socks5rep_successed) {
                        lerror << sl_socks5msg((sl_socks5rep)_resp->rep) << lend;
                        e.event = SL_EVENT_FAILED; callback(e); return;
                    }

                    /* Check ATYP */
                    if ( _resp->atyp != sl_socks5atyp_ipv4 ) {
                        lerror << "ssh-socks5-proxy: Address type not supported: " << _resp->atyp << lend;
                        e.event = SL_EVENT_FAILED; callback(e); return;
                    }
                    e.event = SL_EVENT_CONNECT; callback(e);
                }) ? void() : [&e, callback]() { e.event = SL_EVENT_FAILED; callback(e); }();
            }) ? void() : [&e, callback](){ e.event = SL_EVENT_FAILED; callback(e); }();
        }));
    } else {
        ldebug << "the socks5 is empty, try to connect to host(" << host << ") directly" << lend;
        sl_ip _host_ip(host);
        if ( (uint32_t)_host_ip == (uint32_t)-1 ) {
            ldebug << "the host(" << host << ") is not an IP address, try to resolve first" << lend;
            // This is a domain
            sl_async_gethostname(host, [tso, host, port, callback](const vector<sl_ip> &iplist){
                if ( iplist.size() == 0 || ((uint32_t)iplist[0] == (uint32_t)-1) ) {
                    // Error
                    lerror << "failed to resolv " << host << lend;
                    sl_event _e;
                    _e.so = tso;
                    _e.event = SL_EVENT_FAILED;
                    callback(_e);
                } else {
                    ldebug << "resolvd the host " << host << ", trying to connect via tcp socket" << lend;
                    if ( !sl_tcp_socket_connect(tso, iplist, port, 0, callback) ) {
                        lerror << "failed to connect to the host(" << host << ")" << lend;
                        sl_event _e;
                        _e.so = tso;
                        _e.event = SL_EVENT_FAILED;
                        callback(_e);
                    }
                }
            });
            return true;
        }
        return sl_tcp_socket_connect(tso, sl_peerinfo(host, port), callback);
    }
}
bool sl_tcp_socket_send(SOCKET_T tso, const string &pkg)
{
    if ( pkg.size() == 0 ) return false;
    if ( SOCKET_NOT_VALIDATE(tso) ) return false;

    ldebug << "will write data(l:" << pkg.size() << ") to tcp socket: " << tso << lend;
    int _lastSent = 0;

    unsigned int _length = pkg.size();
    const char *_data = pkg.c_str();

    while ( _length > 0 )
    {
        _lastSent = ::send( tso, _data, 
            _length, 0 | SL_NETWORK_NOSIGNAL );
        if( _lastSent <= 0 ) {
            if ( ENOBUFS == errno ) {
                // try to increase the write buffer and then retry
                uint32_t _wmem = 0, _lmem = 0;
                getsockopt(tso, SOL_SOCKET, SO_SNDBUF, (char *)&_wmem, &_lmem);
                _wmem *= 2; // double the buffer
                setsockopt(tso, SOL_SOCKET, SO_SNDBUF, (char *)&_wmem, _lmem);
            } else {
                // Failed to send
                lerror << "failed to send data on tcp socket: " << tso << ", " << ::strerror(errno) << lend;
                return false;
            }
        } else {
            _data += _lastSent;
            _length -= _lastSent;
        }
    }
    return true;
}
bool sl_tcp_socket_monitor(SOCKET_T tso, sl_socket_event_handler callback, bool new_incoming)
{
    if ( SOCKET_NOT_VALIDATE(tso) ) return false;
    if ( !callback ) return false;
    auto _fp = [callback](sl_event e) {
        if ( e.event != SL_EVENT_READ ) return;
        //callback(e.so);
        callback(e);
    };
    sl_events::server().update_handler(tso, SL_EVENT_READ, _fp);

    // Update the failed callback
    sl_events::server().update_handler(tso, SL_EVENT_FAILED, [callback](sl_event e){
        if ( e.event != SL_EVENT_FAILED ) return;
        //callback(INVALIDATE_SOCKET);
        callback(e);
    });

    return sl_poller::server().monitor_socket(tso, true, SL_EVENT_DEFAULT, !new_incoming);
}
bool sl_tcp_socket_read(SOCKET_T tso, string& buffer, size_t max_buffer_size)
{
    if ( SOCKET_NOT_VALIDATE(tso) ) return false;
    
    // Socket must be nonblocking
    buffer.clear();
    buffer.resize(max_buffer_size);
    size_t _received = 0;
    size_t _leftspace = max_buffer_size;

    do {
        int _retCode = ::recv(tso, &buffer[0] + _received, _leftspace, 0 );
        if ( _retCode < 0 ) {
            int _error = 0, _len = sizeof(int);
            getsockopt( tso, SOL_SOCKET, SO_ERROR,
                    (char *)&_error, (socklen_t *)&_len);
            if ( _error == EINTR ) continue;    // signal 7, retry
            if ( _error == EAGAIN ) {
                // No more data on a non-blocking socket
                buffer.resize(_received);
                return true;
            }
            // Other error
            buffer.resize(0);
            lerror << "failed to receive data on tcp socket: " << tso << ", " << ::strerror( _error ) << lend;
            return false;
        } else if ( _retCode == 0 ) {
            // Peer Close
            buffer.resize(0);
            return false;
        } else {
            _received += _retCode;
            _leftspace -= _retCode;
            if ( _leftspace > 0 ) {
                // Unfull
                buffer.resize(_retCode);
                return true;
            } else {
                // The buffer is full, try to double the buffer and try again
                max_buffer_size *= 2;
                _leftspace = max_buffer_size - _received;
                buffer.resize(max_buffer_size);
            }
        }
    } while ( true );
    return true;
}
bool sl_tcp_socket_listen(SOCKET_T tso, const sl_peerinfo& bind_port, sl_socket_event_handler accept_callback)
{
    if ( SOCKET_NOT_VALIDATE(tso) ) return false;
    struct sockaddr_in _sock_addr;
    memset((char *)&_sock_addr, 0, sizeof(_sock_addr));
    _sock_addr.sin_family = AF_INET;
    _sock_addr.sin_port = htons(bind_port.port_number);
    _sock_addr.sin_addr.s_addr = bind_port.ipaddress;

    sl_events::server().update_handler(tso, SL_EVENT_ACCEPT, [accept_callback](sl_event e) {
        if ( e.event != SL_EVENT_ACCEPT ) {
            lerror << "the incoming socket event is not an accept event." << lend;
            return;
        }
        // Set With TCP_NODELAY
        int flag = 1;
        if( setsockopt( e.so, IPPROTO_TCP, 
            TCP_NODELAY, (const char *)&flag, sizeof(int) ) == -1 )
        {
            lerror << "failed to set the tcp socket(" << e.so << ") to be TCP_NODELAY: " << ::strerror( errno ) << lend;
            SL_NETWORK_CLOSESOCK( e.so );
            return;
        }

        int _reused = 1;
        if ( setsockopt( e.so, SOL_SOCKET, SO_REUSEADDR,
            (const char *)&_reused, sizeof(int) ) == -1)
        {
            lerror << "failed to set the tcp socket(" << e.so << ") to be SO_REUSEADDR: " << ::strerror( errno ) << lend;
            SL_NETWORK_CLOSESOCK( e.so );
            return;
        }

        unsigned long _u = 1;
        if ( SL_NETWORK_IOCTL_CALL(e.so, FIONBIO, &_u) < 0 ) 
        {
            lerror << "failed to set the tcp socket(" << e.so << ") to be Non Blocking: " << ::strerror( errno ) << lend;
            SL_NETWORK_CLOSESOCK( e.so );
            return;
        }

        sl_events::server().bind(e.so, sl_event_empty_handler());
        accept_callback(e);
    });

    if ( ::bind(tso, (struct sockaddr *)&_sock_addr, sizeof(_sock_addr)) == -1 ) {
        lerror << "failed to listen tcp on " << bind_port << ": " << ::strerror( errno ) << lend;
        return false;
    }
    if ( -1 == ::listen(tso, 1024) ) {
        lerror << "failed to listen tcp on " << bind_port << ": " << ::strerror( errno ) << lend;
        return false;
    }
    linfo << "start to listening tcp on " << bind_port << lend;
    if ( !sl_poller::server().bind_tcp_server(tso) ) {
        return false;
    }
    return true;
}
sl_peerinfo sl_tcp_get_original_dest(SOCKET_T tso)
{
    if ( SOCKET_NOT_VALIDATE(tso) ) return sl_peerinfo(INADDR_ANY, 0);
#if SL_TARGET_LINUX
    struct sockaddr_in _dest_addr;
    socklen_t _socklen = sizeof(_dest_addr);
    int _error = getsockopt( tso, SOL_IP, SO_ORIGINAL_DST, &_dest_addr, &_socklen );
    if ( _error ) return sl_peerinfo(INADDR_ANY, 0);
    return sl_peerinfo(_dest_addr.sin_addr.s_addr, ntohs(_dest_addr.sin_port));
#else
    return sl_peerinfo(INADDR_ANY, 0);
#endif
}
// UDP Methods
SOCKET_T sl_udp_socket_init(const sl_peerinfo& bind_addr)
{
    SOCKET_T _so = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if ( SOCKET_NOT_VALIDATE(_so) ) {
        lerror << "failed to init a udp socket: " << ::strerror( errno ) << lend;
        return _so;
    }
    // Bind to 0, so we can get the port number by getsockname
    struct sockaddr_in _usin = {};
    _usin.sin_family = AF_INET;
    _usin.sin_addr.s_addr = bind_addr.ipaddress;
    _usin.sin_port = htons(bind_addr.port_number);
    if ( -1 == ::bind(_so, (struct sockaddr *)&_usin, sizeof(_usin)) ) {
        lerror << "failed to create a udp socket and bind to " << bind_addr << lend;
        SL_NETWORK_CLOSESOCK(_so);
        return INVALIDATE_SOCKET;
    }

    // Try to set the udp socket as nonblocking
    unsigned long _u = 1;
    SL_NETWORK_IOCTL_CALL(_so, FIONBIO, &_u);

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
bool sl_udp_socket_monitor(SOCKET_T uso, const sl_peerinfo& peer, sl_socket_event_handler callback)
{
    if ( SOCKET_NOT_VALIDATE(uso) ) return false;
    if ( !callback ) return false;

    sl_events::server().update_handler(uso, SL_EVENT_READ, [peer, callback](sl_event e) {
        if ( e.event != SL_EVENT_READ ) return;
        ldebug << "udp socket " << e.so << " did get read event callback, which means has incoming data" << lend;
        if ( peer ) {
            e.address.sin_family = AF_INET;
            e.address.sin_port = htons(peer.port_number);
            e.address.sin_addr.s_addr = (uint32_t)peer.ipaddress;
        }
        //callback(e.so, e.address);
        callback(e);
    });

    sl_events::server().update_handler(uso, SL_EVENT_FAILED, [peer, callback](sl_event e){
        if ( e.event != SL_EVENT_FAILED ) return;
        //callback(INVALIDATE_SOCKET, e.address);
        if ( peer ) {
            e.address.sin_family = AF_INET;
            e.address.sin_port = htons(peer.port_number);
            e.address.sin_addr.s_addr = (uint32_t)peer.ipaddress;
        }
        callback(e);
    });
    ldebug << "did update the handler for udp socket " << uso << " on SL_EVENT_READ(2) and SL_EVENT_FAILED(4)" << lend;
    return sl_poller::server().monitor_socket(uso, true, SL_EVENT_DEFAULT);
}
bool sl_udp_socket_read(SOCKET_T uso, struct sockaddr_in addr, string& buffer, size_t max_buffer_size)
{
    if ( SOCKET_NOT_VALIDATE(uso) ) return false;

    sl_peerinfo _pi(addr.sin_addr.s_addr, ntohs(addr.sin_port));
    ldebug << "udp socket " << uso << " tring to read data from " << _pi << lend;
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

bool sl_udp_socket_listen(SOCKET_T uso, sl_socket_event_handler accept_callback)
{
    if ( SOCKET_NOT_VALIDATE(uso) ) return false;

    sl_events::server().update_handler(uso, SL_EVENT_ACCEPT, [accept_callback](sl_event e) {
        if ( e.event != SL_EVENT_DATA ) {
            lerror << "the incoming socket event is not an accept event." << lend;
            return;
        }
        accept_callback(e);
        bool _ret = false;
        do{
            _ret = sl_poller::server().bind_udp_server(e.so);
            if ( _ret == false ) {
                usleep(1000000);
            }
        } while( _ret == false );
    });

    uint32_t _port;
    network_sock_info_from_socket(uso, _port);
    linfo << "start to listening udp on " << _port << lend;
    sl_poller::server().bind_udp_server(uso);
    return true;
}

#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

// Global DNS Server List
vector<sl_peerinfo> _resolv_list;
void __sl_async_gethostnmae_udp(const string&& query_pkg, size_t use_index, async_dns_handler fp);
void __sl_async_gethostnmae_tcp(const string&& query_pkg, size_t use_index, async_dns_handler fp);

void __sl_async_gethostnmae_udp(const string&& query_pkg, size_t use_index, async_dns_handler fp)
{
    // No other validate resolve ip in the list, return the 255.255.255.255
    if ( _resolv_list.size() == use_index ) {
        lwarning << "no more nameserver validated" << lend;
        fp( {sl_ip((uint32_t)-1)} );
        return;
    }

    // Create a new udp socket and send the query package.
    SOCKET_T _uso = sl_udp_socket_init();
    string _domain;
    dns_get_domain(query_pkg.c_str(), query_pkg.size(), _domain);
    ldebug << "initialize a udp socket " << _uso << " to query domain: " << _domain << lend;
    if ( !sl_udp_socket_send(_uso, query_pkg, _resolv_list[use_index]) ) {
        lerror << "failed to send dns query package to " << _resolv_list[use_index] << lend;
        // Failed to send( unable to access the server );
        sl_socket_close(_uso);
        // Go next server
        __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
        return;
    }

    // Monitor for the response data
    sl_udp_socket_monitor(_uso, _resolv_list[use_index], [&query_pkg, use_index, fp](sl_event e) {
        // Current server has closed the socket
        if ( e.event == SL_EVENT_FAILED ) {
            lerror << "failed to get response from " << _resolv_list[use_index] << " for dns query." << lend;
            sl_socket_close(e.so);
            __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
            return;
        }

        // Read the incoming package
        string _incoming_pkg;
        if (!sl_udp_socket_read(e.so, e.address, _incoming_pkg)) {
            lerror << "failed to read data from udp socket for dns query." << lend;
            sl_socket_close(e.so);
            __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
            return;
        }
        sl_socket_close(e.so);

        const clnd_dns_package *_pheader = (const clnd_dns_package *)_incoming_pkg.c_str();
        if ( _pheader->get_resp_code() == dns_rcode_noerr ) {
            vector<uint32_t> _a_recs;
            string _qdomain;
            dns_get_a_records(_incoming_pkg.c_str(), _incoming_pkg.size(), _qdomain, _a_recs);
            vector<sl_ip> _retval;
            for ( auto _a : _a_recs ) {
                _retval.push_back(sl_ip(_a));
            }
            fp( _retval );
        } else if ( _pheader->get_is_truncation() ) {
            // TRUNC flag get, try to use tcp
            __sl_async_gethostnmae_tcp(move(query_pkg), use_index, fp);
        } else {
            __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
        }
    }) ? void() : [&query_pkg, use_index, fp, _uso](){
        lerror << "failed to monitor on " << _uso << lend;
        sl_socket_close(_uso);
        // Go next server
        __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
        return;
    }();
}
void __sl_async_gethostnmae_tcp(const string&& query_pkg, size_t use_index, async_dns_handler fp)
{
    SOCKET_T _tso = sl_tcp_socket_init();
    if ( SOCKET_NOT_VALIDATE(_tso) ) {
        // No enough file handler
        __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
        return;
    }

    sl_tcp_socket_connect(_tso, _resolv_list[use_index], [&query_pkg, use_index, fp](sl_event e) {
        if ( e.event == SL_EVENT_FAILED ) {
            // Server not support tcp
            //sl_socket_close(_tso);
            sl_socket_close(e.so);
            __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
            return;
        }
        string _tpkg;
        dns_generate_tcp_redirect_package(move(query_pkg), _tpkg);
        if ( !sl_tcp_socket_send(e.so, _tpkg) ) {
            // Failed to send
            sl_socket_close(e.so);
            __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
            return;
        }
        sl_tcp_socket_monitor(e.so, [&query_pkg, use_index, fp](sl_event e) {
            if ( e.event == SL_EVENT_FAILED ) {
                // Peer closed
                sl_socket_close(e.so);
                __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
                return;
            }

            // Read incoming
            string _tcp_incoming_pkg;
            if ( !sl_tcp_socket_read(e.so, _tcp_incoming_pkg) ) {
                sl_socket_close(e.so);
                __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
                return;
            }
            sl_socket_close(e.so);

            string _incoming_pkg;
            dns_generate_udp_response_package_from_tcp(_tcp_incoming_pkg, _incoming_pkg);
            const clnd_dns_package *_pheader = (const clnd_dns_package *)_incoming_pkg.c_str();
            if ( _pheader->get_resp_code() == dns_rcode_noerr ) {
                vector<uint32_t> _a_recs;
                string _qdomain;
                dns_get_a_records(_incoming_pkg.c_str(), _incoming_pkg.size(), _qdomain, _a_recs);
                vector<sl_ip> _retval;
                for ( auto _a : _a_recs ) {
                    _retval.push_back(sl_ip(_a));
                }
                fp( _retval );
            } else {
                __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
            }
        }) ? void() : [&query_pkg, use_index, fp, e](){
            lerror << "failed to monitor on " << e.so << lend;
            sl_socket_close(e.so);
            __sl_async_gethostnmae_udp(move(query_pkg), use_index + 1, fp);
        }();
    });
}
void sl_async_gethostname(const string& host, async_dns_handler fp)
{
    if ( _resolv_list.size() == 0 ) {
        res_init();
        for ( int i = 0; i < _res.nscount; ++i ) {
            sl_peerinfo _pi(
                _res.nsaddr_list[i].sin_addr.s_addr, 
                ntohs(_res.nsaddr_list[i].sin_port)
                );
            _resolv_list.push_back(_pi);
            ldebug << "resolv get dns: " << _pi << lend;
        }
    }
    string _qpkg;
    dns_generate_query_package(host, _qpkg);
    __sl_async_gethostnmae_udp(move(_qpkg), 0, fp);
}

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
