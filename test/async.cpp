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

void dump_iplist(const string& domain, const vector<sl_ip> & iplist) {
    for ( auto ip : iplist ) {
        linfo << domain << " get A record: " << ip << lend;
    }
}
/*
void tcp_redirect_callback(sl_event e, sl_event re) {
    if ( e.event == SL_EVENT_FAILED ) {
        linfo << "socket has disconnected" << lend;
        sl_socket_close(e.so);
        sl_socket_close(re.so);
        return;
    }
    string _buf;
    if ( !sl_tcp_socket_read(e.so, _buf, 512000) ) {
        sl_socket_close(e.so);
        sl_socket_close(re.so);
        return;
    }
    if ( !sl_tcp_socket_send(re.so, _buf, [e](sl_event re){
        if ( re.event == SL_EVENT_FAILED ) {
            sl_socket_close(e.so);
            sl_socket_close(re.so);
            return;
        }
        if ( !sl_tcp_socket_monitor(e.so, [re](sl_event e){
            tcp_redirect_callback(e, re);
        }) ) {
            sl_socket_close(e.so);
            sl_socket_close(re.so);
            return;
        }
    })) {
        sl_socket_close(e.so);
        sl_socket_close(re.so);
        return;
    }
}
*/

void tcp_redirect_callback(sl_event from_event, sl_event to_event) {
    string _pkt;
    if ( !sl_tcp_socket_read(from_event.so, _pkt) ) {
        sl_events::server().add_tcpevent(from_event.so, SL_EVENT_FAILED);
        return;
    }
    ldebug << "we get incoming data from socket " << from_event.so << ", will send to socket " << to_event.so << lend;
    sl_tcp_socket_send(to_event.so, _pkt, [from_event](sl_event to_event) {
        ldebug << "the data has been sent from socket " << from_event.so << " to socket " << to_event.so << lend;
        ldebug << "we now going to re-monitor the socket " << from_event.so << lend;
        sl_socket_monitor(from_event.so, 30, bind(tcp_redirect_callback, placeholders::_1, to_event));
    });
}

string _socks5 = "127.0.0.1:1080";

