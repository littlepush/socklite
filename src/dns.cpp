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

// The Following is original cleandns's Lisence
/*
 * Copyright (c) 2014, Push Chen
 * All rights reserved.
 * File Name         : dns.cpp
 * Author            : Push Chen
 * Date              : 2014-06-20
*/

/*
    LGPL V3 Lisence
    This file is part of cleandns.

    cleandns is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cleandns is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cleandns.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
    LISENCE FOR CLEANDNS
    COPYRIGHT (c) 2014, Push Chen.
    ALL RIGHTS RESERVED.

    REDISTRIBUTION AND USE IN SOURCE AND BINARY
    FORMS, WITH OR WITHOUT MODIFICATION, ARE
    PERMITTED PROVIDED THAT THE FOLLOWING CONDITIONS
    ARE MET:

    YOU USE IT, AND YOU JUST USE IT!.
    WHY NOT USE THIS LIBRARY IN YOUR CODE TO MAKE
    THE DEVELOPMENT HAPPIER!
    ENJOY YOUR LIFE AND BE FAR AWAY FROM BUGS.
*/

#include "dns.h"
#include "string_format.hpp"
#include <arpa/inet.h>

#ifdef SOCK_LITE_INTEGRATION_DNS

// Get the formated domain in the dns packet.
// The domain "www.google.com", will be "\3www\6google\3com\0".
int _dns_get_format_domain( const char *data, string &domain ) {
    domain.clear();
    for ( ;; ) {
        uint8_t _l = data[0];
        if ( _l == 0 ) break;
        data++;
        if ( domain.size() > 0 ) domain += ".";
        domain.append( data, _l );
        data += _l;
    }
    return domain.size() + 2;
}

// DNS Method
void _dns_format_domain(const string &dname, string &buf) {
    buf.resize(dname.size() + 2);
    char *_buf = &buf[0];
    vector<string> _com;
    cpputility::split_string(dname, ".", _com);
    for ( auto& _dp : _com ) {
        _buf[0] = (uint8_t)(_dp.size());
        for ( unsigned i = 0; i < _dp.size(); ++i ) {
            _buf[i + 1] = _dp[i];
        }
        _buf += (_dp.size() + 1);
    }
    _buf[0] = '\0';
}

// support the offset in the dns packet
int _dns_get_format_domain( const char *begin_of_domain, const char *begin_of_pkt, string &domain ) {
    domain.clear();
    int _readsize = 0;
    for ( ;; ) {
        uint8_t _l = begin_of_domain[_readsize];
        _readsize += 1;
        if ( (_l & 0xC0) == 0xC0 ) {
            // This is an offset
            string _reset_domain;
            uint16_t _offset = 0;
            if ( (_l & 0x3F) == 0 ) {
                // Use 2 bits
                _offset = ntohs(*(uint16_t *)(begin_of_domain + _readsize - 1));
                _offset &= 0x3FFF;
                _readsize += 1; // read more
            } else {
                _offset = (uint16_t)(_l & 0x3F);
                _readsize += 1;
            }
            _dns_get_format_domain(begin_of_pkt + _offset, _reset_domain);
            domain += _reset_domain;
            break;
        } else {
            if ( _l == 0 ) break;
            if ( domain.size() > 0 ) domain += ".";
            domain.append(begin_of_domain + _readsize, _l);
            _readsize += _l;
        }
    }
    return _readsize;
}

sl_dns_packet::sl_dns_packet()
{
    packet_data_.append((size_t)packet_header_size, '\0');
}
sl_dns_packet::sl_dns_packet(const string& packet, bool is_tcp_packet)
{
    // Force to resize to minimal header size
    if ( packet.size() < packet_header_size ) {
        packet_data_.append((size_t)packet_header_size, '\0');
    } else {
        const char *_data = packet.c_str();
        size_t _length = packet.size();
        if ( is_tcp_packet ) {
            _data += sizeof(uint16_t);
            _length -= sizeof(uint16_t);
        }
        packet_data_.append(_data, _length);
    }
}
sl_dns_packet::sl_dns_packet(const sl_dns_packet& rhs) 
: packet_data_(rhs.packet_data_) { }

