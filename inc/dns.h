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
 * File Name         : dns.h
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

#pragma once

#ifndef __CLEAN_DNS_DNS_PACKAGE_H__
#define __CLEAN_DNS_DNS_PACKAGE_H__

#define SOCK_LITE_INTEGRATION_DNS

#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <stdio.h>
#include <memory.h>
#include <stdlib.h>

using namespace std;

// DNS Question Type
typedef enum {
    dns_qtype_host          = 0x01,     // Host(A) record
    dns_qtype_ns            = 0x02,     // Name server (NS) record
    dns_qtype_cname         = 0x05,     // Alias(CName) record
    dns_qtype_ptr           = 0x0C,     // Reverse-lookup(PTR) record
    dns_qtype_mx            = 0x0F,     // Mail exchange(MX) record
    dns_qtype_srv           = 0x21,     // Service(SRV) record
    dns_qtype_ixfr          = 0xFB,     // Incremental zone transfer(IXFR) record
    dns_qtype_axfr          = 0xFC,     // Standard zone transfer(AXFR) record
    dns_qtype_all           = 0xFF      // All records
} dns_qtype;

// DNS Question Class
typedef enum {
    dns_qclass_in           = 0x0001,   // Represents the IN(internet) question and is normally set to 0x0001
    dns_qclass_ch           = 0x0003,   // the CHAOS class
    dns_qclass_hs           = 0x0004    // Hesiod   
} dns_qclass;

typedef enum {
    dns_opcode_standard     = 0,
    dns_opcode_inverse      = 1,
    dns_opcode_status       = 2,
    dns_opcode_reserved_3   = 3,    // not use
    dns_opcode_notify       = 4,        // in RFC 1996
    dns_opcode_update       = 5         // in RFC 2136
} dns_opcode;

typedef enum {
    dns_rcode_noerr             = 0,
    dns_rcode_format_error      = 1,
    dns_rcode_server_failure    = 2,
    dns_rcode_name_error        = 3,
    dns_rcode_not_impl          = 4,
    dns_rcode_refuse            = 5,
    dns_rcode_yxdomain          = 6,
    dns_rcode_yxrrset           = 7,
    dns_rcode_nxrrset           = 8,
    dns_rcode_notauth           = 9,
    dns_rcode_notzone           = 10,
    dns_rcode_badvers           = 16,
    dns_rcode_badsig            = 16,
    dns_rcode_badkey            = 17,
    dns_rcode_badtime           = 18,
    dns_rcode_badmode           = 19,
    dns_rcode_badname           = 20,
    dns_rcode_badalg            = 21
} dns_rcode;

#pragma pack(push, 1)
class clnd_dns_packet {
protected:
    uint16_t        transaction_id_;
    uint16_t        flags_;
    uint16_t        qd_count_;
    uint16_t        an_count_;
    uint16_t        ns_count_;
    uint16_t        ar_count_;
public:
    // Properties
    uint16_t        get_transaction_id() const;
    bool            get_is_query_request() const;
    bool            get_is_response_request() const;
    dns_opcode      get_opcode() const;
    bool            get_is_authoritative() const;
    void            set_is_authoritative(bool auth = false);
    bool            get_is_truncation() const;
    bool            get_is_recursive_desired() const;
    void            set_is_recursive_desired(bool rd = true);
    bool            get_is_recursive_available() const;
    dns_rcode       get_resp_code() const;

    uint16_t        get_qd_count() const;
    uint16_t        get_an_count() const;
    uint16_t        get_ns_count() const;
    uint16_t        get_ar_count() const;

    clnd_dns_packet( bool is_query = true, dns_opcode opcode = dns_opcode_standard, uint16_t qd_count = 1 );
    clnd_dns_packet( const char *data, uint16_t len );
    clnd_dns_packet( const clnd_dns_packet &rhs );
    clnd_dns_packet& operator= (const clnd_dns_packet &rhs );

    // The size of the packet, should always be 10
    size_t size() const;
    // The buffer point of the packet
    const char *const pbuf();

    clnd_dns_packet *dns_resp_packet(string &buf, dns_rcode rcode, uint16_t ancount = 1) const;
    clnd_dns_packet *dns_truncation_packet( string &buf ) const;

protected:
    static bool clnd_dns_support_recursive;
    static uint16_t clnd_dns_tid;
public:
    static void set_support_recursive( bool ra = true );
};

#pragma pack(pop)

// Get the domain from the dns querying packet.
// The query domain seg will store the domain in the following format:
// [length:1Byte][component][length:1Byte][component]...
int dns_get_domain( const char *pkt, unsigned int len, std::string &domain );

// Generate a query packet
int dns_generate_query_packet( const string &query_name, string& buffer, dns_qtype qtype = dns_qtype_host );

// Generate a tc packet
int dns_generate_tc_packet( const string& incoming_pkt, string& buffer );

// Generate a tcp redirect packet
int dns_generate_tcp_redirect_packet( const string &incoming_pkt, string &buffer );

// Generate a udp redirect packet from tcp response
int dns_generate_udp_response_packet_from_tcp( const string &incoming_pkt, string &buffer );

// Generate the A records response from the received packet
void dns_generate_a_records_resp( const char *pkt, unsigned int len, vector<uint32_t> ipaddress, string &buf );

// Generate response packet for specified query domain
void dns_generate_a_records_resp( 
    const string &query_domain, 
    uint16_t trans_id, 
    const vector<uint32_t> & iplist, 
    string &buf );

// Generate the C Name records response from the received packet
void dns_gnerate_cname_records_resp( const char *pkt, unsigned int len, vector<string> cnamelist, string &buf );

// Get all available A records from a packet
void dns_get_a_records( const char *pkt, unsigned int len, string &qdomain, vector<uint32_t> &a_records );

// Check if is a query request
bool dns_is_query(const char *pkt, unsigned int len);

#endif

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */