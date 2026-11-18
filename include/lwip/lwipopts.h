#pragma once
/* Entire stack belongs to one network worker. No socket or netconn APIs.
 * Both control blocks and variable-size requests use compile-time pools. The
 * malloc pool does not grow, use libc malloc, or fall back to a backing heap.
 */
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 0
#define LWIP_UDP 1
#define LWIP_RAW 0
#define LWIP_DHCP 0
#define LWIP_DNS 0
#define LWIP_AUTOIP 0
#define LWIP_IGMP 0
#define LWIP_ARP 1
#define LWIP_ICMP 1
#define LWIP_ACD 0
#define IP_REASSEMBLY 0
#define IP_FRAG 0
#define MEM_LIBC_MALLOC 0
#define MEMP_MEM_MALLOC 0
#define MEM_USE_POOLS 1
#define MEMP_USE_CUSTOM_POOLS 1
#define MEM_USE_POOLS_TRY_BIGGER_POOL 0
#define MEM_ALIGNMENT 8
#define MEMP_NUM_UDP_PCB 2
#define MEMP_NUM_SYS_TIMEOUT 8
#define PBUF_POOL_SIZE 4
#define PBUF_POOL_BUFSIZE 1600
#define ARP_TABLE_SIZE 4
#define ARP_QUEUEING 0
#define LWIP_NETIF_HOSTNAME 0
#define LWIP_NETIF_STATUS_CALLBACK 0
#define LWIP_NETIF_LINK_CALLBACK 0
#define LWIP_STATS 1
#define MEM_STATS 1
#define MEMP_STATS 1
#define LWIP_STATS_DISPLAY 0
