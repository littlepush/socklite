Sock Lite
===
This is a lite version of socket implemented in C++. The library can be used in Linux and Mac OS.

You can either use it as a shared library or just copy the code into your project.

For TCP socket, can connect to peer via a SOCKS5 proxy.

I also provide a simpale example for creating a socks5 proxy server side.

How to use the sync-api
===
```c++
#include <sl/socket.h>
#include <sl/poller.h>

void worker_listen( unsigned int port ) {
	std::map<SOCKET_T, SOCKET_T> _somap;
    // 1. Create a server socket listen to specified port
    sl_tcpsocket _server_so;
    if ( _server_so.listen( port ) == false ) {
        exit(1);
    }
    // 2. Loop to get client
	std::vector<SL_EVENT> _elist;
    while ( true ) {
		// 3. Fetch new events
		_elist.clear();
		sl_poller::server().fetch_events(_elist);
		// 4. Loop to get all events
		for ( auto &_event : _elist ) {
			if ( _event.event == SL_EVENT_FAILED ) {
				// 5. Peer close, drop connection
				close(_event.so);
				auto _pair = _somap.find(_event.so);
				if ( _pair != end(_somap) ) {
					_somap.erase(_event.so);
					_somap.erase(_pair->second);
				}
			} else if ( _event == SL_EVENT_ACCEPT ) {
				// 6. New incoming 
				sl_tcpsocket _wrap_src(_event.so);
				sl_tcpsocket _wrap_dst(true);	// this is a wrapper
				_wrap_src.set_reusable();
				_wrap_dst.set_reusable();
				// 7. Connect to peer, maybe by socks5 proxy.
				// _wrap_dst.setup_proxy( "127.0.0.1", 1080 );
				// _wrap_dst.setup_proxy( "127.0.0.1", 1080, "username", "pwd" );
				_wrap_dst.connect("my-peer-domain.com", 443);
				// 8. Add to monitor
				_somap[_wrap_src.m_socket] = _wrap_dst.m_socket;
				_somap[_wrap_dst.m_socket] = _wrap_src.m_socket;
				sl_poller::server().monitor_socket(_wrap_src.m_socket);
				sl_poller::server().monitor_socket(_wrap_dst.m_socket);
			} else {
				// 9. Data
				auto _pair = _somap.find(_event.so);
				if ( _pair != end(_somap) ) {
					sl_tcpsocket _wsrc(_event.so);
					sl_tcpsocket _wdst(_pair->second);
					// Read and write
					string _buf;
					SO_READ_STATUE _st = SO_READ_WAITING;
					while ( true ) {
						_st = _wsrc.read_data(_buf);
						if ( _st & SO_READ_DONE ) {
							_wdst.write_data(_buf);
						}
						if ( _st & SO_READ_TIMEOUT ) continue;
						break;
					}
					if ( _st & SO_READ_CLOSE ) {
						_somap.erase(_wsrc.m_socket);
						_somap.erase(_wdst.m_socket);
						_wsrc.close();
						_wdst.close();
					}
				}
			}
		}
    }
}
```

How to use the async-api:
===

The following code is a dns redirect server via a tcp socks5 proxy

```c++
SOCKET_T _dsso = sl_udp_socket_init();
if ( SOCKET_NOT_VALIDATE(_dsso) ) {
    lerror << "failed to init a socket for proxy udp dns listening" << lend;
} else {
    sl_udp_socket_listen(_dsso, sl_peerinfo(INADDR_ANY, 2001), [&](sl_event e) {
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
```

Use with your project
===
1. Use shared library
------

First, compile and install the library, the default install path will be `/usr/local/lib`
```sh
make release && sudo make install
```
Then, in your `Makefile`, add the following flags:
```makefile
-I/usr/local/include -L/usr/local/lib -lsocklite
```

2. Use source file
------

run the script `amalgamate` under the root path of the source code
```sh
./amalgamate
```
the script will create a folder named `dist` and two files:
* socketlite.h
* socketlite.cpp

Then add these two files to your project

Change log
===
v0.1 Initialize the library and import tcp & udp socket. Now support socks5 proxy for tcp socket.

v0.2 Fix some bugs, now support SO_KEEPALIVE

v0.3 Format the socks5 proxy packet, support connect to socks5 proxy with username and password. Use `epoll`/`kqueue` to perform the poll action, in default, max support 1024 concurrency connections.

v0.4 Support amalgamate script, re-write `sl_socket::read_data` to avoid unfinished packet.

v0.5 Rewrite UDP socket, use `sl_poller` to monitor the UDP connections.

v0.6 Add log/multiple thread support, add async api for all socket type

