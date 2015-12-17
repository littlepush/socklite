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

bool clnd_dns_packet::clnd_dns_support_recursive = true;
uint16_t clnd_dns_packet::clnd_dns_tid = 1;

// DNS Package class
clnd_dns_packet::clnd_dns_packet( const char *data, uint16_t len )
{
    memcpy((void *)&transaction_id_, data, sizeof(clnd_dns_packet));
}

clnd_dns_packet::clnd_dns_packet( bool is_query, dns_opcode opcode, uint16_t qd_count)
    : transaction_id_(clnd_dns_tid++), flags_(0), 
    qd_count_( htons(qd_count) ), 
    an_count_(0), ns_count_(0), ar_count_(0) 
{
    uint16_t _h_flag = ntohs(flags_);

    // qr
    if ( !is_query ) _h_flag |= 0x8000;
    else _h_flag &= 0x7FFF;

    // opcode
    uint16_t _op_flag = (uint16_t)opcode;
    _op_flag <<= 13;
    _h_flag |= _op_flag;

    // RD
    _h_flag |= 0x0100;

    flags_ = htons(_h_flag);
};

clnd_dns_packet::clnd_dns_packet( const clnd_dns_packet &rhs )
    : transaction_id_(rhs.transaction_id_), flags_(rhs.flags_),
    qd_count_(rhs.qd_count_), an_count_(rhs.an_count_),
    ns_count_(rhs.ns_count_), ar_count_(rhs.ar_count_) { }

clnd_dns_packet& clnd_dns_packet::operator= (const clnd_dns_packet &rhs )
{
    transaction_id_ = rhs.transaction_id_;
    flags_ = rhs.flags_;
    qd_count_ = rhs.qd_count_;
    an_count_ = rhs.an_count_;
    ns_count_ = rhs.ns_count_;
    ar_count_ = rhs.ar_count_;
    return *this;
}
size_t clnd_dns_packet::size() const { return sizeof(uint16_t) * 5; }
const char *const clnd_dns_packet::pbuf() { return (char *)this; }

clnd_dns_packet * clnd_dns_packet::dns_resp_packet(string &buf, dns_rcode rcode, uint16_t ancount) const {
    buf.resize(sizeof(clnd_dns_packet));
    //clnd_dns_packet *_presp = new ((void*)&buf[0]) clnd_dns_packet(*this);
    clnd_dns_packet *_presp = (clnd_dns_packet *)&buf[0];
    *_presp = *this;
    uint16_t _h_flag = ntohs(flags_);
    // Query -> Response
    _h_flag |= 0x8000;
    // RCode
    (_h_flag &= 0xFFF0) |= ((uint16_t)rcode & 0x000F);
    _presp->flags_ = htons(_h_flag);
    // Answer Count
    _presp->an_count_ = htons(ancount);
    return _presp;
}

clnd_dns_packet * clnd_dns_packet::dns_truncation_packet( string &buf ) const {
    buf.resize(sizeof(clnd_dns_packet));
    clnd_dns_packet *_presp = (clnd_dns_packet *)&buf[0];
    *_presp = *this;
    uint16_t _h_flag = ntohs(flags_);
    // Query -> Response
    _h_flag |= 0x8000;
    // Truncation
    _h_flag |= 0x0200;
    _presp->flags_ = htons(_h_flag);
    return _presp;
}

