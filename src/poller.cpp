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

// Convert the EVENT_ID to string
const string sl_event_name(uint32_t eid)
{
	static string _accept = " SL_EVENT_ACCEPT ";
	static string _data = " SL_EVENT_DATA|SL_EVENT_READ ";
	static string _failed = " SL_EVENT_FAILED ";
	static string _write = " SL_EVENT_WRITE|SL_EVENT_CONNECT ";
	static string _timeout = " SL_EVENT_TIMEOUT ";
	static string _unknown = " Unknown Event ";

	string _name;
	if ( eid & SL_EVENT_ACCEPT ) _name += _accept;
	if ( eid & SL_EVENT_DATA ) _name += _data;
	if ( eid & SL_EVENT_FAILED ) _name += _failed;
	if ( eid & SL_EVENT_WRITE ) _name += _write;
	if ( eid & SL_EVENT_TIMEOUT ) _name += _timeout;
	if ( _name.size() == 0 ) return _unknown;
	return _name;
}
// Output of the event
ostream & operator << (ostream &os, const sl_event & e)
{
    os
        << "event " << sl_event_name(e.event) << " for "
        << (e.socktype == IPPROTO_TCP ? "tcp socket " : "udp socket ") << e.so;
    return os;
}

// Create a failed or timedout event structure object
sl_event sl_event_make_failed(SOCKET_T so) {
	sl_event _e;
	memset(&_e, 0, sizeof(_e));
	_e.so = so;
	_e.event = SL_EVENT_FAILED;
	return _e;
}
sl_event sl_event_make_timeout(SOCKET_T so) {
	sl_event _e;
	memset(&_e, 0, sizeof(_e));
	_e.so = so;
	_e.event = SL_EVENT_TIMEOUT;
	return _e;
}

sl_poller::sl_poller()
	:m_fd(-1), m_events(NULL)
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

bool sl_poller::bind_tcp_server( SOCKET_T so ) {
#if SL_TARGET_LINUX
	auto _tit = m_tcp_svr_map.find(so);
	bool _is_new_bind = (_tit == end(m_tcp_svr_map));
#endif
	m_tcp_svr_map[so] = true;
	int _retval = 0;
#if SL_TARGET_LINUX
	struct epoll_event _e;
	_e.data.fd = so;
	_e.events = EPOLLIN | EPOLLET;
	_retval = epoll_ctl( m_fd, EPOLL_CTL_ADD, so, &_e );
#elif SL_TARGET_MAC
	struct kevent _e;
	EV_SET(&_e, so, EVFILT_READ, EV_ADD, 0, 0, NULL);
	_retval = kevent(m_fd, &_e, 1, NULL, 0, NULL);
#endif
	if ( _retval == -1 ) {
		lerror << "failed to bind and monitor the tcp server socket: " << ::strerror(errno) << lend;
#if SL_TARGET_LINUX
		if ( _is_new_bind ) {
			m_tcp_svr_map.erase(so);
		}
#endif
	}
	return (_retval != -1);
}