sl_dns_packet::sl_dns_packet(const sl_dns_packet&& rrhs)
: packet_data_(move(rrhs.packet_data_)) { }

sl_dns_packet::sl_dns_packet(uint16_t trans_id, const string& query_domain)
{
    packet_data_.resize(packet_header_size);
    this->set_transaction_id(trans_id);
    this->set_is_query_request(true);
    this->set_is_recursive_desired(true);
    this->set_opcode(sl_dns_opcode_standard);
    this->set_query_domain(query_domain);
}

// Transaction ID
uint16_t sl_dns_packet::get_transaction_id() const
{
    return ntohs(*(const uint16_t *)(packet_data_.c_str()));
}
void sl_dns_packet::set_transaction_id(uint16_t tid)
{
    ((uint16_t *)&(packet_data_[0]))[0] = htons(tid);
}

// Request Type
bool sl_dns_packet::get_is_query_request() const
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    return (_h_flag & 0x8000) == 0;
}
bool sl_dns_packet::get_is_response_request() const
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    return (_h_flag & 0x8000) > 0;
}
void sl_dns_packet::set_is_query_request(bool isqr)
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    if ( isqr ) {
        _h_flag &= 0x7FFF;
    } else {
        _h_flag |= 0x8000;
    }
    ((uint16_t *)&(packet_data_[0]))[1] = htons(_h_flag);
}

// Op Code
sl_dns_opcode sl_dns_packet::get_opcode() const
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    return (sl_dns_opcode)((_h_flag >> 11) & 0x000F);
}
void sl_dns_packet::set_opcode(sl_dns_opcode opcode)
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);

    // Setup the mask
    uint16_t _mask = (((opcode & 0x000F) << 11) | 0x87FF);  // 1000 0111 1111 1111
    _h_flag &= _mask;

    ((uint16_t *)&(packet_data_[0]))[1] = htons(_h_flag);
}

// AA
bool sl_dns_packet::get_is_authoritative() const
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    return (_h_flag & 0x0400) > 0;
}
void sl_dns_packet::set_is_authoritative(bool auth)
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);

    if ( auth ) {
        _h_flag |= 0x0400;
    } else {
        _h_flag &= 0xFBFF;
    }

    ((uint16_t *)&(packet_data_[0]))[1] = htons(_h_flag);
}

// Truncation
bool sl_dns_packet::get_is_truncation() const
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    return (_h_flag & 0x0200) > 0;
}

void sl_dns_packet::set_is_truncation(bool trunc)
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);

    if ( trunc ) {
        _h_flag |= 0x0200;
    } else {
        _h_flag &= 0xFDFF;
    }

    ((uint16_t *)&(packet_data_[0]))[1] = htons(_h_flag);
}

// Recursive
bool sl_dns_packet::get_is_recursive_desired() const
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    return (_h_flag & 0x0100) > 0;
}
void sl_dns_packet::set_is_recursive_desired(bool rd)
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    if ( rd ) {
        _h_flag |= 0x0100;
    } else {
        _h_flag &= 0xFEFF;
    }
    ((uint16_t *)&(packet_data_[0]))[1] = htons(_h_flag);
}

// Recursive available
bool sl_dns_packet::get_is_recursive_available() const
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    return (_h_flag & 0x0080) > 0;
}
void sl_dns_packet::set_is_recursive_available(bool recursive)
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);

    if ( recursive ) {
        _h_flag |= 0x0080;
    } else {
        _h_flag &= 0xFF7F;
    }

    ((uint16_t *)&(packet_data_[0]))[1] = htons(_h_flag);
}

