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

#include "socket.h"
// #include <execinfo.h>

// In No-Windows
#ifndef FAR
#define FAR
#endif

/* Translate Domain to IP Address */
char * network_domain_to_ip(const char * domain, char * output, unsigned int length)
{
    struct hostent FAR * _host_ent;
    struct in_addr _in_addr;
    char * _c_addr;
    
    memset(output, 0, length);
    
    _host_ent = gethostbyname(domain);
    if (_host_ent == NULL) return output;
    
    _c_addr = _host_ent->h_addr_list[0];
    if (_c_addr == NULL) return output;
    
    memmove(&_in_addr, _c_addr, 4);
    strcpy(output, inet_ntoa(_in_addr));
    
    return output;
}

/* Translate Domain to InAddr */
unsigned int network_domain_to_inaddr(const char * domain)
{
    /* Get the IP Address of the domain by invoking network_domain_to_ip */
    char _c_address[16];

    if (domain == NULL) return INADDR_ANY;
    if (network_domain_to_ip(domain, _c_address, 16)[0] == '\0') {
        // Try direct translate the domain
        return inet_addr(domain);
        //return (unsigned int)(-1L);
    }
    return inet_addr(_c_address);
}

// Translate the ip string to an InAddr
uint32_t network_ipstring_to_inaddr(const string &ipaddr)
{
    return inet_addr(ipaddr.c_str());
}

// Translate the InAddr to an Ip string
void network_inaddr_to_string(uint32_t inaddr, string &ipstring)
{
    char _ip_[16] = {0};
    sprintf( _ip_, "%u.%u.%u.%u",
        (inaddr >> (0 * 8)) & 0x00FF,
        (inaddr >> (1 * 8)) & 0x00FF,
        (inaddr >> (2 * 8)) & 0x00FF,
        (inaddr >> (3 * 8)) & 0x00FF 
    );
    ipstring = string(_ip_);
}

// Get localhost's computer name on LAN.
void network_get_localhost_name( string &hostname )
{
    char __hostname[256] = { 0 };
    if ( gethostname( __hostname, 256 ) == -1 ) {
        return;
    }
    hostname = string(__hostname);
}

// Convert the uint ip addr to human readable ip string.
void network_int_to_ipaddress( const u_int32_t ipaddr, string &ip )
{
    char _ip_[16] = {0};
    sprintf( _ip_, "%u.%u.%u.%u",
        (ipaddr >> (0 * 8)) & 0x00FF,
        (ipaddr >> (1 * 8)) & 0x00FF,
        (ipaddr >> (2 * 8)) & 0x00FF,
        (ipaddr >> (3 * 8)) & 0x00FF 
    );
    ip = string(_ip_);
}

// Get peer ipaddress and port from a specified socket handler.
void network_peer_info_from_socket( const SOCKET_T hSo, u_int32_t &ipaddr, u_int32_t &port )
{
    if ( SOCKET_NOT_VALIDATE(hSo) ) return;

    struct sockaddr_in _addr;
    socklen_t _addrLen = sizeof(_addr);
    memset( &_addr, 0, sizeof(_addr) );
    if ( 0 == getpeername( hSo, (struct sockaddr *)&_addr, &_addrLen ) )
    {
        port = ntohs(_addr.sin_port);
        ipaddr = _addr.sin_addr.s_addr;
    }
}

// Get current socket's port info
void network_sock_info_from_socket( const SOCKET_T hSo, uint32_t &port )
{
    if ( SOCKET_NOT_VALIDATE(hSo) ) return;

    struct sockaddr_in _addr;
    socklen_t _addrLen = sizeof(_addr);
    memset( &_addr, 0, sizeof(_addr) );
    if ( 0 == getsockname( hSo, (struct sockaddr *)&_addr, &_addrLen ) )
    {
        port = ntohs(_addr.sin_port);
    }
}

