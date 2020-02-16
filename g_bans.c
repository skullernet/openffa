/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2009 Andrey Nazarov

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

#include "g_local.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

typedef union {
    uint8_t     u8[16];
    uint16_t    u16[8];
    uint32_t    u32[4];
    uint64_t    u64[2];
} ipaddr_t;

typedef struct {
    list_t      list;
    ipaction_t  action;
    int         family;
    ipaddr_t    addr;
    ipaddr_t    mask;
    time_t      added;
    time_t      duration;
    char        *adder;
} ipfilter_t;

#define MAX_IPFILTERS   1024

#define FOR_EACH_IPFILTER(f) \
    LIST_FOR_EACH(ipfilter_t, f, &ipfilters, list)

#define FOR_EACH_IPFILTER_SAFE(f, n) \
    LIST_FOR_EACH_SAFE(ipfilter_t, f, n, &ipfilters, list)

#define ADDR_BITS(family)   ((family) == AF_INET6 ? 128 : 32)
#define ADDR_SIZE(family)   ((family) == AF_INET6 ?  16 :  4)

static LIST_DECL(ipfilters);
static int  numipfilters;

static void make_mask(ipaddr_t *mask, int bits)
{
    memset(mask, 0, sizeof(*mask));
    memset(mask->u8, 0xff, bits >> 3);
    if (bits & 7) {
        mask->u8[bits >> 3] = 0xff << (-bits & 7);
    }
}

static int parse_filter(const char *s, ipaddr_t *addr, ipaddr_t *mask)
{
    int bits, family;
    char *p, copy[MAX_IPSTR];

    if (Q_strlcpy(copy, s, sizeof(copy)) >= sizeof(copy))
        return 0;

    s = copy;
    p = strchr(s, '/');
    if (p) {
        *p++ = 0;
        if (*p == 0)
            return 0;
        bits = atoi(p);
    } else {
        bits = -1;
    }

    if (inet_pton(AF_INET, s, addr) == 1) {
        family = AF_INET;
    } else if (inet_pton(AF_INET6, s, addr) == 1) {
        family = AF_INET6;
    } else {
        return 0;
    }

    if (bits == -1) {
        bits = ADDR_BITS(family);
    } else {
        if (bits < 0 || bits > ADDR_BITS(family))
            return 0;
    }

    make_mask(mask, bits);

    return family;
}

static void remove_filter(ipfilter_t *ip)
{
    List_Remove(&ip->list);
    if (ip->adder) {
        G_Free(ip->adder);
    }
    G_Free(ip);
    numipfilters--;
}

static void add_filter(ipaction_t action, int family, ipaddr_t *addr,
                       ipaddr_t *mask, time_t duration, edict_t *ent)
{
    ipfilter_t *ip;

    ip = G_Malloc(sizeof(*ip));
    ip->action = action;
    ip->family = family;
    memcpy(&ip->addr, addr, ADDR_SIZE(family));
    memcpy(&ip->mask, mask, ADDR_SIZE(family));
    ip->added = time(NULL);
    ip->duration = duration;
    if (ent) {
        ip->adder = G_CopyString(ent->client->pers.ip);
    } else {
        ip->adder = NULL;
    }
    List_Append(&ipfilters, &ip->list);
    numipfilters++;
}

static int parse_addr(ipaddr_t *addr, const char *s)
{
    char *p, copy[MAX_IPSTR];

    if (Q_strlcpy(copy, s, sizeof(copy)) >= sizeof(copy))
        return 0;

    s = copy;
    if (*s == '[') {
        s++;
        p = strchr(s, ']');
        if (!p)
            return 0;
        *p = 0;
        if (inet_pton(AF_INET6, s, addr) == 1)
            return AF_INET6;
    } else {
        p = strchr(s, ':');
        if (p)
            *p = 0;
        if (inet_pton(AF_INET, s, addr) == 1)
            return AF_INET;
    }

    return 0;
}