int main( int argc, char * argv[] )
{
    if ( argc == 2 ) {
        _socks5 = argv[1];
    }

    cp_logger::start(stderr, log_debug);
    signal_agent _sa([](void){
        linfo << "the async server receive exit signal, ready to kill all working threads." << lend;
    });

    sl_iprange _range("10.15.11.0/24");
    lnotice << "IP 10.15.11.10 is in range(" << _range << "): " << _range.is_ip_in_range(sl_ip("10.15.11.10")) << lend;
    lnotice << "IP 10.15.12.1 is in range(" << _range << "): " << _range.is_ip_in_range(sl_ip("10.15.12.1")) << lend;

    sl_async_gethostname("www.tmall.com", bind(dump_iplist, "www.tmall.com", placeholders::_1));
    sl_async_gethostname("www.baidu.com", bind(dump_iplist, "www.baidu.com", placeholders::_1));
    sl_async_gethostname("www.dianping.com", bind(dump_iplist, "www.dianping.com", placeholders::_1));
    sl_async_gethostname("www.google.com", {sl_peerinfo("8.8.8.8:53")}, _socks5, bind(dump_iplist, "www.google.com", placeholders::_1));

    sl_tcp_socket_connect(sl_peerinfo::nan(), "www.baidu.com", 80, 3, [](sl_event e) {
        if ( e.event != SL_EVENT_CONNECT ) {
            lerror << "failed to connect to www.baidu.com, " << e << lend;
            return;
        }
        sl_events::server().append_handler(e.so, SL_EVENT_TIMEOUT, [](sl_event e) {
            lerror << e << lend;
            sl_socket_close(e.so);
        });

        string _http_pkt = "GET / HTTP/1.1\r\n\r\n";
        sl_tcp_socket_send(e.so, _http_pkt, [](sl_event e) {
            sl_socket_monitor(e.so, 3, [](sl_event e) {
                string _http_resp;
                sl_tcp_socket_read(e.so, _http_resp, 1024000);
                ldebug << "response size: " << _http_resp.size() << lend;
                ldebug << "HTTP RESPONSE: " << _http_resp << lend;
                sl_socket_close(e.so);
            });
        });
    });

    SOCKET_T _ulso = sl_udp_socket_init(sl_peerinfo(INADDR_ANY, 2000));
    sl_udp_socket_listen(_ulso, [](sl_event e) {
        string _dnspkt;
        sl_udp_socket_read(e.so, e.address, _dnspkt);
        string _domain;
        dns_get_domain(_dnspkt.c_str(), _dnspkt.size(), _domain);
        const clnd_dns_packet *_pkt = (const clnd_dns_packet *)_dnspkt.c_str();
        uint16_t _tid = _pkt->get_transaction_id();
        sl_peerinfo _pi(e.address.sin_addr.s_addr, ntohs(e.address.sin_port));
        linfo << "get request from " << _pi << " to query domain " << _domain << lend;
        sl_async_gethostname(_domain, [=](const vector<sl_ip> & iplist){
            string _resp;
            vector<uint32_t> _iplist;
            for ( auto ip : iplist ) {
                _iplist.push_back((uint32_t)ip);
            }
            dns_generate_a_records_resp(_domain, _tid, _iplist, _resp);
            //sl_udp_socket_send(e.so, _resp, _pi);
            sl_udp_socket_send(e.so, _pi, _resp);
        });
    });

    SOCKET_T _upso = sl_udp_socket_init(sl_peerinfo(INADDR_ANY, 2001));
    sl_udp_socket_listen(_upso, [](sl_event e) {
        string _dnspkt;
        sl_udp_socket_read(e.so, e.address, _dnspkt);
        string _domain;
        dns_get_domain(_dnspkt.c_str(), _dnspkt.size(), _domain);
        const clnd_dns_packet *_pkt = (const clnd_dns_packet *)_dnspkt.c_str();
        uint16_t _tid = _pkt->get_transaction_id();
        sl_peerinfo _pi(e.address.sin_addr.s_addr, ntohs(e.address.sin_port));
        linfo << "get request from " << _pi << " to query domain " << _domain << lend;
        sl_async_gethostname(
            _domain, 
            {sl_peerinfo("8.8.8.8:53"), sl_peerinfo("8.8.4.4:53")},
            _socks5,
            [=](const vector<sl_ip> & iplist){
                string _resp;
                vector<uint32_t> _iplist;
                for ( auto ip : iplist ) {
                    _iplist.push_back((uint32_t)ip);
                }
                dns_generate_a_records_resp(_domain, _tid, _iplist, _resp);
                //sl_udp_socket_send(e.so, _resp, _pi);
                sl_udp_socket_send(e.so, _pi, _resp);
            }
        );
    });
    sl_tcp_socket_listen(sl_peerinfo(INADDR_ANY, 58423), [](sl_event e){
        sl_tcp_socket_connect(_socks5, "106.187.97.108", 58422, 5, [e](sl_event re) {
            if ( re.event != SL_EVENT_CONNECT ) {
                sl_socket_close(e.so);
                return;
            }

            ldebug << "in the main runloop of async test code. we connected to the ssh server" << lend;
            sl_socket_bind_event_failed(e.so, [=](sl_event e) {
                sl_socket_close(re.so);
            });
            sl_socket_bind_event_timeout(e.so, [=](sl_event e) {
                ldebug << "socket " << e.so << " monitor run out of time" << lend;
                sl_socket_close(e.so);
                sl_socket_close(re.so);
            });
            sl_socket_bind_event_failed(re.so, [=](sl_event re) {
                sl_socket_close(e.so);
            });
            sl_socket_bind_event_timeout(re.so, [=](sl_event re) {
                ldebug << "socket " << re.so << " monitor run out of time" << lend;
                sl_socket_close(e.so);
                sl_socket_close(re.so);
            });

            sl_socket_monitor(e.so, 30, bind(tcp_redirect_callback, placeholders::_1, re));
            sl_socket_monitor(re.so, 30, bind(tcp_redirect_callback, placeholders::_1, e));
        });
    });
/*
    SOCKET_T _rso = sl_tcp_socket_init();
    sl_tcp_socket_listen(_rso, sl_peerinfo(INADDR_ANY, 58423), [&](sl_event e){
        SOCKET_T _redirect_so = sl_tcp_socket_init();
        ldebug << "receive a socket: " << e.so << ", create a redirect socket: " << _redirect_so << lend;
        sl_tcp_socket_connect(_redirect_so, sl_peerinfo(_socks5), "106.187.97.108", 38422, [e](sl_event re){
            if ( re.event == SL_EVENT_FAILED ) {
                lerror << "cannot connect to 106.187.97.108:38422" << lend;
                sl_socket_close(re.so);
                sl_socket_close(e.so);
                return;
            }
            ldebug << "did connect to 106.187.97.108:38422 via proxy use socket " << re.so << lend;
            if ( !sl_tcp_socket_monitor(e.so, [re](sl_event e){
                tcp_redirect_callback(e, re);
            }, true) ) {
                sl_socket_close(e.so);
                sl_socket_close(re.so);
                return;
            }
            if ( !sl_tcp_socket_monitor(re.so, [e](sl_event re){
                tcp_redirect_callback(re, e);
            }) ) {
                sl_socket_close(e.so);
                sl_socket_close(re.so);
                return;
            }
        }) ? [](){
            linfo << "the connect return true, wait for the next runloop to monitor the result" << lend;
        }() : [e]() {
            lerror << "failed to create a connection to the redirected server." << lend;
            sl_socket_close(e.so);
        }();
    });
*/
    return 0;
}

// sock.lite.async.cpp

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