// Check the specified socket's status according to the option.
SOCKETSTATUE socket_check_status( SOCKET_T hSo, SOCKETOPT option, u_int32_t waitTime )
{
    if ( SOCKET_NOT_VALIDATE(hSo) ) return SO_INVALIDATE;
    fd_set _fs;
    FD_ZERO( &_fs );
    FD_SET( hSo, &_fs );

    int _ret = 0; struct timeval _tv = {(int32_t)waitTime / 1000, (int32_t)waitTime % 1000 * 1000};

    if ( option & SO_CHECK_READ ) {
        do {
            _ret = ::select( hSo + 1, &_fs, NULL, NULL, &_tv );
        } while ( _ret < 0 && errno == EINTR );
        if ( _ret > 0 ) {
            char _word;
            // the socket has received a close sig
            if ( ::recv( hSo, &_word, 1, MSG_PEEK ) <= 0 ) {
                return SO_INVALIDATE;
            }
            return SO_OK;
        }
        if ( _ret < 0 ) return SO_INVALIDATE;
    }

    if ( option & SO_CHECK_WRITE ){
        do {
            _ret = ::select( hSo + 1, NULL, &_fs, NULL, &_tv );
        } while ( _ret < 0 && errno == EINTR );
        if ( _ret > 0 ) return SO_OK;
        if ( _ret < 0 ) return SO_INVALIDATE;
    }
    return SO_IDLE;
}

// Set the linger time for a socket, I strong suggest not to change this value unless you 
// know what you are doing
bool socket_set_linger_time(SOCKET_T so, bool onoff, unsigned timeout)
{
	struct linger _sol = { (onoff ? 1 : 0), (int)timeout };
	return ( setsockopt(so, SOL_SOCKET, SO_LINGER, &_sol, sizeof(_sol)) == 0 );
}

sl_ip::sl_ip() {}
sl_ip::sl_ip(const sl_ip& rhs) : ip_(rhs.ip_) {}

// Conversition
sl_ip::sl_ip(const string &ipaddr) : ip_(ipaddr) {}
sl_ip::sl_ip(uint32_t ipaddr) {
    network_inaddr_to_string(ipaddr, ip_);
}
sl_ip::operator uint32_t() const {
    return network_ipstring_to_inaddr(ip_);
}
sl_ip::operator string&() { return ip_; }
sl_ip::operator string() const { return ip_; }
sl_ip::operator const string&() const { return ip_; }
sl_ip::operator const char *() const { return ip_.c_str(); }
const char * sl_ip::c_str() const { return ip_.c_str(); }
size_t sl_ip::size() const { return ip_.size(); }
// Cast operator
sl_ip & sl_ip::operator = (const string &ipaddr) {
    ip_ = ipaddr; 
    return *this;
}

sl_ip & sl_ip::operator = (uint32_t ipaddr) {
    network_inaddr_to_string(ipaddr, ip_);
    return *this;
}
bool sl_ip::operator == (const sl_ip& rhs) const
{
    return ip_ == rhs.ip_;
}
bool sl_ip::operator != (const sl_ip& rhs) const
{
    return ip_ != rhs.ip_;
}
bool sl_ip::operator <(const sl_ip& rhs) const
{
    return ntohl(*this) < ntohl(rhs);
}
bool sl_ip::operator >(const sl_ip& rhs) const
{
    return ntohl(*this) > ntohl(rhs);
}
bool sl_ip::operator <=(const sl_ip& rhs) const
{
    return ntohl(*this) <= ntohl(rhs);
}
bool sl_ip::operator >=(const sl_ip& rhs) const
{
    return ntohl(*this) >= ntohl(rhs);
}

ostream & operator << (ostream &os, const sl_ip & ip) {
    os << (const string&)ip;
    return os;
}

// Peer Info
void sl_peerinfo::parse_peerinfo_from_string(const string &format_string) {
    for ( size_t i = 0; i < format_string.size(); ++i ) {
        if ( format_string[i] != ':' ) continue;
        ip_ = format_string.substr(0, i);
        port_ = stoi(format_string.substr(i + 1), nullptr, 10);
        format_ = format_string;
        break;
    }
}
void sl_peerinfo::set_peerinfo(const string &ipaddress, uint16_t port) {
    ip_ = ipaddress;
    port_ = port;
    format_ = (const string &)ip_;
    format_ += ":";
    format_ += to_string(port_);
}
void sl_peerinfo::set_peerinfo(uint32_t inaddr, uint16_t port) {
    ip_ = inaddr;
    port_ = port;
    format_ = (const string &)ip_;
    format_ += ":";
    format_ += to_string(port_);
}

