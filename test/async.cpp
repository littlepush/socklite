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
        sl_dns_packet _dpkt(_dnspkt);

        sl_peerinfo _pi(e.address.sin_addr.s_addr, ntohs(e.address.sin_port));
        linfo << "get request from " << _pi << " to query domain " << _dpkt.get_query_domain() << lend;
        sl_async_gethostname(_dpkt.get_query_domain(), [=](const vector<sl_ip> & iplist){
            sl_dns_packet _rpkt(_dpkt);
            _rpkt.set_A_records(iplist);
            sl_udp_socket_send(e.so, _pi, _rpkt);
        });
    });

    SOCKET_T _upso = sl_udp_socket_init(sl_peerinfo(INADDR_ANY, 2001));
    sl_udp_socket_listen(_upso, [](sl_event e) {
        string _dnspkt;
        sl_udp_socket_read(e.so, e.address, _dnspkt);
        sl_dns_packet _dpkt(_dnspkt);
        sl_peerinfo _pi(e.address.sin_addr.s_addr, ntohs(e.address.sin_port));
        linfo << "get request from " << _pi << " to query domain " << _dpkt.get_query_domain() << lend;
        sl_async_gethostname(
            _dpkt.get_query_domain(), 
            {sl_peerinfo("8.8.8.8:53"), sl_peerinfo("8.8.4.4:53")},
            _socks5,
            [=](const vector<sl_ip> & iplist){
                sl_dns_packet _rpkt(_dpkt);
                _rpkt.set_A_records(iplist);
                //sl_udp_socket_send(e.so, _resp, _pi);
                sl_udp_socket_send(e.so, _pi, _rpkt);
            }
        );
    });

    SOCKET_T _urso = sl_udp_socket_init(sl_peerinfo(INADDR_ANY, 2002));
    sl_udp_socket_listen(_urso, [](sl_event e) {
        string _dnspkt;
        sl_udp_socket_read(e.so, e.address, _dnspkt);
        sl_dns_packet _dpkt(_dnspkt);
        sl_async_redirect_dns_query(_dpkt, sl_peerinfo("119.29.29.29:53"), sl_peerinfo::nan(), [=](const sl_dns_packet& rpkt) {
            dump_iplist(_dpkt.get_query_domain(), rpkt.get_A_records());
            sl_peerinfo _pi(e.address.sin_addr.s_addr, ntohs(e.address.sin_port));
            sl_udp_socket_send(e.so, _pi, rpkt);
        });
    });
    sl_tcp_socket_listen(sl_peerinfo(INADDR_ANY, 58423), [](sl_event e){
        sl_tcp_socket_redirect(e.so, sl_peerinfo("10.15.11.1:38422"), sl_peerinfo::nan());
    });
    return 0;
}

// sock.lite.async.cpp

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
