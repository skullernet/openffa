/*
Copyright (C) 1997-2001 Id Software, Inc.

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

static void PMenu_Write(edict_t *ent)
{
    char string[MAX_STRING_CHARS];
    char entry[MAX_STRING_CHARS];
    int i;
    size_t total, len;
    pmenu_entry_t *p;
    int x;
    pmenu_t *menu = &ent->client->menu;
    const char *t;
    bool alt;

    strcpy(string, "xv 32 yv 8 picn inventory ");
    total = strlen(string);

    for (i = 0, p = menu->entries; i < MAX_MENU_ENTRIES; i++, p++) {
        if (!p->text || !p->text[0])
            continue; // blank line
        t = p->text;
        if (*t == '*') {
            alt = true;
            t++;
        } else {
            alt = false;
        }
        if (p->align == PMENU_ALIGN_CENTER)
            x = 196 / 2 - strlen(t) * 4 + 64;
        else if (p->align == PMENU_ALIGN_RIGHT)
            x = 64 + (196 - strlen(t) * 8);
        else
            x = 64;

        if (menu->cur == i) {
            x -= 8;
            alt ^= 1;
        }

        len = Q_snprintf(entry, sizeof(entry), "yv %d xv %d string%s \"%s%s\" ",
                         32 + i * 8, x, alt ? "2" : "", menu->cur == i ? "\x0d" : "", t);
        if (len >= sizeof(entry)) {
            continue;
        }
        if (total + len >= MAX_STRING_CHARS)
            break;
        memcpy(string + total, entry, len);
        total += len;
    }

    string[total] = 0;

    gi.WriteByte(svc_layout);
    gi.WriteString(string);
}

void PMenu_Open(edict_t *ent, const pmenu_entry_t *entries)
{
    pmenu_t *menu = &ent->client->menu;
    const pmenu_entry_t *p;
    int i;

    if (entries) {
        for (i = 0; i < MAX_MENU_ENTRIES; i++) {
            menu->entries[i].select = entries[i].select;
            menu->entries[i].align = entries[i].align;
            menu->entries[i].text = entries[i].text;
        }
    }

    menu->cur = -1;
    for (i = 0, p = menu->entries; i < MAX_MENU_ENTRIES; i++, p++) {
        if (p->select) {
            menu->cur = i;
            break;
        }
    }

    //ent->client->menu_framenum = 0;
    ent->client->menu_dirty = true;
    ent->client->layout = LAYOUT_MENU;
}

void PMenu_Close(edict_t *ent)
{
    if (ent->client->layout != LAYOUT_MENU) {
        return;
    }
    memset(&ent->client->menu, 0, sizeof(ent->client->menu));
    ent->client->menu_dirty = false;
    ent->client->layout = LAYOUT_NONE;
}

void PMenu_Update(edict_t *ent)
{
    if (ent->client->layout != LAYOUT_MENU) {
        return;
    }

    if (!ent->client->menu_dirty) {
        return;
    }

    //if (level.framenum - ent->client->menu_framenum < 1*HZ ) {
    //return;
    //}

    // been a second or more since last update, update now
    PMenu_Write(ent);
    gi.unicast(ent, true);
    //ent->client->menu_framenum = level.framenum;
    ent->client->menu_dirty = false;
}

void PMenu_Next(edict_t *ent)
{
    pmenu_t *menu = &ent->client->menu;
    pmenu_entry_t *p;
    int i;

    if (ent->client->layout != LAYOUT_MENU) {
        return;
    }

    if (menu->cur < 0)
        return; // no selectable entries
    if (menu->cur >= MAX_MENU_ENTRIES)
        menu->cur = 0;

    i = menu->cur;
    p = menu->entries + menu->cur;
    do {
        i++, p++;
        if (i == MAX_MENU_ENTRIES)
            i = 0, p = menu->entries;
        if (p->select)
            break;
    } while (i != menu->cur);

    menu->cur = i;

    ent->client->menu_dirty = true;
}

void PMenu_Prev(edict_t *ent)
{
    pmenu_t *menu = &ent->client->menu;
    pmenu_entry_t *p;
    int i;

    if (ent->client->layout != LAYOUT_MENU) {
        return;
    }

    if (menu->cur < 0)
        return; // no selectable entries
    if (menu->cur >= MAX_MENU_ENTRIES)
        menu->cur = 0;

    i = menu->cur;
    p = menu->entries + menu->cur;
    do {
        if (i == 0) {
            i = MAX_MENU_ENTRIES - 1;
            p = menu->entries + i;
        } else {
            i--, p--;
        }
        if (p->select)
            break;
    } while (i != menu->cur);

    menu->cur = i;

    ent->client->menu_dirty = true;
}

void PMenu_Select(edict_t *ent)
{
    pmenu_t *menu = &ent->client->menu;
    pmenu_entry_t *p;

    if (ent->client->layout != LAYOUT_MENU) {
        return;
    }

    if (menu->cur < 0)
        return; // no selectable entries
    if (menu->cur >= MAX_MENU_ENTRIES)
        menu->cur = 0;

    p = menu->entries + menu->cur;

    if (p->select)
        p->select(ent);
}