uint16_t clnd_dns_packet::get_transaction_id() const 
{
    return ntohs(transaction_id_);
}
bool clnd_dns_packet::get_is_query_request() const
{
    uint16_t _h_flag = ntohs(flags_);
    return (_h_flag & 0x8000) == 0;
}
bool clnd_dns_packet::get_is_response_request() const
{
    uint16_t _h_flag = ntohs(flags_);
    return (_h_flag & 0x8000) > 0;
}
dns_opcode clnd_dns_packet::get_opcode() const
{
    uint16_t _h_flag = ntohs(flags_);
    return (dns_opcode)((_h_flag >>= 13) & 0x000F);
}
bool clnd_dns_packet::get_is_authoritative() const
{
    uint16_t _h_flag = ntohs(flags_);
    return (_h_flag & 0x0400) > 0;
}
void clnd_dns_packet::set_is_authoritative(bool auth)
{
    uint16_t _h_flag = ntohs(flags_);
    _h_flag |= 0x0400;
    flags_ = htons(_h_flag);
}
bool clnd_dns_packet::get_is_truncation() const
{
    uint16_t _h_flag = ntohs(flags_);
    return (_h_flag & 0x0200) > 0;
}
bool clnd_dns_packet::get_is_recursive_desired() const
{
    uint16_t _h_flag = ntohs(flags_);
    return (_h_flag & 0x0100) > 0;
}
void clnd_dns_packet::set_is_recursive_desired(bool rd)
{
    uint16_t _h_flag = ntohs(flags_);
    _h_flag |= 0x0100;
    flags_ = htons(_h_flag);
}
bool clnd_dns_packet::get_is_recursive_available() const
{
    uint16_t _h_flag = ntohs(flags_);
    return (_h_flag & 0x0080) > 0;
}
dns_rcode clnd_dns_packet::get_resp_code() const
{
    uint16_t _h_flag = ntohs(flags_);
    return (dns_rcode)(_h_flag & 0x000F);
}
uint16_t clnd_dns_packet::get_qd_count() const
{
    return ntohs(qd_count_);
}
uint16_t clnd_dns_packet::get_an_count() const
{
    return ntohs(an_count_);
}
uint16_t clnd_dns_packet::get_ns_count() const
{
    return ntohs(ns_count_);
}
uint16_t clnd_dns_packet::get_ar_count() const
{
    return ntohs(ar_count_);
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

int _dns_get_format_domain( const char *begin_of_domain, const char *begin_of_pkt, string &domain ) {
    domain.clear();
    int _readsize = 0;
    for ( ;; ) {
        uint8_t _l = begin_of_domain[_readsize];
        _readsize += 1;
        if ( _l & 0xC0 ) {
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

// Get the domain from the dns querying packet.
// The query domain seg will store the domain in the following format:
// [length:1Byte][component][length:1Byte][component]...
int dns_get_domain( const char *pkt, unsigned int len, std::string &domain )
{
    // the packet is too small
    if ( len < sizeof(clnd_dns_packet) ) return -1;
    const char *_pDomain = pkt + sizeof(clnd_dns_packet);
    _dns_get_format_domain(_pDomain, domain);
    return 0;
}

int dns_generate_query_packet( const string &query_name, string& buffer, dns_qtype qtype ) 
{
    string _fdomain;
    _dns_format_domain(query_name, _fdomain);

    // Buffer
    buffer.resize(sizeof(clnd_dns_packet) + _fdomain.size() + 2 * sizeof(uint8_t) + 2 * sizeof(uint8_t));
    // Header
    //clnd_dns_packet *_pquery = (clnd_dns_packet*)&buffer[0]);
    clnd_dns_packet _query_pkt;
    memcpy(&buffer[0], _query_pkt.pbuf(), _query_pkt.size());
    // Domain
    char *_data_area = (char *)&buffer[0] + sizeof(clnd_dns_packet);
    memcpy(_data_area, _fdomain.c_str(), _fdomain.size());
    // Flags
    _data_area += _fdomain.size();
    uint16_t *_flag_area = (uint16_t *)_data_area;
    _flag_area[0] = htons(qtype);
    _flag_area[1] = htons(dns_qclass_in);
    return buffer.size();
}

int dns_generate_tc_packet( const string& incoming_pkt, string& buffer ) 
{
    clnd_dns_packet _ipkt(incoming_pkt.c_str(), sizeof(clnd_dns_packet));
    _ipkt.dns_truncation_packet(buffer);
    buffer.resize(incoming_pkt.size());
    memcpy((char *)&buffer[0] + sizeof(clnd_dns_packet), 
        incoming_pkt.c_str() + sizeof(clnd_dns_packet),
        incoming_pkt.size() - sizeof(clnd_dns_packet));
    return buffer.size();
}

int dns_generate_tcp_redirect_packet( const string &incoming_pkt, string &buffer )
{
    buffer.resize(incoming_pkt.size() + sizeof(uint16_t));
    uint16_t *_plen = (uint16_t *)&buffer[0];
    *_plen = htons(incoming_pkt.size());
    memcpy((char *)&buffer[2], incoming_pkt.c_str(), incoming_pkt.size());
    return buffer.size();
}

int dns_generate_udp_response_packet_from_tcp( const string &incoming_pkt, string &buffer )
{
    buffer.resize(incoming_pkt.size() - sizeof(uint16_t));
    memcpy((char *)&buffer[0], incoming_pkt.c_str() + sizeof(uint16_t), incoming_pkt.size() - sizeof(uint16_t));
    return buffer.size();
}

void dns_generate_a_records_resp( const char *pkt, unsigned int len, vector<uint32_t> ipaddress, string &buf )
{
    // Resp Header
    clnd_dns_packet _ipkt(pkt, len);
    _ipkt.dns_resp_packet(buf, dns_rcode_noerr, (uint16_t)ipaddress.size());

    // All length: incoming packet(header + query domain) + 2bytes domain-name(offset to query domain) + 
    // 2 bytes type(A) + 2 bytes class(IN) + 4 bytes(TTL) + 2bytes(r-length) + 4bytes(r-data, ipaddr)
    buf.resize( len + (2 + 2 + 2 + 4 + 2 + 4) * ipaddress.size() );
    // Query Domain
    memcpy(
        &buf[0] + sizeof(clnd_dns_packet), 
        pkt + sizeof(clnd_dns_packet),
        len - sizeof(clnd_dns_packet)
        );
    // Offset
    uint16_t _name_offset = sizeof(clnd_dns_packet);
    _name_offset |= 0xC000;
    _name_offset = htons(_name_offset);
    // Generate the RR
    size_t _boffset = len;
    for ( auto _ip : ipaddress ) {
        // Name
        uint16_t *_pname = (uint16_t *)(&buf[0] + _boffset);
        *_pname = _name_offset;
        _boffset += sizeof(uint16_t);

        // Type
        uint16_t *_ptype = (uint16_t *)(&buf[0] + _boffset);
        *_ptype = htons((uint16_t)dns_qtype_host);
        _boffset += sizeof(uint16_t);

        // Class
        uint16_t *_pclass = (uint16_t *)(&buf[0] + _boffset);
        *_pclass = htons((uint16_t)dns_qclass_in);
        _boffset += sizeof(uint16_t);

        // TTL
        uint32_t *_pttl = (uint32_t *)(&buf[0] + _boffset);
        *_pttl = htonl(30 * 60);    // 30 mins
        _boffset += sizeof(uint32_t);

        // RLENGTH
        uint16_t *_prlen = (uint16_t *)(&buf[0] + _boffset);
        *_prlen = htons(4);
        _boffset += sizeof(uint16_t);

        // RDATA
        uint32_t *_prdata = (uint32_t *)(&buf[0] + _boffset);
        *_prdata = htonl((uint32_t)_ip);
        _boffset += sizeof(uint32_t);
    }
}

// Generate response packet for specified query domain
void dns_generate_a_records_resp( 
    const string &query_domain, 
    uint16_t trans_id, 
    const vector<uint32_t> & iplist, 
    string &buf )
{
    string _query_pkt;
    dns_generate_query_packet(query_domain, _query_pkt);
    clnd_dns_packet *_pkt = (clnd_dns_packet *)&_query_pkt[0];
    uint16_t* _ptid = (uint16_t *)_pkt;
    *_ptid = htons(trans_id);
    dns_generate_a_records_resp(_query_pkt.c_str(), _query_pkt.size(), iplist, buf);
}

void dns_gnerate_cname_records_resp( const char *pkt, unsigned int len, vector<string> cnamelist, string &buf )
{
    // Resp Header
    clnd_dns_packet _ipkt(pkt, len);
    _ipkt.dns_resp_packet(buf, dns_rcode_noerr, (uint16_t)cnamelist.size());

    // All length: incoming packet(header + query domain) + 2bytes domain-name(offset to query domain) + 
    // 2 bytes type(CName) + 2 bytes class(IN) + 4 bytes(TTL) + 2bytes(r-length) + r-length bytes(r-data, cname)
    //buf.resize( len + (2 + 2 + 2 + 4 + 2 + 4) * cnamelist.size() );
    buf.resize(len);
    // Query Domain
    memcpy(
        &buf[0] + sizeof(clnd_dns_packet), 
        pkt + sizeof(clnd_dns_packet),
        len - sizeof(clnd_dns_packet)
        );
    // Offset
    uint16_t _name_offset = sizeof(clnd_dns_packet);
    _name_offset |= 0xC000;
    _name_offset = htons(_name_offset);
    // Generate the RR
    size_t _boffset = len;
    for ( auto _cname : cnamelist ) {
        buf.resize(buf.size() + 2 + 2 + 2 + 4 + 2 + (_cname.size() + 2));
        // Name
        uint16_t *_pname = (uint16_t *)(&buf[0] + _boffset);
        *_pname = _name_offset;
        _boffset += sizeof(uint16_t);

        // Type
        uint16_t *_ptype = (uint16_t *)(&buf[0] + _boffset);
        *_ptype = htons((uint16_t)dns_qtype_cname);
        _boffset += sizeof(uint16_t);

        // Class
        uint16_t *_pclass = (uint16_t *)(&buf[0] + _boffset);
        *_pclass = htons((uint16_t)dns_qclass_in);
        _boffset += sizeof(uint16_t);

        // TTL
        uint32_t *_pttl = (uint32_t *)(&buf[0] + _boffset);
        *_pttl = htonl(30 * 60);    // 30 mins
        _boffset += sizeof(uint32_t);

        // RLENGTH
        uint16_t *_prlen = (uint16_t *)(&buf[0] + _boffset);
        *_prlen = htons((uint16_t)_cname.size() + 2);
        _boffset += sizeof(uint16_t);

        // RDATA
        string _fcname;
        _dns_format_domain(_cname, _fcname);
        char *_prdata = (char *)(&buf[0] + _boffset);
        memcpy(_prdata, _fcname.c_str(), _fcname.size());
        _boffset += _fcname.size();
    }
}

void dns_get_a_records( const char *pkt, unsigned int len, string &qdomain, vector<uint32_t> &a_records )
{
    clnd_dns_packet *_pheader = (clnd_dns_packet *)pkt;
    const char *_pDomain = pkt + sizeof(clnd_dns_packet);
    int _readsize = _dns_get_format_domain(_pDomain, pkt, qdomain);

    // Begin Answer Point
    const char *_pbuf = pkt + sizeof(clnd_dns_packet) + _readsize + 2 + 2; // type + class
    uint16_t _an_count = _pheader->get_an_count();
    for ( uint16_t i = 0; i < _an_count; ++i ) {
        string _adomain;
        int _asize = _dns_get_format_domain(_pbuf, pkt, _adomain);
        _pbuf += _asize;

        uint16_t _type = ntohs(*(uint16_t *)_pbuf);
        _pbuf += sizeof(uint16_t);

        bool _need_a_records = ((dns_qtype)_type == dns_qtype_host);
        // skip class
        _pbuf += sizeof(uint16_t);
        // skip ttl
        _pbuf += sizeof(uint32_t);
        // length
        uint16_t _rlen = ntohs(*(uint16_t *)_pbuf);
        _pbuf += sizeof(uint16_t);

        if ( _need_a_records ) {
            uint32_t _a_rec = *(uint32_t *)_pbuf;
            a_records.push_back(_a_rec);
        }
        _pbuf += _rlen;
    }
}

// Check if is a query request
bool dns_is_query(const char *pkt, unsigned int len)
{
    clnd_dns_packet *_pheader = (clnd_dns_packet *)pkt;
    return _pheader->get_is_query_request();
}

#endif

// cleandns.dns.cpp
/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */
