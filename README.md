Sock Lite
===
This is a lite version of socket implemented in C++. The library can be used in Linux and Mac OS.

You can either use it as a shared library or just copy the code into your project.

For TCP socket, can connect to peer via a SOCKS5 proxy.

I also provide a simpale example for creating a socks5 proxy server side.

How to use
===

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
						if ( _wsrc.read_data(_buf) ) {
							_wdst.write_data(_buf);
						}
					}
				}
			}
        }
    }

Change log
===
v0.1 Initialize the library and import tcp & udp socket. Now support socks5 proxy for tcp socket.
v0.2 Fix some bugs, now support SO_KEEPALIVE
v0.3 Format the socks5 proxy package, support connect to socks5 proxy with username and password. Use `epoll`/`kqueue` to perform the poll action, in default, max support 1024 concurrency connections.