sl_dns_rcode sl_dns_packet::get_resp_code() const
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    return (sl_dns_rcode)(_h_flag & 0x000F);
}
void sl_dns_packet::set_resp_code(sl_dns_rcode rcode)
{
    uint16_t _h_flag = ntohs(((const uint16_t *)(packet_data_.c_str()))[1]);
    _h_flag |= (rcode & 0x000F);
    ((uint16_t *)&(packet_data_[0]))[1] = htons(_h_flag);
}

uint16_t sl_dns_packet::get_qd_count() const
{
    return ntohs(((const uint16_t *)(packet_data_.c_str()))[2]);
}
uint16_t sl_dns_packet::get_an_count() const
{
    return ntohs(((const uint16_t *)(packet_data_.c_str()))[3]);
}
uint16_t sl_dns_packet::get_ns_count() const
{
    return ntohs(((const uint16_t *)(packet_data_.c_str()))[4]);
}
uint16_t sl_dns_packet::get_ar_count() const
{
    return ntohs(((const uint16_t *)(packet_data_.c_str()))[5]);
}

// Operators
sl_dns_packet& sl_dns_packet::operator = (const sl_dns_packet& rhs)
{
    packet_data_ = rhs.packet_data_;
    return *this;
}
sl_dns_packet& sl_dns_packet::operator = (const sl_dns_packet&& rrhs)
{
    packet_data_ = move(rrhs.packet_data_);
    return *this;
}

const string sl_dns_packet::get_query_domain() const
{
    string _domain;
    if ( packet_data_.size() > (packet_header_size + 2 + 2) ) {
        _dns_get_format_domain(packet_data_.c_str() + packet_header_size, _domain);
    }
    return _domain;
}
void sl_dns_packet::set_query_domain(const string& domain, sl_dns_qtype qtype, sl_dns_qclass qclass)
{
    // Format the query domain
    string _fdomain;
    _dns_format_domain(domain, _fdomain);

    // Copy the domain
    packet_data_.resize(packet_header_size + _fdomain.size() + 2 * sizeof(uint8_t) + 2 * sizeof(uint8_t));
    memcpy(&packet_data_[packet_header_size], _fdomain.c_str(), _fdomain.size());

    // Set type and class
    uint16_t *_f_area = ((uint16_t *)(&packet_data_[packet_header_size + _fdomain.size()]));
    _f_area[0] = htons(qtype);
    _f_area[1] = htons(qclass);

    // Update QD count
    ((uint16_t *)&(packet_data_[0]))[2] = htons(1);
}

