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
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>


#include <queue>

// The socket's write package structure
typedef struct sl_write_packet {
    string                          packet;
    size_t                          sent_size;
    sl_peerinfo                     peerinfo;
    sl_socket_event_handler         callback;
} sl_write_packet;
typedef shared_ptr<sl_write_packet>                 sl_shared_write_packet_t;

typedef struct sl_write_info {
    shared_ptr< mutex >                             locker;
    shared_ptr< queue<sl_shared_write_packet_t> >   packet_queue;
} sl_write_info;

typedef map< SOCKET_T, sl_write_info >              sl_write_map_t;

mutex               _g_so_write_mutex;
sl_write_map_t      _g_so_write_map;

/*!
    Close the socket and release the handler set 

    @Description
    This method will close the socket(udp or tcp) and release all cache/buffer
    associalate with it.
*/
void sl_socket_close(SOCKET_T so)
{
    if ( SOCKET_NOT_VALIDATE(so) ) return;

    // ldebug << "the socket " << so << " will be unbind and closed" << lend;
    sl_events::server().unbind(so);

    // Remove all pending write package
    do {
        lock_guard<mutex> _(_g_so_write_mutex);
        _g_so_write_map.erase(so);
    } while(false);

    close(so);
}

/*
    Monitor the socket for incoming data.

    @Description
    As reading action will block current thread if there is no data right now,
    this method will add an EPOLLIN(Linux)/EVFILT_READ(BSD) event to the queue.

    In Linux, as epoll will combine read and write flag in one set, this method
    will always monitor both EPOLLIN and EPOLLOUT.
    For a BSD based system use kqueue, will only add a EVFILT_READ to the queue.
*/
void sl_socket_monitor(
    SOCKET_T tso,
    uint32_t timedout,
    sl_socket_event_handler callback
)
{
    if ( SOCKET_NOT_VALIDATE(tso) ) return;
    if ( !callback ) return;
    sl_events::server().monitor(tso, SL_EVENT_READ, callback, timedout);
}

/*
    Bind Default Failed Handler for a Socket

    @Description
    Bind the default handler for SL_EVENT_FAILED of a socket.
    In any case if the socket receive a SL_EVENT_FAILED event, will
    invoke this handler.
    Wether set this handler or not, system will close the socket
    automatically. Which means, if you receive a SL_EVENT_FAILED
    event, the socket assigned in the sl_event structure has
    already been closed.
*/
void sl_socket_bind_event_failed(SOCKET_T so, sl_socket_event_handler handler)
{
    sl_events::server().update_handler(
        so, 
        SL_EVENT_FAILED, 
        [=](sl_event e){
            sl_socket_close(e.so);
            if ( handler ) handler(e);
        }
    );
}

/*
    Bind Default TimedOut Handler for a Socket

    @Description
    Bind the default timedout handler for SL_EVENT_TIMEOUT of a socket.
    If a socket receive a timedout event, the system will invoke this
    handler.
    If not bind this handler, system will close the socket automatically,
    otherwise, a timedout socket will NOT be closed.
*/
void sl_socket_bind_event_timeout(SOCKET_T so, sl_socket_event_handler handler)
{
    sl_events::server().update_handler(
        so, 
        SL_EVENT_TIMEOUT, 
        [=](sl_event e){
            if ( handler ) handler(e);
            else sl_socket_close(e.so);
        }
    );
}

