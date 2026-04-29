/*
* Copyright (c) 1998-2016 Apple Inc. All rights reserved.
*
* @APPLE_OSREFERENCE_LICENSE_HEADER_START@
*
* This file contains Original Code and/or Modifications of Original Code
* as defined in and that are subject to the Apple Public Source License
* Version 2.0 (the 'License'). You may not use this file except in
* compliance with the License. The rights granted to you under the License
* may not be used to create, or enable the creation or redistribution of,
* unlawful or unlicensed copies of an Apple operating system, or to
* circumvent, violate, or enable the circumvention or violation of, any
* terms of an Apple operating system software license agreement.
*
* Please obtain a copy of the License at
* http://www.opensource.apple.com/apsl/ and read it before using this file.
*
* The Original Code and all software distributed under the License are
* distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
* EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
* INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
* Please see the License for the specific language governing rights and
* limitations under the License.
*
* @APPLE_OSREFERENCE_LICENSE_HEADER_END@
*/

#ifndef apple_private_spi_h
#define apple_private_spi_h

#ifndef __PRIVATE_SPI__

/*! @enum IOMbufServiceClass
    @discussion Service class of a mbuf packet.
    @constant kIOMbufServiceClassBKSYS Background System-Initiated.
    @constant kIOMbufServiceClassBK  Background.
    @constant kIOMbufServiceClassBE  Best Effort.
    @constant kIOMbufServiceClassRD  Responsive Data.
    @constant kIOMbufServiceClassOAM Operations, Administration, and Management.
    @constant kIOMbufServiceClassAV  Multimedia Audio/Video Streaming.
    @constant kIOMbufServiceClassRV  Responsive Multimedia Audio/Video.
    @constant kIOMbufServiceClassVI  Interactive Video.
    @constant kIOMbufServiceClassVO  Interactive Voice.
    @constant kIOMbufServiceClassCTL Network Control.
*/
enum IOMbufServiceClass {
    kIOMbufServiceClassBKSYS    = 100,
    kIOMbufServiceClassBK       = 200,
    kIOMbufServiceClassBE       = 0,
    kIOMbufServiceClassRD       = 300,
    kIOMbufServiceClassOAM      = 400,
    kIOMbufServiceClassAV       = 500,
    kIOMbufServiceClassRV       = 600,
    kIOMbufServiceClassVI       = 700,
    kIOMbufServiceClassVO       = 800,
    kIOMbufServiceClassCTL      = 900
};

#endif

struct packet_info_tag {
    unsigned char reserved0[0x14];
    unsigned int rx_completion_marker;
    unsigned char tid;
    unsigned char reserved19[0x1c - 0x19];
    uint32_t tx_vlan_tag;
    unsigned char reserved20[0x22 - 0x20];
    uint16_t rx_vlan_tag;
    uint32_t rx_drop_marker;
    uint8_t ac_meta;
    unsigned char service_class;
    unsigned char reserved2a[0x48 - 0x2a];
    uint64_t bus_address;
    uint64_t virtual_address;
    unsigned char reserved58[0x74 - 0x58];
    uint32_t packet_signature;
    unsigned char reserved78[0x80 - 0x78];
    uint32_t tx_status;
    unsigned char reserved84[0x8a - 0x84];
    uint16_t flow_queue_idx;
    unsigned char reserved8c[0x90 - 0x8c];
    unsigned char ac_dup_flags;
    unsigned char reserved91[0x98 - 0x91];
};

static_assert(__builtin_offsetof(packet_info_tag, rx_completion_marker) == 0x14,
              "packet_info_tag rx completion marker offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, tid) == 0x18,
              "packet_info_tag TID offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, tx_vlan_tag) == 0x1c,
              "packet_info_tag TX VLAN tag offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, rx_vlan_tag) == 0x22,
              "packet_info_tag RX VLAN tag offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, rx_drop_marker) == 0x24,
              "packet_info_tag RX drop marker offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, ac_meta) == 0x28,
              "packet_info_tag AC meta byte offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, service_class) == 0x29,
              "packet_info_tag service-class offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, bus_address) == 0x48,
              "packet_info_tag bus address offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, virtual_address) == 0x50,
              "packet_info_tag virtual address offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, packet_signature) == 0x74,
              "packet_info_tag packet signature offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, tx_status) == 0x80,
              "packet_info_tag TX status offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, flow_queue_idx) == 0x8a,
              "packet_info_tag flow queue index offset mismatch");
static_assert(__builtin_offsetof(packet_info_tag, ac_dup_flags) == 0x90,
              "packet_info_tag AC/dup flags offset mismatch");
static_assert(sizeof(packet_info_tag) == 0x98,
              "packet_info_tag recovered scratch size mismatch");

struct apple80211_debug_command {
    
};

struct ifnet_init_eparams {
    
};

#endif /* apple_private_spi_h */
