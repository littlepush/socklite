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

void dump_iplist(const vector<sl_ip> & iplist) {
    for ( auto ip : iplist ) {
        linfo << "get A record: " << ip << lend;
    }
}

void tcp_redirect_callback(sl_event e, sl_event re) {
    if ( e.event == SL_EVENT_FAILED ) {
        linfo << "socket has disconnected" << lend;
        sl_socket_close(e.so);
        sl_socket_close(re.so);
        return;
    }
    string _buf;
    sl_tcp_socket_read(e.so, _buf, 512000);
    sl_tcp_socket_send(re.so, _buf);
    sl_tcp_socket_monitor(e.so, [re](sl_event e) {
        tcp_redirect_callback(e, re);
    });
}

int main( int argc, char * argv[] )
{
    cp_logger::start(stderr, log_debug);
    signal_agent _sa([](void){
        linfo << "the async server receive exit signal, ready to kill all working threads." << lend;
    });

    sl_iprange _range("10.15.11.0/24");
    lnotice << "IP 10.15.11.10 is in range(" << _range << "): " << _range.is_ip_in_range(sl_ip("10.15.11.10")) << lend;
    lnotice << "IP 10.15.12.1 is in range(" << _range << "): " << _range.is_ip_in_range(sl_ip("10.15.12.1")) << lend;

    sl_async_gethostname("www.tmall.com", dump_iplist);
    sl_async_gethostname("www.baidu.com", dump_iplist);
    sl_async_gethostname("www.dianping.com", dump_iplist);

    string _test_domain = "www.baidu.com";
    SOCKET_T _tso = sl_tcp_socket_init();
    if ( SOCKET_NOT_VALIDATE(_tso) ) {
        lerror << "failed to init a tcp socket" << lend;
    } else {
        ldebug << "before connect to " << _test_domain << lend;
        sl_tcp_socket_connect(_tso, sl_peerinfo::nan(), _test_domain, 80, [_test_domain](sl_event e) {
            if ( e.event == SL_EVENT_FAILED ) {
                lerror << "failed to connect to " << _test_domain << lend;
                sl_socket_close(e.so);
                return;
            }
            ldebug << "connected to " << _test_domain << lend;
            linfo << "we now connect to " << _test_domain << lnewl << "try to send basic http request" << lend;
            string _http_pkt = "GET / HTTP/1.1\r\n\r\n";
            sl_tcp_socket_send(e.so, _http_pkt, [](sl_event e) {
                if ( e.event == SL_EVENT_FAILED ) {
                    lerror << "failed to send data to www.baidu.com" << lend;
                    sl_socket_close(e.so);
                    return;
                }
                ldebug << "did write data to www.baidu.com, wait for response" << lend;
                sl_tcp_socket_monitor(e.so, [=](sl_event e) {
                    if ( e.event == SL_EVENT_FAILED ) {
                        lerror << "no response get from the server." << lend;
                        sl_socket_close(e.so);
                        return;
                    }
                    ldebug << "did get the response, prepare for reading" << lend;
                    string _http_resp;
                    sl_tcp_socket_read(e.so, _http_resp, 1024000);
                    ldebug << "response size: " << _http_resp.size() << lend;
                    ldebug << "HTTP RESPONSE: " << _http_resp << lend;
                    dump_hex(_http_resp);
                    sl_socket_close(e.so);
                });
            });
        });     
    }

    SOCKET_T _ptso = sl_tcp_socket_init();
    if ( SOCKET_NOT_VALIDATE(_ptso) ) {
        lerror << "failed to init a socket for socks5 proxy testing" << lend;
    } else {
        string _proxy_domain = "www.google.com";
        ldebug << "before connect to " << _proxy_domain << lend;
        sl_tcp_socket_connect(_ptso, sl_peerinfo("127.0.0.1:1080"), _proxy_domain, 80, [_proxy_domain](sl_event e) {
            if ( e.event == SL_EVENT_FAILED ) {
                lerror << "failed to connect to " << _proxy_domain << " via socks5 proxy 127.0.0.1:1080" << lend;
                sl_socket_close(e.so);
                return;
            }
            linfo << "did connected to " << _proxy_domain << lend;
            sl_socket_close(e.so);
        });
    }

    SOCKET_T _sso = sl_tcp_socket_init();
    if ( SOCKET_NOT_VALIDATE(_sso) ) {
        lerror << "failed to init a socket for listening" << lend;
    } else {
        sl_tcp_socket_listen(_sso, sl_peerinfo(INADDR_ANY, 1090), [&](sl_event e) {
            sl_tcp_socket_monitor(e.so, [&](sl_event e){
                string _buf;
                sl_tcp_socket_read(e.so, _buf, 1024);
                dump_hex(_buf);
                sl_tcp_socket_send(e.so, _buf);
                sl_socket_close(e.so);
            }, true);
        });
    }

    SOCKET_T _dso = sl_udp_socket_init(sl_peerinfo(INADDR_ANY, 2000));
    if ( SOCKET_NOT_VALIDATE(_dso) ) {
        lerror << "failed to init a socket for udp listening" << lend;
    } else {
        sl_udp_socket_listen(_dso, [&](sl_event e) {
            string _dnspkt;
            sl_udp_socket_read(e.so, e.address, _dnspkt);
            string _domain;
            dns_get_domain(_dnspkt.c_str(), _dnspkt.size(), _domain);
            const clnd_dns_packet *_pkt = (const clnd_dns_packet *)_dnspkt.c_str();
            uint16_t _tid = _pkt->get_transaction_id();
            sl_peerinfo _pi(e.address.sin_addr.s_addr, ntohs(e.address.sin_port));
            linfo << "get request from " << _pi << " to query domain " << _domain << lend;
            sl_async_gethostname(_domain, [_domain, _tid, e, _pi](const vector<sl_ip> & iplist){
                string _resp;
                vector<uint32_t> _iplist;
                for ( auto ip : iplist ) {
                    _iplist.push_back((uint32_t)ip);
                }
                dns_generate_a_records_resp(_domain, _tid, _iplist, _resp);
                sl_udp_socket_send(e.so, _resp, _pi);
            });
        });
    }

    SOCKET_T _dsso = sl_udp_socket_init(sl_peerinfo(INADDR_ANY, 2001));
    if ( SOCKET_NOT_VALIDATE(_dsso) ) {
        lerror << "failed to init a socket for proxy udp dns listening" << lend;
    } else {
        sl_udp_socket_listen(_dsso, [&](sl_event e) {
            string _dnspkt;
            sl_udp_socket_read(e.so, e.address, _dnspkt);
            string _domain;
            dns_get_domain(_dnspkt.c_str(), _dnspkt.size(), _domain);
            // const clnd_dns_packet *_pkt = (const clnd_dns_packet *)_dnspkt.c_str();
            // uint16_t _tid = _pkt->get_transaction_id();
            sl_peerinfo _pi(e.address.sin_addr.s_addr, ntohs(e.address.sin_port));
            linfo << "get request from " << _pi << " to query domain " << _domain << lend;
            SOCKET_T _tso = sl_tcp_socket_init();
            sl_tcp_socket_connect(
                _tso, sl_peerinfo("127.0.0.1:1080"), "8.8.8.8", 53, 
                [e, _dnspkt, _pi](sl_event te) {
                if ( te.event == SL_EVENT_FAILED ) {
                    lerror << "failed to connect to 8.8.8.8:53 via socks5 127.0.0.1:1080" << lend;
                    sl_socket_close(te.so);
                    return;
                }
                string _tcp_dns;
                dns_generate_tcp_redirect_packet(_dnspkt, _tcp_dns);
                sl_tcp_socket_send(te.so, _tcp_dns);
                sl_tcp_socket_monitor(te.so, [e, _pi](sl_event te) {
                    if ( te.event == SL_EVENT_FAILED ) {
                        lerror << "the connection has been dropped for tcp socket: " << te.so << lend;
                        sl_socket_close(te.so);
                        return;
                    }
                    string _resp;
                    sl_tcp_socket_read(te.so, _resp);
                    // Release the tcp socket
                    sl_socket_close(te.so);

                    string _udp_resp;
                    dns_generate_udp_response_packet_from_tcp(_resp, _udp_resp);
                    sl_udp_socket_send(e.so, _udp_resp, _pi);
                });
            });
        });
    }

    SOCKET_T _rso = sl_tcp_socket_init();
    sl_tcp_socket_listen(_rso, sl_peerinfo(INADDR_ANY, 58422), [&](sl_event e){
        SOCKET_T _redirect_so = sl_tcp_socket_init();
        ldebug << "receive a socket: " << e.so << ", create a redirect socket: " << _redirect_so << lend;
        sl_tcp_socket_connect(_redirect_so, sl_peerinfo("127.0.0.1:1080"), "106.187.97.108", 38422, [e](sl_event re){
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
    return 0;
}

// sock.lite.async.cpp

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