// TCP Methods
/*!
    Initialize a TCP socket.

    @Description
    This method will create a new tcp socket file descriptor, the fd will
    be set as TCP_NODELAY, SO_REUSEADDR and NON_BLOCKING.
    And will automatically bind empty handler set in the event system.
*/
SOCKET_T _raw_internal_tcp_socket_init(
    sl_socket_event_handler failed = NULL, 
    sl_socket_event_handler timedout = NULL,
    SOCKET_T tso = INVALIDATE_SOCKET
)
{
    SOCKET_T _so = tso;
    if ( SOCKET_NOT_VALIDATE(_so) ) {
        _so = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    if ( SOCKET_NOT_VALIDATE(_so) ) {
        lerror 
            << "failed to init a tcp socket: " 
            << ::strerror( errno ) 
        << lend;
        return _so;
    }
    // Set With TCP_NODELAY
    int flag = 1;
    if( setsockopt( _so, IPPROTO_TCP, 
        TCP_NODELAY, (const char *)&flag, sizeof(int) ) == -1 )
    {
        lerror 
            << "failed to set the tcp socket(" 
            << _so << ") to be TCP_NODELAY: " 
            << ::strerror( errno ) 
        << lend;
        SL_NETWORK_CLOSESOCK( _so );
        return INVALIDATE_SOCKET;
    }

    int _reused = 1;
    if ( setsockopt( _so, SOL_SOCKET, SO_REUSEADDR,
        (const char *)&_reused, sizeof(int) ) == -1)
    {
        lerror 
            << "failed to set the tcp socket(" 
            << _so << ") to be SO_REUSEADDR: " 
            << ::strerror( errno ) 
        << lend;
        SL_NETWORK_CLOSESOCK( _so );
        return INVALIDATE_SOCKET;
    }

    unsigned long _u = 1;
    if ( SL_NETWORK_IOCTL_CALL(_so, FIONBIO, &_u) < 0 ) 
    {
        lerror 
            << "failed to set the tcp socket("
            << _so << ") to be Non Blocking: " 
            << ::strerror( errno ) 
        << lend;
        SL_NETWORK_CLOSESOCK( _so );
        return INVALIDATE_SOCKET;
    }

    // Bind the default handler set.
    sl_handler_set _hset = sl_events::empty_handler();
    _hset.on_failed = [=](sl_event e) {
        sl_socket_close(e.so);
        if ( failed ) failed(e);
    };
    _hset.on_timedout = [=](sl_event e) {
        if ( timedout ) timedout(e);
        else sl_socket_close(e.so);
    };
    sl_events::server().bind(_so, move(_hset));

    // Add A Write Buffer
    sl_write_info _wi = { 
        make_shared<mutex>(), 
        make_shared< queue<sl_shared_write_packet_t> >() 
    };
    do {
        lock_guard<mutex> _(_g_so_write_mutex);
        _g_so_write_map[_so] = _wi;
    } while(false);
    return _so;
}

// Internal async connect to the peer
void _raw_internal_tcp_socket_connect(
    const sl_peerinfo& peer,
    uint32_t timedout,
    sl_socket_event_handler callback
)
{
    auto _cb = [=](sl_event e) {
        // By default, close the timed out socket when connecting failed.
        if ( e.event == SL_EVENT_TIMEOUT ) {
            sl_socket_close(e.so);
        }
        if ( callback ) callback(e);
    };
    SOCKET_T _tso = _raw_internal_tcp_socket_init(_cb, _cb);
    if ( SOCKET_NOT_VALIDATE(_tso) ) {
        sl_event _e;
        _e.so = _tso;
        _e.event = SL_EVENT_FAILED;
        callback(_e);
        return;
    }

    struct sockaddr_in _sock_addr;
    memset(&_sock_addr, 0, sizeof(_sock_addr));
    _sock_addr.sin_addr.s_addr = peer.ipaddress;
    _sock_addr.sin_family = AF_INET;
    _sock_addr.sin_port = htons(peer.port_number);

    if ( ::connect( 
        _tso, 
        (struct sockaddr *)&_sock_addr, 
        sizeof(_sock_addr)) == -1 ) 
    {
        int _error = 0, _len = sizeof(_error);
        getsockopt( 
            _tso, SOL_SOCKET, 
            SO_ERROR, (char *)&_error, 
            (socklen_t *)&_len);
        if ( _error != 0 ) {
            lerror 
                << "failed to connect to " 
                << peer << " on tcp socket: "
                << _tso << ", " << ::strerror( _error ) 
            << lend;
        } else {
            // Monitor the socket, the poller will invoke on_connect 
            // when the socket is connected or failed.
            //ldebug << "monitor tcp socket " << tso << 
            //  " for connecting" << lend;
            sl_events::server().monitor(
                _tso, SL_EVENT_CONNECT, 
                callback, timedout);
        }
    } else {
        // Add to next run loop to process the connect event.
        linfo 
            << "connect to " << peer 
            << " is too fast, the connect method return success directly" 
        << lend;
        sl_events::server().add_tcpevent(_tso, SL_EVENT_CONNECT);
    }
}

// Internal Connecton Method, Try to connect to peer with an IP list.
void _raw_internal_tcp_socket_try_connect(
    const vector<sl_peerinfo>& peer_list, 
    uint32_t index,
    uint32_t timedout,
    sl_socket_event_handler callback
)
{
    if ( peer_list.size() <= index ) {
        if ( !callback ) return;

        sl_event _e;
        _e.event = SL_EVENT_FAILED;
        if ( callback ) callback(_e);

        return;
    }
    _raw_internal_tcp_socket_connect(
        peer_list[index],
        timedout,
        [=](sl_event e) {
            if ( e.event == SL_EVENT_CONNECT ) {
                if ( callback ) callback(e);
                return;
            }
            // Try to invoke the next ip in the list.
            _raw_internal_tcp_socket_try_connect(
                peer_list, index + 1, 
                timedout, callback);
        }
    );
}

/*
    Async connect to the host via a socks5 proxy

    @Description
    Connect to host:port via a socks5 proxy.
    If the socks5 proxy is not set(like sl_peerinfo::nan()), will try to
    connect to the host in directly connection.
    If the host is not an sl_ip, then will invoke <sl_async_gethostname>
    to resolve the host first.

    If the host is connected syncized, this method will add a SL_EVENT_CONNECT
    to the events runloop and the caller will be noticed at the next
    timepiece.

    The default timeout time is 30 seconds(30000ms).
*/
void sl_tcp_socket_connect(
    const sl_peerinfo& socks5, 
    const string& host, 
    uint16_t port,
    uint32_t timedout,
    sl_socket_event_handler callback
)
{
    shared_ptr<sl_peerinfo> _psocks5 = make_shared<sl_peerinfo>(socks5);
    if ( socks5 ) {
        //ldebug << "try to connect to " << host << ":" << port << " via socks proxy " << socks5 << lend;
        _raw_internal_tcp_socket_connect(socks5, timedout, [=](sl_event e) {
            if ( e.event != SL_EVENT_CONNECT ) {
                lerror << "the socks5 proxy " << *_psocks5 << " cannot be connected" << lend;
                if ( callback ) callback(e); 
                return;
            }

            //ldebug << "did build a connection to the socks proxy on socket " << e.so << lend;

            sl_socket_bind_event_failed(e.so, [=](sl_event e) {
                lerror << "failed to connect to socks5 proxy" << lend;
                if ( callback ) callback(e);
            });
            sl_socket_bind_event_timeout(e.so, [=](sl_event e) {
                lerror << "connect to socks5 proxy timedout" << lend;
                sl_socket_close(e.so);
                if ( callback ) callback(e);
            });

            sl_socks5_noauth_request _req;
            // Exchange version info
            if (write(e.so, (char *)&_req, sizeof(_req)) < 0) {
                e.event = SL_EVENT_FAILED; 
                if ( callback ) callback(e); 
                return;
            }
            //ldebug << "did send version checking to proxy" << lend;
            sl_socket_monitor(e.so, timedout, [=](sl_event e){
                if ( e.event != SL_EVENT_DATA ) {
                    if ( callback ) callback(e);
                    return;
                }

                //ldebug << "proxy response for the version checking" << lend;

                string _pkt;
                if ( !sl_tcp_socket_read(e.so, _pkt) ) {
                    e.event = SL_EVENT_FAILED; 
                    if ( callback ) callback(e);
                    return;
                }
                const sl_socks5_handshake_response* _resp = (const sl_socks5_handshake_response *)_pkt.c_str();
                // This api is for no-auth proxy
                if ( _resp->ver != 0x05 && _resp->method != sl_method_noauth ) {
                    lerror << "unsupported authentication method" << lend;
                    e.event = SL_EVENT_FAILED;
                    if ( callback ) callback(e);
                    return;
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

                //ldebug << "did send connection request to the proxy" << lend;
                // Wait for the socks5 server's response
                sl_socket_monitor(e.so, timedout, [=](sl_event e) {
                    if ( e.event != SL_EVENT_DATA ) {
                        if ( callback ) callback(e); 
                        return;
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
                    string _pkt;
                    if (!sl_tcp_socket_read(e.so, _pkt)) {
                        e.event = SL_EVENT_FAILED; 
                        if ( callback ) callback(e); 
                        return;
                    }
                    const sl_socks5_ipv4_response* _resp = (const sl_socks5_ipv4_response *)_pkt.c_str();

                    /* Check the server's version. */
                    if ( _resp->ver != 0x05 ) {
                        lerror << "Unsupported SOCKS version: " << _resp->ver << lend;
                        e.event = SL_EVENT_FAILED;
                        if ( callback ) callback(e); 
                        return;
                    }
                    if (_resp->rep != sl_socks5rep_successed) {
                        lerror << sl_socks5msg((sl_socks5rep)_resp->rep) << lend;
                        e.event = SL_EVENT_FAILED;
                        if ( callback ) callback(e); 
                        return;
                    }

                    /* Check ATYP */
                    if ( _resp->atyp != sl_socks5atyp_ipv4 ) {
                        lerror << "ssh-socks5-proxy: Address type not supported: " << _resp->atyp << lend;
                        e.event = SL_EVENT_FAILED;
                        if ( callback ) callback(e); 
                        return;
                    }
                    //ldebug << "now we build the connection to the peer server via current proxy" << lend;
                    e.event = SL_EVENT_CONNECT;
                    if ( callback ) callback(e);
                });
            });
        });
    } else {
        //ldebug << "the socks5 is empty, try to connect to host(" << host << ") directly" << lend;
        sl_ip _host_ip(host);
        if ( (uint32_t)_host_ip == (uint32_t)-1 ) {
            //ldebug << "the host(" << host << ") is not an IP address, try to resolve first" << lend;
            // This is a domain
            sl_async_gethostname(host, [=](const vector<sl_ip> &iplist){
                if ( iplist.size() == 0 || ((uint32_t)iplist[0] == (uint32_t)-1) ) {
                    // Error
                    lerror << "failed to resolv " << host << lend;
                    sl_event _e;
                    _e.event = SL_EVENT_FAILED;
                    callback(_e);
                } else {
                    //ldebug << "resolvd the host " << host << ", trying to connect via tcp socket" << lend;
                    vector<sl_peerinfo> _peerlist;
                    for ( auto & _ip : iplist ) {
                        _peerlist.push_back(sl_peerinfo((const string &)_ip, port));
                    }
                    _raw_internal_tcp_socket_try_connect(_peerlist, 0, timedout, callback);
                }
            });
        } else {
            _raw_internal_tcp_socket_connect(sl_peerinfo(host, port), timedout, callback);
        }
    }
}

// Internal write method of a tcp socket
void _raw_internal_tcp_socket_write(sl_event e) 
{
    sl_write_info _wi;
    do {
        lock_guard<mutex> _(_g_so_write_mutex);
        auto _wiit = _g_so_write_map.find(e.so);
        if ( _wiit == _g_so_write_map.end() ) return;
        _wi = _wiit->second;
    } while( false );

    sl_shared_write_packet_t _sswpkt;
    do {
        lock_guard<mutex> _(*_wi.locker);
        assert(_wi.packet_queue->size() > 0);
        _sswpkt = _wi.packet_queue->front();
    } while( false );

    //ldebug << "will send data(l:" << _sswpkt->packet.size() << ") to socket " << e.so << ", write mem: " << _wmem << lend;
    while ( _sswpkt->sent_size < _sswpkt->packet.size() ) {
        int _retval = ::send(e.so, 
            _sswpkt->packet.c_str() + _sswpkt->sent_size, 
            (_sswpkt->packet.size() - _sswpkt->sent_size), 
            0 | SL_NETWORK_NOSIGNAL);
        //ldebug << "send return value: " << _retval << lend;
        if ( _retval < 0 ) {
            if ( ENOBUFS == errno || EAGAIN == errno || EWOULDBLOCK == errno ) {
                // No buf
                break;
            } else {
                lerror
                    << "failed to send data on tcp socket: " << e.so 
                    << ", err(" << errno << "): " << ::strerror(errno) << lend;
                e.event = SL_EVENT_FAILED;
                if ( _sswpkt->callback ) _sswpkt->callback(e);
                return;
            }
        } else if ( _retval == 0 ) {
            // No buf? sent 0
            break;
        } else {
            _sswpkt->sent_size += _retval;
        }
    }
    // ldebug << "sent data size " << _sswpkt->sent_size << " to socket " << e.so << lend;

    // Check if has pending data
    do {
        lock_guard<mutex> _(*_wi.locker);
        if ( _sswpkt->sent_size == _sswpkt->packet.size() ) {
            _wi.packet_queue->pop();
        }
        if ( _wi.packet_queue->size() == 0 ) break;

        // Remonitor
        sl_events::server().monitor(e.so, SL_EVENT_WRITE, _raw_internal_tcp_socket_write);
    } while ( false );

    if ( _sswpkt->callback ) _sswpkt->callback(e);
}

/*
    Async send a packet to the peer via current socket.

    @Description
    This method will append the packet to the write queue of the socket,
    then check if current socket is writing or not.
    If is now writing, the method will return directly. Otherwise,
    this method will make the socket to monitor SL_EVENT_WRITE.

    In Linux, this method will always monitor both EPOLLIN and EPOLLOUT
*/
void sl_tcp_socket_send(
    SOCKET_T tso, 
    const string &pkt, 
    sl_socket_event_handler callback
)
{
    if ( pkt.size() == 0 ) return;
    if ( SOCKET_NOT_VALIDATE(tso) ) return;

    //_g_so_write_map
    sl_write_info _wi;
    do {
        lock_guard<mutex> _(_g_so_write_mutex);
        auto _wiit = _g_so_write_map.find(tso);
        if ( _wiit == _g_so_write_map.end() ) return;
        _wi = _wiit->second;
    } while( false );

    // Create the new write packet
    shared_ptr<sl_write_packet> _wpkt = make_shared<sl_write_packet>();
    //_wpkt->packet.swap(pkt);
    _wpkt->packet = move(pkt);
    _wpkt->sent_size = 0;
    _wpkt->callback = move(callback);

    do {
        // Lock the write queue
        lock_guard<mutex> _(*_wi.locker);
        _wi.packet_queue->emplace(_wpkt);

        // Just push the packet to the end of the queue
        if ( _wi.packet_queue->size() > 1 ) return;

        // Do monitor
        sl_events::server().monitor(tso, SL_EVENT_WRITE, _raw_internal_tcp_socket_write);
    } while ( false );
}

/*
    Read incoming data from the socket.

    @Description
    This is a block method to read data from the socket.
    
    The socket must be NON_BLOCKING. This method will use a loop
    to fetch all data on the socket till two conditions:
    1. the buffer is not full after current recv action
    2. receive a EAGAIN or EWOULDBLOCK signal

    The method will increase the buffer's size after each loop 
    until reach the max size of string, which should be the size
    of machine memory in default.
*/
bool sl_tcp_socket_read(
    SOCKET_T tso, 
    string& buffer, 
    size_t min_buffer_size
)
{
    if ( SOCKET_NOT_VALIDATE(tso) ) return false;
    
    // Socket must be nonblocking
    buffer.clear();
    buffer.resize(min_buffer_size);
    size_t _received = 0;
    size_t _leftspace = min_buffer_size;

    do {
        int _retCode = ::recv(tso, &buffer[0] + _received, _leftspace, 0 );
        if ( _retCode < 0 ) {
            if ( errno == EINTR ) continue;    // signal 7, retry
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // No more data on a non-blocking socket
                buffer.resize(_received);
                return true;
            }
            // Other error
            buffer.resize(0);
            lerror << "failed to receive data on tcp socket: " << tso << ", " << ::strerror( errno ) << lend;
            return false;
        } else if ( _retCode == 0 ) {
            // Peer Close
            buffer.resize(0);
            lerror << "the peer has close the socket, recv 0" << lend;
            return false;
        } else {
            _received += _retCode;
            _leftspace -= _retCode;
            if ( _leftspace > 0 ) {
                // Unfull
                buffer.resize(_received);
                return true;
            } else {
                // The buffer is full, try to double the buffer and try again
                if ( min_buffer_size * 2 <= buffer.max_size() ) {
                    min_buffer_size *= 2;
                } else if ( min_buffer_size < buffer.max_size() ) {
                    min_buffer_size = buffer.max_size();
                } else {
                    return true;    // direct return, wait for next read.
                }
                // Resize the buffer and try to read again
                _leftspace = min_buffer_size - _received;
                buffer.resize(min_buffer_size);
            }
        }
    } while ( true );
    return true;
}
/*
    Listen on a tcp port

    @Description
    Listen on a specified tcp port on sepcified interface.
    The bind_port is the listen port info of the method.
    If you want to listen on port 4040 on all interface, set 
    <bind_port> as "0.0.0.0:4040" or sl_peerinfo(INADDR_ANY, 4040).
    If you want to listen only the internal network, like 192.168.1.0/24
    set the <bind_port> like "192.168.1.1:4040"

    The accept callback will return a new incoming socket, which
    has not been monited on any event.
*/
SOCKET_T sl_tcp_socket_listen(
    const sl_peerinfo& bind_port, 
    sl_socket_event_handler accept_callback
)
{
    SOCKET_T tso = _raw_internal_tcp_socket_init();
    if ( SOCKET_NOT_VALIDATE(tso) ) return INVALIDATE_SOCKET;

    // Bind the socket
    struct sockaddr_in _sock_addr;
    memset((char *)&_sock_addr, 0, sizeof(_sock_addr));
    _sock_addr.sin_family = AF_INET;
    _sock_addr.sin_port = htons(bind_port.port_number);
    _sock_addr.sin_addr.s_addr = bind_port.ipaddress;

    sl_events::server().update_handler(tso, SL_EVENT_ACCEPT, [=](sl_event e) {
        SOCKET_T _so = _raw_internal_tcp_socket_init(NULL, NULL, e.so);
        if ( SOCKET_NOT_VALIDATE(_so) ) {
            lerror << "failed to initialize the incoming socket " << e.so << lend;
            sl_socket_close(e.so);
            return;
        }
        accept_callback(e);
    });

    if ( ::bind(tso, (struct sockaddr *)&_sock_addr, sizeof(_sock_addr)) == -1 ) {
        lerror << "failed to listen tcp on " << bind_port << ": " << ::strerror( errno ) << lend;
        sl_socket_close(tso);
        return INVALIDATE_SOCKET;
    }
    if ( -1 == ::listen(tso, 1024) ) {
        lerror << "failed to listen tcp on " << bind_port << ": " << ::strerror( errno ) << lend;
        sl_socket_close(tso);
        return INVALIDATE_SOCKET;
    }
    linfo << "start to listening tcp on " << bind_port << lend;
    if ( !sl_poller::server().bind_tcp_server(tso) ) {
        sl_socket_close(tso);
        return INVALIDATE_SOCKET;
    }
    return tso;
}

/*
    Get original peer info of a socket.

    @Description
    This method will return the original connect peerinfo of a socket
    in Linux with iptables redirect by fetch the info with SO_ORIGINAL_DST
    flag.

    In a BSD(like Mac OS X), will return 0.0.0.0:0
*/
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
/*
    Initialize a UDP socket

    @Description
    This method will create a UDP socket and bind to the <bind_addr>
    The ipaddress in bind_addr should always be INADDR_ANY.

    As the UDP socket is connectionless, if you want to receive any
    data on specified port, you must set the port at this time.

    In order to get the local infomation of the udp socket,
    the method will bind port 0 to this socket in default.
*/
SOCKET_T sl_udp_socket_init(
    const sl_peerinfo& bind_addr,
    sl_socket_event_handler failed, 
    sl_socket_event_handler timedout
)
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

    // Bind the default handler set.
    sl_handler_set _hset = sl_events::empty_handler();
    _hset.on_failed = [failed](sl_event e) {
        sl_socket_close(e.so);
        if ( failed ) failed(e);
    };
    _hset.on_timedout = [timedout](sl_event e) {
        if ( timedout ) timedout(e);
        else sl_socket_close(e.so);
    };
    sl_events::server().bind(_so, move(_hset));

    // Add A Write Buffer
    sl_write_info _wi = { make_shared<mutex>(), make_shared< queue<sl_shared_write_packet_t> >() };
    do {
        lock_guard<mutex> _(_g_so_write_mutex);
        _g_so_write_map[_so] = _wi;
    } while(false);

    return _so;
}
// Internal write method of a udp socket
void _raw_internal_udp_socket_write(sl_event e) 
{
    sl_write_info _wi;
    do {
        lock_guard<mutex> _(_g_so_write_mutex);
        auto _wiit = _g_so_write_map.find(e.so);
        if ( _wiit == _g_so_write_map.end() ) return;
        _wi = _wiit->second;
    } while( false );

    sl_shared_write_packet_t _sswpkt;
    do {
        lock_guard<mutex> _(*_wi.locker);
        assert(_wi.packet_queue->size() > 0);
        _sswpkt = _wi.packet_queue->front();
    } while( false );

    struct sockaddr_in _sock_addr = {};
    _sock_addr.sin_family = AF_INET;
    _sock_addr.sin_port = htons(_sswpkt->peerinfo.port_number);
    _sock_addr.sin_addr.s_addr = (uint32_t)_sswpkt->peerinfo.ipaddress;

    while ( _sswpkt->sent_size < _sswpkt->packet.size() ) {
        int _retval = ::sendto(e.so, 
            _sswpkt->packet.c_str() + _sswpkt->sent_size, 
            (_sswpkt->packet.size() - _sswpkt->sent_size), 
            0 | SL_NETWORK_NOSIGNAL, 
            (struct sockaddr *)&_sock_addr, sizeof(_sock_addr));
        //ldebug << "send return value: " << _retval << lend;
        if ( _retval < 0 ) {
            if ( ENOBUFS == errno || EAGAIN == errno || EWOULDBLOCK == errno ) {
                // No buf
                break;
            } else {
                lerror
                    << "failed to send data on udp socket: " << e.so 
                    << ", err(" << errno << "): " << ::strerror(errno) << lend;
                e.event = SL_EVENT_FAILED;
                if ( _sswpkt->callback ) _sswpkt->callback(e);
                return;
            }
        } else if ( _retval == 0 ) {
            // No buf? sent 0
            break;
        } else {
            _sswpkt->sent_size += _retval;
        }
    }

    // Check if has pending data
    do {
        lock_guard<mutex> _(*_wi.locker);
        if ( _sswpkt->sent_size == _sswpkt->packet.size() ) {
            _wi.packet_queue->pop();
        }
        if ( _wi.packet_queue->size() == 0 ) break;

        // Remonitor
        sl_events::server().monitor(e.so, SL_EVENT_WRITE, _raw_internal_udp_socket_write);
    } while ( false );

    if ( _sswpkt->callback ) _sswpkt->callback(e);
}

/*
    Send packet to the peer.

    @Description
    This method is an async send method.
    It will push the packet to the end of the write queue and 
    try to monitor the SL_EVENT_WRITE flag of the socket.
*/
void sl_udp_socket_send(
    SOCKET_T uso,
    const sl_peerinfo& peer,
    const string &pkt,
    sl_socket_event_handler callback
)
{
    if ( pkt.size() == 0 ) return;
    if ( SOCKET_NOT_VALIDATE(uso) ) return;

    //_g_so_write_map
    sl_write_info _wi;
    do {
        lock_guard<mutex> _(_g_so_write_mutex);
        auto _wiit = _g_so_write_map.find(uso);
        if ( _wiit == _g_so_write_map.end() ) return;
        _wi = _wiit->second;
    } while( false );

    // Create the new write packet
    shared_ptr<sl_write_packet> _wpkt = make_shared<sl_write_packet>();
    //_wpkt->packet.swap(pkt);
    _wpkt->packet = move(pkt);
    _wpkt->sent_size = 0;
    _wpkt->peerinfo = peer;
    _wpkt->callback = move(callback);

    do {
        // Lock the write queue
        lock_guard<mutex> _(*_wi.locker);
        _wi.packet_queue->emplace(_wpkt);

        // Just push the packet to the end of the queue
        if ( _wi.packet_queue->size() > 1 ) return;

        // Do monitor
        sl_events::server().monitor(uso, SL_EVENT_WRITE, _raw_internal_udp_socket_write);
    } while ( false );
}

/*
    Block and read data from the UDP socket.

    @Description
    Same as tcp socket read method.
*/
bool sl_udp_socket_read(
    SOCKET_T uso, 
    struct sockaddr_in addr, 
    string& buffer, 
    size_t min_buffer_size
)
{
    if ( SOCKET_NOT_VALIDATE(uso) ) return false;

    sl_peerinfo _pi(addr.sin_addr.s_addr, ntohs(addr.sin_port));

    // Socket must be nonblocking
    buffer.clear();
    buffer.resize(min_buffer_size);
    size_t _received = 0;
    size_t _leftspace = min_buffer_size;

    do {
        unsigned _so_len = sizeof(addr);
        int _retCode = ::recvfrom( uso, &buffer[0], min_buffer_size, 0,
            (struct sockaddr *)&addr, &_so_len);
        if ( _retCode < 0 ) {
            if ( errno == EINTR ) continue;    // signal 7, retry
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // No more data on a non-blocking socket
                buffer.resize(_received);
                return true;
            }
            // Other error
            buffer.resize(0);
            lerror << "failed to receive data on udp socket: " << uso << "(" << _pi << "), " << ::strerror( errno ) << lend;
            return false;
        } else if ( _retCode == 0 ) {
            // Peer Close
            buffer.resize(0);
            lerror << "the peer has close the socket, recv 0" << lend;
            return false;
        } else {
            _received += _retCode;
            _leftspace -= _retCode;
            if ( _leftspace > 0 ) {
                // Unfull
                buffer.resize(_received);
                return true;
            } else {
                // The buffer is full, try to double the buffer and try again
                if ( min_buffer_size * 2 <= buffer.max_size() ) {
                    min_buffer_size *= 2;
                } else if ( min_buffer_size < buffer.max_size() ) {
                    min_buffer_size = buffer.max_size();
                } else {
                    return true;    // direct return, wait for next read.
                }
                // Resize the buffer and try to read again
                _leftspace = min_buffer_size - _received;
                buffer.resize(min_buffer_size);
            }
        }
    } while ( true );
    return true;
}