// A-Records
const vector<sl_ip> sl_dns_packet::get_A_records() const
{
    string _qdomain(this->get_query_domain());
    vector<sl_ip> _result_list;

    if ( _qdomain.size() == 0 ) return _result_list;

    // Get the data offset
    // Header Size + Domain Size(+2) + QType + QClass
    const char *_pbuf = (
        packet_data_.c_str() +      // Start point
        packet_header_size +        // Header Fixed Size
        (_qdomain.size() + 2) +     // Domain Size + 2(start length, and \0)
        2 +                         // QType
        2                           // QClass
        );

    uint16_t _an = this->get_an_count();
    for ( uint16_t i = 0; i < _an; ++i ) {
        string _domain;
        int _dsize = _dns_get_format_domain(_pbuf, packet_data_.c_str(), _domain);
        _pbuf += _dsize;

        // Get the record type
        uint16_t _type = ntohs(((uint16_t *)_pbuf)[0]);
        _pbuf += sizeof(uint16_t);

        bool _is_a_records = ((sl_dns_qtype)_type == sl_dns_qtype_host);

        // Skip QClass
        _pbuf += sizeof(uint16_t);
        // Skip TTL
        _pbuf += sizeof(uint32_t);

        // Get RData Length
        uint16_t _rlen = ntohs(*(uint16_t *)_pbuf);
        _pbuf += sizeof(uint16_t);

        if ( _is_a_records ) {
            uint32_t _a_rec = *(uint32_t *)_pbuf;
            _result_list.emplace_back(sl_ip(_a_rec));
        }
        _pbuf += _rlen;
    }
    return _result_list;
}
void sl_dns_packet::set_A_records(const vector<sl_ip> & a_records)
{
    // Check if has set the query domain
    if ( packet_data_.size() <= (packet_header_size + 2 + 2) ) return;
    if ( a_records.size() == 0 ) return;

    // This packet should be a response
    this->set_is_query_request(false);

    // Set the response code, no error
    this->set_resp_code(sl_dns_rcode_noerr);

    // Update answer count
    ((uint16_t *)&(packet_data_[0]))[3] = htons(a_records.size() + this->get_an_count());

    // All length: incoming packet(header + query domain) + 2bytes domain-name(offset to query domain) + 
    // 2 bytes type(A) + 2 bytes class(IN) + 4 bytes(TTL) + 2bytes(r-length) + 4bytes(r-data, ipaddr)
    size_t _append_size = (2 + 2 + 2 + 4 + 2 + 4) * a_records.size();
    size_t _current_size = packet_data_.size();
    packet_data_.resize(_current_size + _append_size);

    // Offset
    uint16_t _name_offset = packet_header_size;
    _name_offset |= 0xC000;
    _name_offset = htons(_name_offset);

    // Generate the RR
    size_t _boffset = _current_size;
    for ( auto _ip : a_records ) {
        // Name
        uint16_t *_pname = (uint16_t *)(&packet_data_[0] + _boffset);
        *_pname = _name_offset;
        _boffset += sizeof(uint16_t);

        // Type
        uint16_t *_ptype = (uint16_t *)(&packet_data_[0] + _boffset);
        *_ptype = htons((uint16_t)sl_dns_qtype_host);
        _boffset += sizeof(uint16_t);

        // Class
        uint16_t *_pclass = (uint16_t *)(&packet_data_[0] + _boffset);
        *_pclass = htons((uint16_t)sl_dns_qclass_in);
        _boffset += sizeof(uint16_t);

        // TTL
        uint32_t *_pttl = (uint32_t *)(&packet_data_[0] + _boffset);
        *_pttl = htonl(30 * 60);    // 30 mins
        _boffset += sizeof(uint32_t);

        // RLENGTH
        uint16_t *_prlen = (uint16_t *)(&packet_data_[0] + _boffset);
        *_prlen = htons(4);
        _boffset += sizeof(uint16_t);

        // RDATA
        uint32_t *_prdata = (uint32_t *)(&packet_data_[0] + _boffset);
        *_prdata = _ip;
        _boffset += sizeof(uint32_t);
    }
}

