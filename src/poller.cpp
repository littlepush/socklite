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

#include "poller.h"

sl_poller::sl_poller()
	:m_fd(-1), m_events(NULL), m_tcp_svr_so(-1), m_udp_svr_so(-1)
{
#if SL_TARGET_LINUX
	m_fd = epoll_create1(0);
	if ( m_fd == -1 ) {
		throw(std::runtime_error("Failed to create poller"));
	}
	m_events = (struct epoll_event *)calloc(
			CO_MAX_SO_EVENTS, sizeof(struct epoll_event));
#elif SL_TARGET_MAC
	m_fd = kqueue();
	if ( m_fd == -1 ) {
		throw(std::runtime_error("Failed to create poller"));
	}
	m_events = (struct kevent *)calloc(
			CO_MAX_SO_EVENTS, sizeof(struct kevent));
#endif
}

sl_poller::~sl_poller() {
	if ( m_fd != -1 ) close(m_fd);
	if ( m_events != NULL ) free(m_events);
	m_fd = -1;
	m_events = NULL;
}

void sl_poller::bind_tcp_server( SOCKET_T so ) {
	m_tcp_svr_so = so;
}

void sl_poller::bind_udp_server( SOCKET_T so ) {
	m_udp_svr_so = so;
}

size_t sl_poller::fetch_events( sl_poller::earray &events, unsigned int timedout ) {
	if ( m_fd == -1 ) return 0;
	int _count = 0;
#if SL_TARGET_LINUX
	_count = epoll_wait( m_fd, m_events, CO_MAX_SO_EVENTS, timedout );
#elif SL_TARGET_MAC
	struct timespec _ts = { timedout / 1000, timedout % 1000 * 1000 * 1000 };
	_count = kevent(m_fd, NULL, 0, m_events, CO_MAX_SO_EVENTS, &_ts);
#endif

	for ( int i = 0; i < _count; ++i ) {
#if SL_TARGET_LINUX
		struct epoll_event *_pe = m_events + i;
#elif SL_TARGET_MAC
		struct kevent *_pe = m_events + i;
#endif
		sl_event _e;
		// Disconnected
#if SL_TARGET_LINUX
		if ( _pe->events & EPOLLERR || _pe->events & EPOLLHUP ) {
			_e.so = _pe->data.fd;
#elif SL_TARGET_MAC
		if ( _pe->flags & EV_EOF || _pe->flags & EV_ERROR ) {
			_e.so = _pe->ident;
#endif
			_e.event = SL_EVENT_FAILED;
			events.push_back(_e);
			continue;
		}
#if SL_TARGET_LINUX
		else if ( _pe->data.fd == m_tcp_svr_so ) {
#elif SL_TARGET_MAC
		else if ( _pe->ident == m_tcp_svr_so ) {
#endif
			// Incoming
			while ( true ) {
				struct sockaddr _inaddr;
				socklen_t _inlen;
				SOCKET_T _inso = accept( m_tcp_svr_so, &_inaddr, &_inlen );
				if ( _inso == -1 ) {
					// No more incoming
					if ( errno == EAGAIN || errno == EWOULDBLOCK ) break;
					// On error
					_e.event = SL_EVENT_FAILED;
					_e.so = m_tcp_svr_so;
					events.push_back(_e);
					break;
				} else {
					// Set non-blocking
					unsigned long _u = 1;
					SL_NETWORK_IOCTL_CALL(_inso, FIONBIO, &_u);
					_e.event = SL_EVENT_ACCEPT;
					_e.so = _inso;
					events.push_back(_e);
					// Add to poll monitor
					this->monitor_socket(_inso);
				}
			}
		}
#if SL_TARGET_LINUX
		else if ( _pe->data.fd == m_udp_svr_so ) {
#elif SL_TARGET_MAC
		else if ( _pe->ident == m_udp_svr_so ) {
#endif
			// Nothing now...
		}
		else {
			// R/W
#if SL_TARGET_LINUX
			_e.so = _pe->data.fd;
#elif SL_TARGET_MAC
			_e.so = _pe->ident;
#endif
			int _error = 0, _len = sizeof(int);
			getsockopt( _e.so, SOL_SOCKET, SO_ERROR, 
					(char *)&_error, (socklen_t *)&_len);
			_e.event = (_error != 0) ? SL_EVENT_FAILED : SL_EVENT_DATA;
			events.push_back(_e);
		}
	}
	return events.size();
}

void sl_poller::monitor_socket( SOCKET_T so ) {
	if ( m_fd == -1 ) return;
#if SL_TARGET_LINUX
	struct epoll_event _ee;
	_ee.data.fd = so;
	_ee.events = EPOLLIN | EPOLLET | EPOLLOUT;
	epoll_ctl( m_fd, EPOLL_CTL_ADD, so, &_ee );
#elif SL_TARGET_MAC
	struct kevent _ke;
	EV_SET(&_ke, so, EVFILT_READ | EVFILT_WRITE, EV_ADD, 0, 0, NULL);
	kevent(m_fd, &_ke, 1, NULL, 0, NULL);
#endif
}