/*
    Listen on a UDP port and wait for any incoming data.

    @Description
    As a UDP socket is connectionless, the only different between
    listen and monitor is 'listen' will auto re-monitor the socket
    after a data incoming message has been processed.
*/
void sl_udp_socket_listen(
    SOCKET_T uso, 
    sl_socket_event_handler accept_callback
)
{
    if ( SOCKET_NOT_VALIDATE(uso) ) return;
    sl_socket_monitor(uso, 0, [=](sl_event e) {
        if ( accept_callback ) accept_callback(e);
        sl_udp_socket_listen(e.so, accept_callback);
    });
    // uint32_t _port;
    // network_sock_info_from_socket(uso, _port);
    // linfo << "start to listening udp on " << _port << lend;
}

// Global DNS Server List
vector<sl_peerinfo> _resolv_list;

void _raw_internal_async_gethostname_udp(
    const string&& query_pkt,
    const vector<sl_peerinfo>&& resolv_list,
    size_t use_index,
    async_dns_handler fp
);

void _raw_internal_async_gethostname_tcp(
    const string&& query_pkt,
    const vector<sl_peerinfo>&& resolv_list,
    size_t use_index,
    const sl_peerinfo& socks5,
    async_dns_handler fp
);

void _raw_internal_async_gethostname_udp(
    const string&& query_pkt,
    const vector<sl_peerinfo>&& resolv_list,
    size_t use_index,
    async_dns_handler fp
)
{
    // No other validate resolve ip in the list, return the 255.255.255.255
    if ( resolv_list.size() == use_index ) {
        lwarning << "no more nameserver validated" << lend;
        fp( {sl_ip((uint32_t)-1)} );
        return;
    }

    // Create a new udp socket and send the query packet.
    auto _errorfp = [=](sl_event e) {
        // Assert the event status
        assert(e.event == SL_EVENT_FAILED || e.event == SL_EVENT_TIMEOUT);

        // Go next server
        _raw_internal_async_gethostname_udp(
            move(query_pkt), 
            move(resolv_list), 
            use_index + 1,
            fp
        );
    };

    SOCKET_T _uso = sl_udp_socket_init(sl_peerinfo::nan(), _errorfp, _errorfp);

    if ( SOCKET_NOT_VALIDATE(_uso) ) {
        _errorfp(sl_event_make_failed());
        return;
    }

    sl_udp_socket_send(_uso, resolv_list[use_index], query_pkt, [=](sl_event e) {
        sl_socket_monitor(e.so, 1, [=](sl_event e){
            // Read the incoming packet
            string _incoming_pkt;
            bool _ret = sl_udp_socket_read(e.so, e.address, _incoming_pkt);
            // After reading, whether success or not, close the socket first.
            sl_socket_close(e.so);
            if ( !_ret ) {
                // On Failed, go next
                e.event = SL_EVENT_FAILED;
                _errorfp(e);
                return;
            }

            const clnd_dns_packet *_pheader = (const clnd_dns_packet *)_incoming_pkt.c_str();
            if ( _pheader->get_resp_code() == dns_rcode_noerr ) {
                vector<uint32_t> _a_recs;
                string _qdomain;
                dns_get_a_records(_incoming_pkt.c_str(), _incoming_pkt.size(), _qdomain, _a_recs);
                vector<sl_ip> _retval;
                for ( auto _a : _a_recs ) {
                    _retval.push_back(sl_ip(_a));
                }
                fp( _retval );
            } else if ( _pheader->get_is_truncation() ) {
                // TRUNC flag get, try to use tcp
                _raw_internal_async_gethostname_tcp(
                    move(query_pkt), move(resolv_list), use_index, sl_peerinfo::nan(), fp
                );
            } else {
                // Other error, try next
                _raw_internal_async_gethostname_udp(
                    move(query_pkt), move(resolv_list), use_index + 1, fp
                );
            }
        });
    });
}