// Dump all C-Name Records in the dns packet
const vector<string> sl_dns_packet::get_C_Names() const
{
    string _qdomain(this->get_query_domain());
    vector<string> _result_list;

    if ( _qdomain.size() == 0 ) return _result_list;

    // Get the data offset
    // Header Size + Domain Size(+2) + QType + QClass
    const char *_pbuf = (
        packet_data_.c_str() +      // Start point
        packet_header_size +        // Header Fixed Size
        (_qdomain.size() + 2) +     // Domain Size + 2(start length, and \0)
        2 +                         // QType
        2                           // QClass
        );

    uint16_t _an = this->get_an_count();
    for ( uint16_t i = 0; i < _an; ++i ) {
        string _domain;
        int _dsize = _dns_get_format_domain(_pbuf, packet_data_.c_str(), _domain);
        _pbuf += _dsize;

        // Get the record type
        uint16_t _type = ntohs(*(uint16_t *)_pbuf);
        _pbuf += sizeof(uint16_t);

        bool _is_c_name = ((sl_dns_qtype)_type == sl_dns_qtype_cname);

        // Skip QClass
        _pbuf += sizeof(uint16_t);
        // Skip TTL
        _pbuf += sizeof(uint32_t);

        // Get RData Length
        uint16_t _rlen = ntohs(*(uint16_t *)_pbuf);
        _pbuf += sizeof(uint16_t);

        if ( _is_c_name ) {
            string _cname;
            _dns_get_format_domain(_pbuf, packet_data_.c_str(), _cname);
            _result_list.emplace_back(_cname);
        }
        _pbuf += _rlen;
    }
    return _result_list;
}
// Append C-Name to the end of the dns packet
void sl_dns_packet::set_C_Names(const vector<string> & c_names)
{
    // Check if has set the query domain
    if ( packet_data_.size() <= (packet_header_size + 2 + 2) ) return;
    if ( c_names.size() == 0 ) return;

    // This packet should be a response
    this->set_is_query_request(false);

    // Set the response code, no error
    this->set_resp_code(sl_dns_rcode_noerr);

    // Update answer count
    ((uint16_t *)&(packet_data_[0]))[3] = htons(c_names.size() + this->get_an_count());

    // All length: incoming packet(header + query domain) + 2bytes domain-name(offset to query domain) + 
    // 2 bytes type(A) + 2 bytes class(IN) + 4 bytes(TTL) + 2bytes(r-length) + n-bytes data
    size_t _append_size = 0;
    for ( auto &_name : c_names ) {
        _append_size += (2 + 2 + 2 + 4 + 2 + _name.size());
    }
    size_t _current_size = packet_data_.size();
    packet_data_.resize(_current_size + _append_size);

    // Offset
    uint16_t _name_offset = packet_header_size;
    _name_offset |= 0xC000;
    _name_offset = htons(_name_offset);

    // Generate the RR
    size_t _boffset = _current_size;
    for ( auto _cname : c_names ) {
        // Name
        uint16_t *_pname = (uint16_t *)(&packet_data_[0] + _boffset);
        *_pname = _name_offset;
        _boffset += sizeof(uint16_t);

        // Type
        uint16_t *_ptype = (uint16_t *)(&packet_data_[0] + _boffset);
        *_ptype = htons((uint16_t)sl_dns_qtype_cname);
        _boffset += sizeof(uint16_t);

        // Class
        uint16_t *_pclass = (uint16_t *)(&packet_data_[0] + _boffset);
        *_pclass = htons((uint16_t)sl_dns_qclass_in);
        _boffset += sizeof(uint16_t);

        // TTL
        uint32_t *_pttl = (uint32_t *)(&packet_data_[0] + _boffset);
        *_pttl = htonl(30 * 60);    // 30 mins
        _boffset += sizeof(uint32_t);

        // RLENGTH
        uint16_t *_prlen = (uint16_t *)(&packet_data_[0] + _boffset);
        *_prlen = htons((uint16_t)_cname.size() + 2);
        _boffset += sizeof(uint16_t);

        // RDATA
        string _fcname;
        _dns_format_domain(_cname, _fcname);
        char *_prdata = (char *)(&packet_data_[0] + _boffset);
        memcpy(_prdata, _fcname.c_str(), _fcname.size());
        _boffset += _fcname.size();
    }
}

// Size
size_t sl_dns_packet::size() const { return packet_data_.size(); }
// Buffer Point
const char *const sl_dns_packet::pbuf() { return packet_data_.c_str(); }

// String Cast
sl_dns_packet::operator const string& () const { return packet_data_; }
const string& sl_dns_packet::str() const { return packet_data_; }

const string sl_dns_packet::to_tcp_packet() const
{
    // Initialize an empty packet
    string _packet(2 + packet_data_.size(), '\0');
    *((uint16_t *)&_packet[0]) = htons(packet_data_.size());
    memcpy(&_packet[2], packet_data_.c_str(), packet_data_.size());
    return _packet;
}

#endif

// cleandns.dns.cpp
/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