size_t sl_poller::fetch_events( sl_poller::earray &events, unsigned int timedout ) {
	if ( m_fd == -1 ) return 0;
	int _count = 0;
#if SL_TARGET_LINUX
	do {
		_count = epoll_wait( m_fd, m_events, CO_MAX_SO_EVENTS, timedout );
	} while ( _count < 0 && errno == EINTR );
#elif SL_TARGET_MAC
	struct timespec _ts = { timedout / 1000, timedout % 1000 * 1000 * 1000 };
	_count = kevent(m_fd, NULL, 0, m_events, CO_MAX_SO_EVENTS, &_ts);
#endif

	time_t _now_time = time(NULL);

	for ( int i = 0; i < _count; ++i ) {
#if SL_TARGET_LINUX
		struct epoll_event *_pe = m_events + i;
#elif SL_TARGET_MAC
		struct kevent *_pe = m_events + i;
#endif
		sl_event _e;
		_e.source = INVALIDATE_SOCKET;
		_e.socktype = IPPROTO_TCP;
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

			// Remove the timeout info
			lock_guard<mutex> _(m_timeout_mutex);
			m_timeout_map.erase(_e.so);

			continue;
		}
#if SL_TARGET_LINUX
		else if ( m_tcp_svr_map.find(_pe->data.fd) != m_tcp_svr_map.end() ) {
			_e.source = _pe->data.fd;
#elif SL_TARGET_MAC
		else if ( m_tcp_svr_map.find(_pe->ident) != m_tcp_svr_map.end()  ) {
			_e.source = _pe->ident;
#endif
			// Incoming
			while ( true ) {
				struct sockaddr _inaddr;
				socklen_t _inlen;
				SOCKET_T _inso = accept( _e.source, &_inaddr, &_inlen );
				if ( _inso == -1 ) {
					// No more incoming
					if ( errno == EAGAIN || errno == EWOULDBLOCK ) break;
					// On error
					_e.event = SL_EVENT_FAILED;
					_e.so = _e.source;
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
					// this->monitor_socket(_inso);
				}
			}
		}
		else {
			// R/W
#if SL_TARGET_LINUX
			_e.so = _pe->data.fd;
#elif SL_TARGET_MAC
			_e.so = _pe->ident;
#endif
			int _error = 0, _len = sizeof(int);
			// Get the type
            int _type;
			getsockopt( _e.so, SOL_SOCKET, SO_TYPE,
					(char *)&_type, (socklen_t *)&_len);
            if ( _type == SOCK_STREAM ) {
                _e.socktype = IPPROTO_TCP;
            } else {
                _e.socktype = IPPROTO_UDP;
            }

			// ldebug << "get event for socket: " << _e.so << lend;
			getsockopt( _e.so, SOL_SOCKET, SO_ERROR, 
					(char *)&_error, (socklen_t *)&_len);
			if ( _error == 0 ) {
				// Check if is read or write
#if SL_TARGET_LINUX
				if ( _pe->events & EPOLLIN ) {
					_e.event = SL_EVENT_DATA;
					// ldebug << "did get r/w event for socket: " << _e.so << ", event: " << sl_event_name(_e.event) << lend;
					if ( _e.socktype == IPPROTO_UDP ) {
						// Try to fetch the address info
						socklen_t _l = sizeof(_e.address);
						::recvfrom( _e.so, NULL, 0, MSG_PEEK,
			            	(struct sockaddr *)&_e.address, &_l);
					}
					events.push_back(_e);
				}
				if ( _pe->events & EPOLLOUT ) {
					_e.event = SL_EVENT_WRITE;
					// ldebug << "did get r/w event for socket: " << _e.so << ", event: " << sl_event_name(_e.event) << lend;
					events.push_back(_e);
				}
#elif SL_TARGET_MAC
				if ( _pe->filter == EVFILT_READ ) {
					_e.event = SL_EVENT_DATA;
					if ( _e.socktype == IPPROTO_UDP ) {
						// Try to fetch the address info
						socklen_t _l = sizeof(_e.address);
						::recvfrom( _e.so, NULL, 0, MSG_PEEK,
			            	(struct sockaddr *)&_e.address, &_l);
					}
				}
				else {
					_e.event = SL_EVENT_WRITE;
				}
				events.push_back(_e);
				// ldebug << "did get r/w event for socket: " << _e.so << ", event: " << sl_event_name(_e.event) << lend;
#endif
			} else {
				_e.event = SL_EVENT_FAILED;
				events.push_back(_e);
				// ldebug << "did get r/w event for socket: " << _e.so << ", event: " << sl_event_name(_e.event) << lend;
			}

			lock_guard<mutex> _(m_timeout_mutex);
			m_timeout_map.erase(_e.so);
		}
	}

	vector<SOCKET_T> _timeout_list;
	lock_guard<mutex> _(m_timeout_mutex);
	for ( auto _tit = begin(m_timeout_map); _tit != end(m_timeout_map); ++_tit ) {
		if ( _tit->second > 0 && _tit->second < _now_time ) {
			_timeout_list.push_back(_tit->first);
			#if DEBUG
			ldebug << "socket " << _tit->first << " runs time out in poller" << lend;
			#endif
		}
	}

	for ( auto _so : _timeout_list ) {
		sl_event _e;
		_e.so = _so;
		_e.event = SL_EVENT_TIMEOUT;
		events.push_back(_e);
		m_timeout_map.erase(_so);
	}

	return events.size();
}

