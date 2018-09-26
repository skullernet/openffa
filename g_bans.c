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

/*
==============================================================================

PACKET FILTERING


You can add or remove addresses from the filter list with:

addip <ip>
removeip <ip>

The ip address is specified in dot format, and any unspecified digits will match
any value, so you can specify an entire class C network with "addip 192.246.40".

Removeip will only remove an address specified exactly the same way.  You cannot
addip a subnet, then removeip a single host.

listip
Prints the current list of filters.

writeip
Dumps "addip <ip>" commands to listip.cfg so it can be execed at a later date.
The filter lists are not saved and restored by default, because I beleive
it would cause too much confusion.

filterban <0 or 1>

If 1 (the default), then ip addresses matching the current list will be prohibited
from entering the game.  This is the default setting.

If 0, then only addresses matching the list will be allowed.  This lets you easily
set up a private game, or a game that only allows players from your local network.


==============================================================================
*/

typedef struct {
    list_t      list;
    ipaction_t  action;
    unsigned    mask, compare;
    time_t      added;
    unsigned    duration;
    char        adder[32];
} ipfilter_t;

#define MAX_IPFILTERS   1024

#define FOR_EACH_IPFILTER(f) \
    LIST_FOR_EACH(ipfilter_t, f, &ipfilters, list)

#define FOR_EACH_IPFILTER_SAFE(f, n) \
    LIST_FOR_EACH_SAFE(ipfilter_t, f, n, &ipfilters, list)

static LIST_DECL(ipfilters);
static int      numipfilters;

//extern cvar_t   *filterban;

static bool parse_filter(const char *s, unsigned *mask, unsigned *compare)
{
    int     i;
    byte    b[4] = { 0 };
    byte    m[4] = { 0 };
    char    *p;

    for (i = 0; i < 4; i++) {
        b[i] = strtoul(s, &p, 10);
        if (s == p) {
            return false;
        }

        if (b[i] != 0)
            m[i] = 255;

        if (!*p)
            break;
        s = p + 1;
    }

    *mask = *(unsigned *)m;
    *compare = *(unsigned *)b;

    return true;
}

static void remove_filter(ipfilter_t *ip)
{
    List_Remove(&ip->list);
    G_Free(ip);
    numipfilters--;
}

static void add_filter(ipaction_t action, unsigned mask, unsigned compare, unsigned duration, edict_t *ent)
{
    ipfilter_t *ip;
    char *s;

    ip = G_Malloc(sizeof(*ip));
    ip->action = action;
    ip->mask = mask;
    ip->compare = compare;
    ip->added = time(NULL);
    ip->duration = duration;
    if (ent) {
        strcpy(ip->adder, ent->client->pers.ip);
        s = strchr(ip->adder, ':');
        if (s) {
            *s = 0;
        }
    } else {
        strcpy(ip->adder, "console");
    }
    List_Append(&ipfilters, &ip->list);
    numipfilters++;
}

/*
=================
G_CheckFilters
=================
*/
ipaction_t G_CheckFilters(char *s)
{
    int         i;
    unsigned    in;
    byte        m[4] = { 0 };
    char        *p;
    time_t      now;
    ipfilter_t  *ip, *next;

    for (i = 0; i < 4; i++) {
        m[i] = strtoul(s, &p, 10);
        if (s == p || !*p || *p == ':')
            break;
        s = p + 1;
    }

    in = *(unsigned *)m;

    now = time(NULL);

    FOR_EACH_IPFILTER_SAFE(ip, next) {
        if (ip->duration && now - ip->added > ip->duration) {
            remove_filter(ip);
            continue;
        }
        if ((in & ip->mask) == ip->compare) {
            return ip->action; //(int)filterban->value ? ip->action : IPA_NONE;
        }
    }

    return IPA_NONE; //(int)filterban->value ? IPA_NONE : IPA_BAN;
}

static unsigned parse_duration(const char *s)
{
    unsigned sec;
    char *p;

    sec = strtoul(s, &p, 10);
    if (*p == 0 || *p == 'm' || *p == 'M') {
        sec *= 60; // minutes are default
    } else if (*p == 'h' || *p == 'H') {
        sec *= 60 * 60;
    } else if (*p == 'd' || *p == 'D') {
        sec *= 60 * 60 * 24;
    }

    return sec;
}