/*
=================
G_CheckFilters
=================
*/
ipaction_t G_CheckFilters(const char *s)
{
    int         family;
    ipaddr_t    addr;
    time_t      now;
    ipfilter_t  *ip, *next;

    family = parse_addr(&addr, s);
    if (!family) {
        return IPA_NONE;
    }

    now = time(NULL);

    FOR_EACH_IPFILTER_SAFE(ip, next) {
        if (ip->duration) {
            if (ip->added > now) {
                ip->added = now;
            }
            if (now - ip->added > ip->duration) {
                remove_filter(ip);
                continue;
            }
        }
        if (ip->family != family) {
            continue;
        }
        if (ip->family == AF_INET6) {
            if (!(((addr.u64[0] ^ ip->addr.u64[0]) & ip->mask.u64[0]) |
                  ((addr.u64[1] ^ ip->addr.u64[1]) & ip->mask.u64[1]))) {
                return ip->action;
            }
        } else {
            if (!((addr.u32[0] ^ ip->addr.u32[0]) & ip->mask.u32[0])) {
                return ip->action;
            }
        }
    }

    return IPA_NONE;
}

static time_t parse_duration(const char *s)
{
    time_t sec;
    char *p;

    sec = strtoul(s, &p, 10);
    if (p == s) {
        return -1;
    }
    if (*p == 0 || *p == 'm' || *p == 'M') {
        sec *= 60; // minutes are default
    } else if (*p == 'h' || *p == 'H') {
        sec *= 60 * 60;
    } else if (*p == 'd' || *p == 'D') {
        sec *= 60 * 60 * 24;
    } else {
        return -1;
    }

    return sec;
}

static ipaction_t parse_action(const char *s)
{
    if (!Q_stricmp(s, "ban")) return IPA_BAN;
    if (!Q_stricmp(s, "mute")) return IPA_MUTE;
    if (!Q_stricmp(s, "allow")) return IPA_ALLOW;
    return IPA_NONE;
}

static char *action_to_string(ipaction_t action)
{
    switch (action) {
        default: return NULL;
        case IPA_BAN: return "ban";
        case IPA_MUTE: return "mute";
        case IPA_ALLOW: return "allow";
    }
}

#define DEF_DURATION    (1 * 3600)
#define MAX_DURATION    (12 * 3600)

/*
=================
G_AddIP_f
=================
*/
void G_AddIP_f(edict_t *ent)
{
    ipaddr_t addr, mask;
    ipaction_t action;
    time_t duration;
    int i, start, argc, family;
    char *s;

    start = ent ? 0 : 1;
    argc = gi.argc() - start;

    if (argc < 2) {
        gi.cprintf(ent, PRINT_HIGH, "Usage: %s <ip/mask> [action] [duration]\n", gi.argv(start));
        return;
    }

    if (numipfilters == MAX_IPFILTERS) {
        gi.cprintf(ent, PRINT_HIGH, "IP filter list is full\n");
        return;
    }

    s = gi.argv(start + 1);
    family = parse_filter(s, &addr, &mask);
    if (!family) {
        gi.cprintf(ent, PRINT_HIGH, "Bad filter address: %s\n", s);
        return;
    }

    action = IPA_BAN;
    duration = ent ? DEF_DURATION : 0;

    for (i = 2; i < argc; i++) {
        ipaction_t new_act;
        time_t new_dur;

        s = gi.argv(start + i);
        new_act = parse_action(s);
        if (new_act != IPA_NONE) {
            action = new_act;
            continue;
        }
        new_dur = parse_duration(s);
        if (new_dur >= 0) {
            duration = new_dur;
            continue;
        }
        gi.cprintf(ent, PRINT_HIGH, "Invalid argument: %s\n", s);
        return;
    }

    if (ent) {
        if (!duration) {
            gi.cprintf(ent, PRINT_HIGH, "You may not add permanent bans.\n");
            return;
        }
        if (duration > MAX_DURATION) {
            duration = MAX_DURATION;
        }
    }

    add_filter(action, family, &addr, &mask, duration, ent);
}

/*
=================
G_BanEdict
=================
*/
void G_BanEdict(edict_t *victim, edict_t *initiator)
{
    ipaddr_t addr, mask;
    int family;

    if (numipfilters == MAX_IPFILTERS) {
        return;
    }

    family = parse_addr(&addr, victim->client->pers.ip);
    if (!family) {
        return;
    }

    make_mask(&mask, family == AF_INET6 ? 64 : 32);

    add_filter(IPA_BAN, family, &addr, &mask, DEF_DURATION, initiator);
}

static char *filter_to_string(int family, ipaddr_t *addr, ipaddr_t *mask)
{
    int i;
    for (i = 0; i < ADDR_BITS(family) && mask->u8[i >> 3] & (1 << (7 - (i & 7))); i++)
        ;
    return va("%s/%d", inet_ntop(family, addr, (char [MAX_IPSTR]){}, MAX_IPSTR), i);
}