void _raw_internal_async_gethostname_tcp(
    const string&& query_pkt,
    const vector<sl_peerinfo>&& resolv_list,
    size_t use_index,
    const sl_peerinfo& socks5,
    async_dns_handler fp
)
{
    // No other validate resolve ip in the list, return the 255.255.255.255
    if ( resolv_list.size() == use_index ) {
        lwarning << "no more nameserver validated" << lend;
        fp( {sl_ip((uint32_t)-1)} );
        return;
    }

    // Create a new udp socket and send the query packet.
    auto _errorfp = [=](sl_event e) {
        // Assert the event status
        assert(e.event == SL_EVENT_FAILED || e.event == SL_EVENT_TIMEOUT);

        // Go next server
        if ( socks5 ) {
            _raw_internal_async_gethostname_tcp(
                move(query_pkt), move(resolv_list), use_index + 1, socks5, fp
            );
        } else {
            _raw_internal_async_gethostname_udp(
                move(query_pkt), move(resolv_list), use_index + 1, fp
            );
        }
    };

    sl_peerinfo _resolv_peer = move(resolv_list[use_index]);

    sl_tcp_socket_connect(socks5, _resolv_peer.ipaddress, _resolv_peer.port_number, 3, [=](sl_event e) {
        if ( e.event != SL_EVENT_CONNECT ) {
            _errorfp(e);
            return;
        }

        // Append error handler to the socket's handler set
        sl_events::server().append_handler(e.so, SL_EVENT_FAILED, _errorfp);
        sl_events::server().append_handler(e.so, SL_EVENT_TIMEOUT, [=](sl_event e){
            sl_socket_close(e.so);
            _errorfp(e);
        });

        // Create the TCP redirect packet
        string _tpkt;
        dns_generate_tcp_redirect_packet(move(query_pkt), _tpkt);

        sl_tcp_socket_send(e.so, _tpkt, [=](sl_event e){
            sl_socket_monitor(e.so, 1, [=](sl_event e){
                // Read incoming
                string _tcp_incoming_pkt;
                bool _ret = sl_tcp_socket_read(e.so, _tcp_incoming_pkt);
                // After reading, whether success or not, close the socket first.
                sl_socket_close(e.so);
                if ( !_ret ) {
                    // On Failed, go next
                    e.event = SL_EVENT_FAILED;
                    _errorfp(e);
                    return;
                }
                string _incoming_pkt;
                dns_generate_udp_response_packet_from_tcp(_tcp_incoming_pkt, _incoming_pkt);

                const clnd_dns_packet *_pheader = (const clnd_dns_packet *)_incoming_pkt.c_str();
                if ( _pheader->get_resp_code() == dns_rcode_noerr ) {
                    vector<uint32_t> _a_recs;
                    string _qdomain;
                    dns_get_a_records(_incoming_pkt.c_str(), _incoming_pkt.size(), _qdomain, _a_recs);
                    vector<sl_ip> _retval;
                    for ( auto _a : _a_recs ) {
                        _retval.push_back(sl_ip(_a));
                    }
                    fp( _retval );
                } else {
                    // Failed to get the dns result
                    e.event = SL_EVENT_FAILED;
                    _errorfp(e);
                }
            });
        });
    });
}
/*!
    Try to get the dns result async
    @Description
    Use async udp/tcp socket to send a dns query request to the domain name server.
    If has multiple nameserver set in the system, will try all the sever in order
    till the first have a no-error response.
    The method will use a UDP socket at first, if the answer is a TC package, then
    force to send TCP request to the same server again.
    If the server does not response after timeout(5s), will try to use the next
    server in the list.
    If all server failed to answer the query, then will return 255.255.255.255 
    as the IP address of the host to query in the result.

    This method will always return an IP address.
*/
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
            // ldebug << "resolv get dns: " << _pi << lend;
        }
    }
    string _qpkt;
    dns_generate_query_packet(host, _qpkt);
    _raw_internal_async_gethostname_udp(move(_qpkt), move(_resolv_list), 0, fp);
}
/*
    Try to get the dns result async via specified name servers
*/
void sl_async_gethostname(
    const string& host, 
    const vector<sl_peerinfo>& nameserver_list, 
    async_dns_handler fp
)
{
    string _qpkt;
    dns_generate_query_packet(host, _qpkt);
    _raw_internal_async_gethostname_udp(move(_qpkt), move(nameserver_list), 0, fp);
}

/*
    Try to get the dns result via specified name servers through a socks5 proxy.
    THis will force to use tcp connection to the nameserver
*/
void sl_async_gethostname(
    const string& host, 
    const vector<sl_peerinfo>& nameserver_list, 
    const sl_peerinfo &socks5, 
    async_dns_handler fp
)
{
    string _qpkt;
    dns_generate_query_packet(host, _qpkt);
    if ( socks5 ) {
        _raw_internal_async_gethostname_tcp(move(_qpkt), move(nameserver_list), 0, socks5, fp);
    } else {
        _raw_internal_async_gethostname_udp(move(_qpkt), move(nameserver_list), 0, fp);
    }
}
/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