sl_peerinfo::sl_peerinfo(): format_("0.0.0.0:0"), ipaddress(ip_), port_number(port_) {}
sl_peerinfo::sl_peerinfo(uint32_t inaddr, uint16_t port) 
: ip_(inaddr), port_(port), ipaddress(ip_), port_number(port_) { 
    format_ = (const string &)ip_;
    format_ += ":";
    format_ += to_string(port_);
}
sl_peerinfo::sl_peerinfo(const string &format_string) : ipaddress(ip_), port_number(port_) {
    parse_peerinfo_from_string(format_string);
}
sl_peerinfo::sl_peerinfo(const string &ipstring, uint16_t port) 
: ip_(ipstring), port_(port), ipaddress(ip_), port_number(port_) {
    format_ = (const string &)ip_;
    format_ += ":";
    format_ += to_string(port_);
}
sl_peerinfo::sl_peerinfo(const sl_peerinfo& rhs)
: ip_(rhs.ip_), port_(rhs.port_), ipaddress(ip_), port_number(port_) { }

sl_peerinfo & sl_peerinfo::operator = (const sl_peerinfo& rhs) {
    ip_ = rhs.ip_;
    port_ = rhs.port_;
    format_ = rhs.format_;
    return *this;
}
sl_peerinfo & sl_peerinfo::operator = (const string &format_string) {
    parse_peerinfo_from_string(format_string);
    return *this;
}

sl_peerinfo::operator bool() const { return port_ > 0 && port_ <= 65535; }
sl_peerinfo::operator const string () const { 
    return format_;
}
sl_peerinfo::operator const char *() const {
    return format_.c_str();
}
const char * sl_peerinfo::c_str() const {
    return format_.c_str();
}
size_t sl_peerinfo::size() const {
    return format_.size();
}

ostream & operator << (ostream &os, const sl_peerinfo &peer) {
    os << peer.operator const string();
    return os;
}

sl_socket::sl_socket(bool iswrapper) : m_iswrapper(iswrapper), m_socket(INVALIDATE_SOCKET) { }

// Virtual destructure
sl_socket::~sl_socket()
{
    if ( m_iswrapper == false ) {
        this->close();
    }
}
// Close the connection
void sl_socket::close()
{
    // // Debug to output the call stack
    // void *_callstack[128];
    // int _frames = backtrace(_callstack, 128);
    // backtrace_symbols_fd(_callstack, _frames, STDOUT_FILENO);

    if ( SOCKET_NOT_VALIDATE(m_socket) ) return;
    SL_NETWORK_CLOSESOCK(m_socket);
    m_socket = INVALIDATE_SOCKET;
}

// Set current socket reusable or not
bool sl_socket::set_reusable( bool reusable )
{
    if ( m_socket == INVALIDATE_SOCKET ) return false;
    int _reused = reusable ? 1 : 0;
    return setsockopt( m_socket, SOL_SOCKET, SO_REUSEADDR,
        (const char *)&_reused, sizeof(int) ) != -1;
}

bool sl_socket::set_keepalive( bool keepalive )
{
    if ( m_socket == INVALIDATE_SOCKET ) return false;
    int _keepalive = keepalive ? 1 : 0;
    return setsockopt( m_socket, SOL_SOCKET, SO_KEEPALIVE, 
        (const char *)&_keepalive, sizeof(int) );
}

bool sl_socket::set_nonblocking(bool nonblocking) 
{
    if ( m_socket == INVALIDATE_SOCKET ) return false;
    unsigned long _u = (nonblocking ? 1 : 0);
    return SL_NETWORK_IOCTL_CALL(m_socket, FIONBIO, &_u) >= 0;
}

bool sl_socket::set_socketbufsize( unsigned int rmem, unsigned int wmem )
{
    if ( m_socket == INVALIDATE_SOCKET ) return false;
    if ( rmem != 0 ) {
        setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, 
                (char *)&rmem, sizeof(rmem));
    }
    if ( wmem != 0 ) {
        setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF,
                (char *)&wmem, sizeof(wmem));
    }
    return true;
}

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
