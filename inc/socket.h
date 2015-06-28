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

#pragma once

#ifndef __SOCK_LITE_SOCKET_H__
#define __SOCK_LITE_SOCKET_H__

#if ( defined WIN32 | defined _WIN32 | defined WIN64 | defined _WIN64 )
    #define _SL_PLATFORM_WIN      1
#elif TARGET_OS_WIN32
    #define _SL_PLATFORM_WIN      1
#elif defined __CYGWIN__
    #define _SL_PLATFORM_WIN      1
#else
    #define _SL_PLATFORM_WIN      0
#endif
#ifdef __APPLE__
    #define _SL_PLATFORM_MAC      1
#else
    #define _SL_PLATFORM_MAC      0
#endif
#if _SL_PLATFORM_WIN == 0 && _SL_PLATFORM_MAC == 0
    #define _SL_PLATFORM_LINUX    1
#else
    #define _SL_PLATFORM_LINUX    0
#endif
#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
    #define _SL_PLATFORM_IOS      1
#else
    #define _SL_PLATFORM_IOS      0
#endif

#define SL_TARGET_WIN32  (_SL_PLATFORM_WIN == 1)
#define SL_TARGET_LINUX  (_SL_PLATFORM_LINUX == 1)
#define SL_TARGET_MAC    (_SL_PLATFORM_MAC == 1)
#define SL_TARGET_IOS    (_SL_PLATFORM_IOS == 1)

#if SL_TARGET_WIN32
// Disable the certain warn in Visual Studio for old functions.
#pragma warning (disable : 4996)
#pragma warning (disable : 4251)

#endif

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>
#include <stddef.h>
#include <math.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/stat.h>

#include <iostream>
#include <string>
using namespace std;

#if SL_TARGET_WIN32
#include <WinSock2.h>
#include <Windows.h>
#include <process.h>
#else
#include <pthread.h>
#include <stddef.h>
#include <sys/time.h>
#endif

// Linux Thread, pit_t
#if SL_TARGET_LINUX
#include <sys/syscall.h>
#include <unistd.h>
#include <signal.h>
#define gettid()    syscall(__NR_gettid)
#endif

// For Mac OS X
#ifdef __APPLE__
#include <libkern/OSAtomic.h>
#include <unistd.h>
#include <sys/syscall.h>
#define gettid()    syscall(SYS_gettid)
#endif

#if SL_TARGET_WIN32
    #include <WS2tcpip.h>
    #pragma comment( lib, "Ws2_32.lib" )
    #define SL_NETWORK_NOSIGNAL           0
    #define SL_NETWORK_IOCTL_CALL         ioctlsocket
    #define SL_NETWORK_CLOSESOCK          ::closesocket
#else 
    #include <sys/socket.h>
    #include <unistd.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <sys/ioctl.h>
    #include <netinet/tcp.h>
    #define SL_NETWORK_NOSIGNAL           MSG_NOSIGNAL
    #define SL_NETWORK_IOCTL_CALL         ioctl
    #define SL_NETWORK_CLOSESOCK          ::close
#endif

#if SL_TARGET_MAC
    #undef  SL_NETWORK_NOSIGNAL
    #define SL_NETWORK_NOSIGNAL           0
#endif

typedef enum {
    SO_INVALIDATE       = -1,
    SO_IDLE             = 0,
    SO_OK               = 1
} SOCKETSTATUE;

typedef enum {
    SO_CHECK_WRITE      = 1,
    SO_CHECK_READ       = 2,
    SO_CHECK_CONNECT    = SO_CHECK_WRITE | SO_CHECK_READ
} SOCKETOPT;

typedef long SOCKET_T;

#ifndef INVALIDATE_SOCKET
#define INVALIDATE_SOCKET           ((long)((long)0 - (long)1))
#endif

#define SOCKET_NOT_VALIDATE( so )   ((so) == INVALIDATE_SOCKET)
#define SOCKET_VALIDATE( so )       ((so) != INVALIDATE_SOCKET)

// In No-Windows
#ifndef FAR
#define FAR
#endif

#ifndef __SOCKET_SERVER_INIT_IN_WINDOWS__
#define __SOCKET_SERVER_INIT_IN_WINDOWS__
#if SL_TARGET_WIN32

// In Windows Only.
// This class is used to initialize the WinSock Server.
// A global instance of this object will be create and
// provide nothing. only the c'str of this object
// will invoke WSAStartup and the d'str will invoke 
// WSACleanup.
// In Linux or other platform, this object will not be
// defined.
template< int __TMP_VALUE__ = 0 >
class __socket_init_svr_in_windows
{
    __socket_init_svr_in_windows< __TMP_VALUE__ >()
    {
        WSADATA v_wsaData;
        WORD v_wVersionRequested;

        v_wVersionRequested = MAKEWORD(1, 1);
        WSAStartup(v_wVersionRequested, &v_wsaData);
    }

public:
    ~__socket_init_svr_in_windows< __TMP_VALUE__ >()
    {
        WSACleanup();
    }
    static __socket_init_svr_in_windows< __TMP_VALUE__ > __g_socksvrInWindows;
};

template< > __socket_init_svr_in_windows< 0 > 
__socket_init_svr_in_windows< 0 >::__g_socksvrInWindows;

#endif
#endif

// Translate Domain to IP Address
char * network_domain_to_ip(const char * domain, char * output, unsigned int length);

// Translate Domain to InAddr
unsigned int network_domain_to_inaddr(const char * domain);

// Get localhost's computer name on LAN.
void network_get_localhost_name( string &hostname );

// Convert the uint ip addr to human readable ip string.
void network_int_to_ipaddress( const u_int32_t ipaddr, string &ip );

// Get peer ipaddress and port from a specified socket handler.
void network_peer_info_from_socket( const SOCKET_T hSo, u_int32_t &ipaddr, u_int32_t &port );

// Check the specified socket's status according to the option.
SOCKETSTATUE socket_check_status( SOCKET_T hSo, SOCKETOPT option = SO_CHECK_READ, u_int32_t waitTime = 0 );

// The basic virtual socket class
class sl_socket
{
public:
    virtual ~sl_socket();
    // Connect to peer
    virtual bool connect( const string &ipaddr, u_int32_t port ) = 0;
    // Listen on specified port and address, default is 0.0.0.0
    virtual bool listen( u_int32_t port, u_int32_t ipaddr = INADDR_ANY ) = 0;
    // Close the connection
    virtual void close() = 0;
    // When the socket is a listener, use this method 
    // to accept client's connection.
    //virtual sl_socket *get_client( u_int32_t timeout = 100 ) = 0;
    //virtual void release_client( sl_socket *client ) = 0;

    // Set current socket reusable or not
    virtual bool set_reusable( bool reusable = true ) = 0;

    // Read data from the socket until timeout or get any data.
    virtual bool read_data( string &buffer, u_int32_t timeout = 1000, SOCKETSTATUE *pst = NULL ) = 0;
    // Write data to peer.
    virtual bool write_data( const string &data ) = 0;
};

#endif 
// sock.lite.h

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
