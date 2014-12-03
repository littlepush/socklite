Sock Lite
===
This is a lite version of socket implemented in C++. The library can be used in Linux, Windows and iOS.

You can either use it as a shared library or just copy the code into your project.

For TCP socket, can connect to peer via a SOCKS5 proxy.

How to use
===

    #include <sl/socket.h>

    void worker_listen( unsigned int port ) {
        // 1. Create a server socket listen to specified port
        sl_tcpsocket _server_so;
        if ( _server_so.listen( port ) == false ) {
            exit(1);
        }
        // 2. Loop to get client
        while ( true ) {
            // 3. Get the client from the server socket, 
            //  if NULL, means no incoming connection
            sl_tcpsocket *_client = _server_so.get_client();
            if ( _client == NULL ) continue;

            // 4. Read data from the client
            string _buffer;
            _client->read_data( _buffer );

            // 5. Get the peer ip & port infomation
            u_int32_t _ip, _port;
            network_peer_info_from_socket(_cilent->m_socket, _ip, _port);
            string _ip_addr;
            network_int_to_ipaddress( _ip, _ip_addr );
            printf("[%s:%u]%s\n", _ip_addr.c_str(), _port, _buffer );

            // 6. Create a redirect socket.
            sl_tcpsocket _redirect_so;
            // 7. Setup socks5 proxy use localhost:8081
            _redirect_so.setup_proxy( "127.0.0.1", 8081 );
            // 8. Connect to github via the proxy.
            if ( !_redirect_so.connect( "github.com", 443) ) {
                _server_so.release_client(_client);
                continue;
            }
            // 9. Redirect the data and write back to client.
            _redirect_so.write_data( _buffer );
            _redirect_so.read_data( _buffer );
            _client->write_data( _buffer );

            // 10. Release the client
            _server_so.release_client(_client);
        }
    }

Change log
===
v0.1 Initialize the library and import tcp & udp socket. Now support socks5 proxy for tcp socket.