bool sl_poller::monitor_socket( 
	SOCKET_T so, 
	bool oneshot, 
	uint32_t eid, 
	uint32_t timedout
) {
	if ( m_fd == -1 ) return false;

	// ldebug << "is going to monitor socket " << so << " for event " << sl_event_name(eid) << lend;
#if SL_TARGET_LINUX

	// Socket must be nonblocking
	unsigned long _u = 1;
	SL_NETWORK_IOCTL_CALL(so, FIONBIO, &_u);

	struct epoll_event _ee;
	_ee.data.fd = so;
	_ee.events = EPOLLET;
	if ( eid & SL_EVENT_DATA ) _ee.events |= EPOLLIN;
	if ( eid & SL_EVENT_WRITE ) _ee.events |= EPOLLOUT;

	// In default the operation should be ADD, and we
	// will try to use ADD and MOD both.
	int _op = EPOLL_CTL_ADD;
	if ( oneshot ) {
		_ee.events |= EPOLLONESHOT;
	}
	if ( -1 == epoll_ctl( m_fd, _op, so, &_ee ) ) {
		if ( errno == EEXIST ) {
			if ( -1 == epoll_ctl( m_fd, EPOLL_CTL_MOD, so, &_ee ) ) {
				lerror << "failed to monitor the socket " << so << ": " << ::strerror(errno) << lend;
				return false;
			}
		} else if ( errno == ENOENT ) {
			if ( -1 == epoll_ctl(m_fd, EPOLL_CTL_ADD, so, &_ee ) ) {
				lerror << "failed to monitor the socket " << so << ": " << ::strerror(errno) << lend;
				return false;
			}
		} else {
			lerror << "failed to monitor the socket " << so << ": " << ::strerror(errno) << lend;
			return false;
		}
	}
#elif SL_TARGET_MAC
	struct kevent _ke;
	unsigned short _flags = EV_ADD;
	if ( oneshot ) {
		_flags |= EV_ONESHOT;
	}
	if ( eid & SL_EVENT_DATA ) {
		EV_SET(&_ke, so, EVFILT_READ, _flags, 0, 0, NULL);
		if ( -1 == kevent(m_fd, &_ke, 1, NULL, 0, NULL) ) {
			lerror << "failed to monitor the socket for read " << so << ": " << ::strerror(errno) << lend;
			return false;
		}
	}
	if ( eid & SL_EVENT_WRITE ) {
		EV_SET(&_ke, so, EVFILT_WRITE, _flags, 0, 0, NULL);
		if ( -1 == kevent(m_fd, &_ke, 1, NULL, 0, NULL) ) {
			lerror << "failed to monitor the socket for write " << so << ": " << ::strerror(errno) << lend;
			return false;
		}
	}
#endif

	lock_guard<mutex> _(m_timeout_mutex);
	if ( timedout == 0 ) {
		#if DEBUG
		ldebug << "socket " << so << " will monitor infinitvie" << lend;
		#endif
		m_timeout_map[so] = 0;
	} else {
		#if DEBUG
		ldebug 
			<< "socket " << so << " monitor on event " << sl_event_name(eid) 
			<< ", will time out after " << timedout << " seconds" 
		<< lend;
		#endif
		m_timeout_map[so] = (time(NULL) + timedout);
	}
	return true;
}

void sl_poller::unmonitor_socket(SOCKET_T so) {
	lock_guard<mutex> _(m_timeout_mutex);
	m_timeout_map.erase(so);
}

sl_poller &sl_poller::server() {
	static sl_poller _g_poller;
	return _g_poller;
}

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
