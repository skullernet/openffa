/*
Copyright (C) 2016 Andrey Nazarov

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>

#include "g_local.h"

#ifndef htole16
#define htole16(x)  ((uint16_t)(x))
#define htole32(x)  ((uint32_t)(x))
#define htole64(x)  ((uint64_t)(x))
#endif

#define MAX_PACKETS     16

#define MAX_PACKETLEN   4096
#define HEADER_LEN      16

typedef struct {
    uint32_t    sequence;
    uint32_t    timestamp;
    uint64_t    cookie;
    uint8_t     data[MAX_PACKETLEN - HEADER_LEN];
    unsigned    cursize;
} packet_t;

static cvar_t   *g_udp_host;
static cvar_t   *g_udp_port;
static cvar_t   *g_udp_cookie;

static packet_t packets[MAX_PACKETS];
static int      packet_head;
static int      packet_tail;

static int      sock_fd = -1;

static struct sockaddr_in   sv_addr;

static unsigned     generate_framenum;
static unsigned     generate_sequence;

static unsigned     transmit_framenum;
static unsigned     transmit_backoff;

static unsigned     resolve_framenum;
static unsigned     current_framenum;

static uint8_t      s_data[MAX_PACKETLEN - HEADER_LEN];
static unsigned     s_cursize;

static void write_data(const void *data, size_t len)
{
    len = min(len, sizeof(s_data) - s_cursize);
    memcpy(s_data + s_cursize, data, len);
    s_cursize += len;
}

static void write_u8(uint8_t v)
{
    write_data(&v, sizeof(v));
}

static void write_u16(unsigned val)
{
    uint16_t v = htole16(min(val, UINT16_MAX));
    write_data(&v, sizeof(v));
}

static void write_s16(int val)
{
    uint16_t v = htole16(clamp(val, INT16_MIN, INT16_MAX));
    write_data(&v, sizeof(v));
}

static void write_str(const char *s)
{
    write_data(s, strlen(s) + 1);
}

static void log_client(gclient_t *c)
{
    fragstat_t *fs;
    itemstat_t *is;
    int i, v;

    write_str(c->pers.netname);
    write_u16((level.framenum - c->resp.enter_framenum) / HZ);
    write_s16(c->resp.score);
    write_u16(c->resp.deaths);
    write_u16(c->resp.damage_given);
    write_u16(c->resp.damage_recvd);

    for (i = 0, fs = c->resp.frags; i < FRAG_TOTAL; i++, fs++) {
        v = 0;
        if (fs->kills)    v |=  1;
        if (fs->deaths)   v |=  2;
        if (fs->suicides) v |=  4;
        if (fs->atts)     v |=  8;
        if (fs->hits)     v |= 16;
        if (!v)
            continue;

        if ((fs->kills | fs->deaths | fs->suicides) > 0xffu)
            v |= 32;
        if ((fs->atts | fs->hits) > 0xffu)
            v |= 64;

        write_u8(i);
        write_u8(v);

        if (v & 32) {
            if (fs->kills)    write_u16(fs->kills);
            if (fs->deaths)   write_u16(fs->deaths);
            if (fs->suicides) write_u16(fs->suicides);
        } else {
            if (fs->kills)    write_u8(fs->kills);
            if (fs->deaths)   write_u8(fs->deaths);
            if (fs->suicides) write_u8(fs->suicides);
        }

        if (v & 64) {
            if (fs->atts)     write_u16(fs->atts);
            if (fs->hits)     write_u16(fs->hits);
        } else {
            if (fs->atts)     write_u8(fs->atts);
            if (fs->hits)     write_u8(fs->hits);
        }
    }

    write_u8(0xff);

    for (i = 0, is = c->resp.items; i < ITEM_TOTAL; i++, is++) {
        v = 0;
        if (is->pickups) v |= 1;
        if (is->misses)  v |= 2;
        if (is->kills)   v |= 4;
        if (!v)
            continue;

        if ((is->pickups | is->misses | is->kills) > 0xffu)
            v |= 8;

        write_u8(i);
        write_u8(v);

        if (v & 8) {
            if (is->pickups) write_u16(is->pickups);
            if (is->misses)  write_u16(is->misses);
            if (is->kills)   write_u16(is->kills);
        } else {
            if (is->pickups) write_u8(is->pickups);
            if (is->misses)  write_u8(is->misses);
            if (is->kills)   write_u8(is->kills);
        }
    }

    write_u8(0xff);
}

static void resolve(void)
{
    char buffer[INET_ADDRSTRLEN];
    struct addrinfo hints, *res;
    int ret;

    if (sv_addr.sin_family && current_framenum - resolve_framenum < 24 * 60 * 60 * HZ)
        return;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    ret = getaddrinfo(g_udp_host->string, g_udp_port->string, &hints, &res);
    if (ret) {
        gi.dprintf("[UDP] Couldn't resolve address: %s\n", gai_strerror(ret));
        return;
    }

    resolve_framenum = current_framenum;

    memcpy(&sv_addr, res->ai_addr, sizeof(sv_addr));
    freeaddrinfo(res);

    gi.dprintf("[UDP] Stats server resolved to %s:%d\n",
               inet_ntop(AF_INET, &sv_addr.sin_addr, buffer, sizeof(buffer)),
               ntohs(sv_addr.sin_port));
}

static void udp_log(gclient_t *c)
{
    int i;

    if (sock_fd == -1)
        return;

    if (!game.clients)
        return;

    if (c) {
        log_client(c);
        return;
    }

    resolve();

    for (i = 0, c = game.clients; i < game.maxclients; i++, c++)
        if (c->pers.connected == CONN_SPAWNED)
            log_client(c);
}

static void udp_open(void)
{
    int val;

    g_udp_host = gi.cvar("g_udp_host", "", CVAR_LATCH);
    g_udp_port = gi.cvar("g_udp_port", "27999", CVAR_LATCH);
    g_udp_cookie = gi.cvar("g_udp_cookie", "", 0);

    if (!g_udp_host->string[0])
        return;

    resolve();

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd == -1) {
        gi.dprintf("[UDP] Error opening socket: %s\n", strerror(errno));
        return;
    }

    if ((val = fcntl(sock_fd, F_GETFL, 0)) < 0 || fcntl(sock_fd, F_SETFL, val | O_NONBLOCK) < 0) {
        gi.dprintf("[UDP] Couldn't make socket non-blocking: %s\n", strerror(errno));
        close(sock_fd);
        sock_fd = -1;
        return;
    }

#ifdef IP_MTU_DISCOVER
    val = IP_PMTUDISC_DONT;
    if (setsockopt(sock_fd, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val)) < 0)
        gi.dprintf("[UDP] Couldn't disable path MTU discovery: %s\n", strerror(errno));
#endif

    transmit_backoff = 1 * HZ;
}

static void udp_close(void)
{
    if (sock_fd != -1) {
        close(sock_fd);
        sock_fd = -1;
    }

    sv_addr.sin_family = 0;

    packet_head = 0;
    packet_tail = 0;

    transmit_framenum = 0;
    resolve_framenum  = 0;
    current_framenum  = 0;

    generate_framenum = 0;
    generate_sequence = 0;

    s_cursize = 0;
}

static void receive(void)
{
    uint8_t buffer[HEADER_LEN];
    struct sockaddr_in addr;
    socklen_t addrlen;
    packet_t *p;
    ssize_t ret;

    while (1) {
        addrlen = sizeof(addr);
        ret = recvfrom(sock_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, &addrlen);
        if (ret < 0)
            break;
        if (ret < HEADER_LEN)
            continue;

        if (addr.sin_family != sv_addr.sin_family)
            continue;
        if (addr.sin_addr.s_addr != sv_addr.sin_addr.s_addr)
            continue;
        if (addr.sin_port != sv_addr.sin_port)
            continue;

        if (packet_tail == packet_head)
            continue;

        p = &packets[packet_tail];
        if (memcmp(buffer, p, HEADER_LEN))
            continue;

        packet_tail = (packet_tail + 1) & (MAX_PACKETS - 1);
        transmit_backoff = 1 * HZ;
    }
}

static void generate(void)
{
    packet_t *p;

    if (!s_cursize)
        return;
    if (current_framenum - generate_framenum < 1 * HZ)
        return;

    generate_framenum = current_framenum;
    generate_sequence++;

    p = &packets[packet_head];
    p->sequence  = htole32(generate_sequence);
    p->timestamp = htole32(time(NULL));
    p->cookie    = htole64(strtoull(g_udp_cookie->string, NULL, 0));
    memcpy(p->data, s_data, s_cursize);
    p->cursize = s_cursize + HEADER_LEN;

    packet_head = (packet_head + 1) & (MAX_PACKETS - 1);
    if (packet_head == packet_tail)
        packet_tail = (packet_tail + 1) & (MAX_PACKETS - 1);

    s_cursize = 0;
}

static void transmit(void)
{
    packet_t *p;
    ssize_t ret;

    if (!sv_addr.sin_family)
        return;
    if (packet_tail == packet_head)
        return;
    if (current_framenum - transmit_framenum < transmit_backoff)
        return;

    transmit_framenum = current_framenum;
    transmit_backoff = min(transmit_backoff + 1 * HZ, 120 * HZ);

    p = &packets[packet_tail];
    ret = sendto(sock_fd, p, p->cursize, 0, (const struct sockaddr *)&sv_addr, sizeof(sv_addr));
    if (ret < 0)
        gi.dprintf("[UDP] Error sending packet: %s\n", strerror(errno));
}

static void udp_run(void)
{
    if (sock_fd == -1)
        return;

    receive();
    generate();
    transmit();

    current_framenum++;
}

const database_t g_db_udp = {
    .name = "udp",
    .open = udp_open,
    .close = udp_close,
    .run = udp_run,
    .log = udp_log,
};