/*
=================
G_RemoveIP_f
=================
*/
void G_RemoveIP_f(edict_t *ent)
{
    ipaddr_t    addr, mask;
    char        *s;
    ipfilter_t  *ip;
    int         start, argc, family;

    start = ent ? 0 : 1;
    argc = gi.argc() - start;

    if (argc < 2) {
        gi.cprintf(ent, PRINT_HIGH, "Usage: %s <ip/mask>\n", gi.argv(start));
        return;
    }

    s = gi.argv(start + 1);
    family = parse_filter(s, &addr, &mask);
    if (!family) {
        gi.cprintf(ent, PRINT_HIGH, "Bad filter address: %s\n", s);
        return;
    }

    FOR_EACH_IPFILTER(ip) {
        if (ip->family != family ||
            memcmp(&ip->addr, &addr, ADDR_SIZE(family)) ||
            memcmp(&ip->mask, &mask, ADDR_SIZE(family))) {
            continue;
        }
        if (ent && !ip->duration) {
            gi.cprintf(ent, PRINT_HIGH, "You may not remove permanent bans.\n");
            return;
        }
        remove_filter(ip);
        gi.cprintf(ent, PRINT_HIGH, "Removed.\n");
        return;
    }

    gi.cprintf(ent, PRINT_HIGH, "Didn't find %s.\n", filter_to_string(family, &addr, &mask));
}

static char *duration_to_string(time_t t)
{
    int sec, min, hour, day;

    min = t / 60; sec = t % 60;
    hour = min / 60; min %= 60;
    day = hour / 24; hour %= 24;

    if (day) {
        return va("%d+%d:%02d.%02d", day, hour, min, sec);
    }
    if (hour) {
        return va("%d:%02d.%02d", hour, min, sec);
    }
    return va("%02d.%02d", min, sec);
}

/*
=================
G_ListIP_f
=================
*/
void G_ListIP_f(edict_t *ent)
{
    ipfilter_t *ip, *next;
    time_t now;

    now = time(NULL);

    FOR_EACH_IPFILTER_SAFE(ip, next) {
        if (ip->duration) {
            if (ip->added > now) {
                ip->added = now;
            }
            if (now - ip->added > ip->duration) {
                remove_filter(ip);
            }
        }
    }

    if (LIST_EMPTY(&ipfilters)) {
        gi.cprintf(ent, PRINT_HIGH, "Filter list is empty.\n");
        return;
    }

    gi.cprintf(ent, PRINT_HIGH,
               "address            action expires in added by\n"
               "------------------ ------ ---------- ---------------\n");
    FOR_EACH_IPFILTER(ip) {
        gi.cprintf(ent, PRINT_HIGH, "%-18s %6s %10s %s\n",
                   filter_to_string(ip->family, &ip->addr, &ip->mask),
                   action_to_string(ip->action),
                   ip->duration ? duration_to_string(ip->duration - (now - ip->added)) : "permanent",
                   ip->adder ? ip->adder : "console");
    }
}

/*
=================
G_WriteIP_f
=================
*/
void G_WriteIP_f(void)
{
    FILE    *f;
    char    name[MAX_OSPATH];
    size_t  len;
    ipfilter_t  *ip;

    if (!game.dir[0]) {
        gi.cprintf(NULL, PRINT_HIGH, "No gamedir set\n");
        return;
    }

    len = Q_snprintf(name, sizeof(name), "%s/listip.cfg", game.dir);
    if (len >= sizeof(name)) {
        gi.cprintf(NULL, PRINT_HIGH, "Oversize gamedir\n");
        return;
    }

    f = fopen(name, "w");
    if (!f) {
        gi.cprintf(NULL, PRINT_HIGH, "Couldn't open %s\n", name);
        return;
    }

    gi.cprintf(NULL, PRINT_HIGH, "Writing %s.\n", name);

    FOR_EACH_IPFILTER(ip) {
        if (!ip->duration) {
            char *s = filter_to_string(ip->family, &ip->addr, &ip->mask);
            if (ip->action == IPA_BAN) {
                fprintf(f, "sv addip %s\n", s);
            } else {
                fprintf(f, "sv addip %s %s\n", s, action_to_string(ip->action));
            }
        }
    }

    fclose(f);
}