static ipaction_t parse_action(const char *s)
{
    if (!Q_stricmp(s, "ban")) {
        return IPA_BAN;
    }
    if (!Q_stricmp(s, "mute")) {
        return IPA_MUTE;
    }
    return IPA_NONE;
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
    unsigned mask, compare;
    unsigned duration;
    ipaction_t action;
    int start, argc;
    char *s;

    start = ent ? 0 : 1;
    argc = gi.argc() - start;

    if (argc < 2) {
        gi.cprintf(ent, PRINT_HIGH, "Usage: %s <ip-mask> [duration] [action]\n", gi.argv(start));
        return;
    }

    if (numipfilters == MAX_IPFILTERS) {
        gi.cprintf(ent, PRINT_HIGH, "IP filter list is full\n");
        return;
    }

    s = gi.argv(start + 1);
    if (!parse_filter(s, &mask, &compare)) {
        gi.cprintf(ent, PRINT_HIGH, "Bad filter address: %s\n", s);
        return;
    }

    duration = ent ? DEF_DURATION : 0;
    action = IPA_BAN;
    if (argc > 2) {
        s = gi.argv(start + 2);
        duration = parse_duration(s);
        if (ent) {
            if (!duration) {
                gi.cprintf(ent, PRINT_HIGH, "You may not add permanent bans.\n");
                return;
            }
            if (duration > MAX_DURATION) {
                duration = MAX_DURATION;
            }
        }
        if (argc > 3) {
            s = gi.argv(start + 3);
            action = parse_action(s);
            if (action == IPA_NONE) {
                gi.cprintf(ent, PRINT_HIGH, "Bad action specifier: %s\n", s);
                return;
            }
        }
    }

    add_filter(action, mask, compare, duration, ent);
}

void G_BanEdict(edict_t *victim, edict_t *initiator)
{
    unsigned mask, compare;

    if (numipfilters == MAX_IPFILTERS) {
        return;
    }
    if (!parse_filter(victim->client->pers.ip, &mask, &compare)) {
        return;
    }

    add_filter(IPA_BAN, mask, compare, DEF_DURATION, initiator);
}

/*
=================
G_RemoveIP_f
=================
*/
void G_RemoveIP_f(edict_t *ent)
{
    unsigned    mask, compare;
    char        *s;
    ipfilter_t  *ip;
    int         start, argc;

    start = ent ? 0 : 1;
    argc = gi.argc() - start;

    if (argc < 2) {
        gi.cprintf(ent, PRINT_HIGH, "Usage: %s <ip-mask>\n", gi.argv(start));
        return;
    }

    s = gi.argv(start + 1);
    if (!parse_filter(s, &mask, &compare)) {
        gi.cprintf(ent, PRINT_HIGH, "Bad filter address: %s\n", s);
        return;
    }

    FOR_EACH_IPFILTER(ip) {
        if (ip->mask == mask && ip->compare == compare) {
            if (ent && !ip->duration) {
                gi.cprintf(ent, PRINT_HIGH, "You may not remove permanent bans.\n");
                return;
            }
            remove_filter(ip);
            gi.cprintf(ent, PRINT_HIGH, "Removed.\n");
            return;
        }
    }

    gi.cprintf(ent, PRINT_HIGH, "Didn't find %s.\n", s);
}

static size_t Com_FormatTime(char *buffer, size_t size, time_t t)
{
    int     sec, min, hour, day;

    sec = (int)t;
    min = sec / 60; sec %= 60;
    hour = min / 60; min %= 60;
    day = hour / 24; hour %= 24;

    if (day) {
        return Q_scnprintf(buffer, size, "%d+%d:%02d.%02d", day, hour, min, sec);
    }
    if (hour) {
        return Q_scnprintf(buffer, size, "%d:%02d.%02d", hour, min, sec);
    }
    return Q_scnprintf(buffer, size, "%02d.%02d", min, sec);
}

/*
=================
G_ListIP_f
=================
*/
void G_ListIP_f(edict_t *ent)
{
    byte b[4];
    ipfilter_t *ip, *next;
    char address[32], expires[32];
    time_t now, diff;

    if (LIST_EMPTY(&ipfilters)) {
        gi.cprintf(ent, PRINT_HIGH, "Filter list is empty.\n");
        return;
    }

    now = time(NULL);

    gi.cprintf(ent, PRINT_HIGH,
               "address         expires in action added by\n"
               "--------------- ---------- ------ ---------------\n");
    FOR_EACH_IPFILTER_SAFE(ip, next) {
        *(unsigned *)b = ip->compare;
        Q_snprintf(address, sizeof(address), "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);

        if (ip->duration) {
            diff = now - ip->added;
            if (diff > ip->duration) {
                remove_filter(ip);
                continue;
            }
            Com_FormatTime(expires, sizeof(expires), ip->duration - diff);
        } else {
            strcpy(expires, "permanent");
        }

        gi.cprintf(ent, PRINT_HIGH, "%-15s %10s %6s %s\n",
                   address, expires, ip->action == IPA_MUTE ? "mute" : "ban", ip->adder);
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
    byte    b[4];
    size_t  len;
    ipfilter_t  *ip;

    if (!game.dir[0]) {
        return;
    }

    len = Q_snprintf(name, sizeof(name), "%s/listip.cfg", game.dir);
    if (len >= sizeof(name)) {
        return;
    }

    f = fopen(name, "wb");
    if (!f) {
        gi.cprintf(NULL, PRINT_HIGH, "Couldn't open %s\n", name);
        return;
    }

    gi.cprintf(NULL, PRINT_HIGH, "Writing %s.\n", name);

    //fprintf(f, "set filterban %d\n", (int)filterban->value);

    FOR_EACH_IPFILTER(ip) {
        if (!ip->duration) {
            *(unsigned *)b = ip->compare;
            fprintf(f, "sv addip %d.%d.%d.%d\n", b[0], b[1], b[2], b[3]);
        }
    }

    fclose(f);
}
