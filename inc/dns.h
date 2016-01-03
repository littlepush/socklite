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
#include "socket.h"

using namespace std;

// DNS Question Type
typedef enum {
    sl_dns_qtype_host           = 0x01,     // Host(A) record
    sl_dns_qtype_ns             = 0x02,     // Name server (NS) record
    sl_dns_qtype_cname          = 0x05,     // Alias(CName) record
    sl_dns_qtype_ptr            = 0x0C,     // Reverse-lookup(PTR) record
    sl_dns_qtype_mx             = 0x0F,     // Mail exchange(MX) record
    sl_dns_qtype_srv            = 0x21,     // Service(SRV) record
    sl_dns_qtype_ixfr           = 0xFB,     // Incremental zone transfer(IXFR) record
    sl_dns_qtype_axfr           = 0xFC,     // Standard zone transfer(AXFR) record
    sl_dns_qtype_all            = 0xFF      // All records
} sl_dns_qtype;

// DNS Question Class
typedef enum {
    sl_dns_qclass_in            = 0x0001,   // Represents the IN(internet) question and is normally set to 0x0001
    sl_dns_qclass_ch            = 0x0003,   // the CHAOS class
    sl_dns_qclass_hs            = 0x0004    // Hesiod   
} sl_dns_qclass;

typedef enum {
    sl_dns_opcode_standard      = 0,
    sl_dns_opcode_inverse       = 1,
    sl_dns_opcode_status        = 2,
    sl_dns_opcode_reserved_3    = 3,    // not use
    sl_dns_opcode_notify        = 4,        // in RFC 1996
    sl_dns_opcode_update        = 5         // in RFC 2136
} sl_dns_opcode;

typedef enum {
    sl_dns_rcode_noerr              = 0,
    sl_dns_rcode_format_error       = 1,
    sl_dns_rcode_server_failure     = 2,
    sl_dns_rcode_name_error         = 3,
    sl_dns_rcode_not_impl           = 4,
    sl_dns_rcode_refuse             = 5,
    sl_dns_rcode_yxdomain           = 6,
    sl_dns_rcode_yxrrset            = 7,
    sl_dns_rcode_nxrrset            = 8,
    sl_dns_rcode_notauth            = 9,
    sl_dns_rcode_notzone            = 10,
    sl_dns_rcode_badvers            = 16,
    sl_dns_rcode_badsig             = 16,
    sl_dns_rcode_badkey             = 17,
    sl_dns_rcode_badtime            = 18,
    sl_dns_rcode_badmode            = 19,
    sl_dns_rcode_badname            = 20,
    sl_dns_rcode_badalg             = 21
} sl_dns_rcode;

#pragma pack(push, 1)
class sl_dns_packet {

    enum { packet_header_size = sizeof(uint16_t) * 6 };
protected:
    string          packet_data_;
public:
    // Properties

    // Trans-Action ID
    uint16_t        get_transaction_id() const;
    void            set_transaction_id(uint16_t tid);

    // Request Type
    bool            get_is_query_request() const;
    bool            get_is_response_request() const;
    void            set_is_query_request(bool isqr = true);

    // Operator Code
    sl_dns_opcode   get_opcode() const;
    void            set_opcode(sl_dns_opcode opcode = sl_dns_opcode_standard);

    // If this is an authoritative answer
    bool            get_is_authoritative() const;
    void            set_is_authoritative(bool auth = false);

    // If current packet is truncation
    bool            get_is_truncation() const;
    void            set_is_truncation(bool trunc = true);

    // If the request need recursive query.
    bool            get_is_recursive_desired() const;
    void            set_is_recursive_desired(bool rd = true);

    // If current server support recursive query
    bool            get_is_recursive_available() const;
    void            set_is_recursive_available(bool recursive = true);

    // Get the response code
    sl_dns_rcode    get_resp_code() const;
    void            set_resp_code(sl_dns_rcode rcode = sl_dns_rcode_noerr);

    uint16_t        get_qd_count() const;
    uint16_t        get_an_count() const;
    uint16_t        get_ns_count() const;
    uint16_t        get_ar_count() const;

    // Constructures
    sl_dns_packet();
    sl_dns_packet(const sl_dns_packet& rhs);
    sl_dns_packet(const sl_dns_packet&& rrhs);
    sl_dns_packet(const string& packet, bool is_tcp_packet = false);
    sl_dns_packet(uint16_t trans_id, const string& query_domain);

    // Check if the packet is a validate dns packet
    operator bool() const;
    bool is_validate_query() const;
    bool is_validate_response() const;

    // Operators
    sl_dns_packet& operator = (const sl_dns_packet& rhs);
    sl_dns_packet& operator = (const sl_dns_packet&& rhs);

    // Parse the query domain
    // The query domain seg will store the domain in the following format:
    // [length:1Byte][component][length:1Byte][component]...
    const string get_query_domain() const;
    // This method will auto increase the packet size
    void set_query_domain(const string& domain, sl_dns_qtype qtype = sl_dns_qtype_host, sl_dns_qclass qclass = sl_dns_qclass_in);

    // Dump all A-Records in the dns packet
    const vector<sl_ip> get_A_records() const;
    // Add a records to the end of the dns packet
    void set_A_records(const vector<sl_ip> & a_records);

    // Dump all C-Name Records in the dns packet
    const vector<string> get_C_Names() const;
    // Append C-Name to the end of the dns packet
    void set_C_Names(const vector<string> & c_names);

    // The size of the packet
    size_t size() const;
    // The buffer point of the packet
    const char *const pbuf();

    // Cast to string
    operator const string&() const;
    const string& str() const;

    // Convert current packet to tcp packet
    const string to_tcp_packet() const;
};

#pragma pack(pop)

#endif

/*
 Push Chen.
 littlepush@gmail.com
 http://pushchen.com
 http://twitter.com/littlepush
 */