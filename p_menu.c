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

static void PMenu_Write(edict_t *ent) {
	char string[MAX_STRING_CHARS];
	char entry[MAX_STRING_CHARS];
	int i, j, length;
	pmenu_entry_t *p;
	int x;
	pmenu_t *menu = ent->client->menu;
	const char *t;
    qboolean alt;

	if (!menu) {
		return;
	}

	strcpy( string, "xv 32 yv 8 picn inventory " );
    length = strlen( string );

	for (i = 0, p = menu->entries; i < menu->num; i++, p++) {
		if (!p->text || !p->text[0])
			continue; // blank line
		t = p->text;
		if (*t == '*') {
            alt = qtrue;
			t++;
		} else {
            alt = qfalse;
        }
		if (p->align == PMENU_ALIGN_CENTER)
			x = 196/2 - strlen(t)*4 + 64;
		else if (p->align == PMENU_ALIGN_RIGHT)
			x = 64 + (196 - strlen(t)*8);
		else
			x = 64;

        if( menu->cur == i ) {
            x -= 8;
            alt ^= 1;
        }

		j = Com_sprintf( entry, sizeof( entry ), "yv %d xv %d string%s \"%s%s\" ",
            32 + i * 8, x, alt ? "2" : "", menu->cur == i ? "\x0d" : "", t );

        if( length + j >= MAX_STRING_CHARS )
            break;
        memcpy( string + length, entry, j );
        length += j;
	}

    string[length] = 0;

	gi.WriteByte (svc_layout);
	gi.WriteString (string);
}


// Note that the pmenu entries are duplicated
// this is so that a static set of pmenu entries can be used
// for multiple clients and changed without interference
// note that arg will be freed when the menu is closed, it must be allocated memory
pmenu_t *PMenu_Open( edict_t *ent, pmenu_entry_t *entries, int cur, int num, void *arg ) {
	pmenu_t *menu;
	pmenu_entry_t *p;
	int i;

	if (!ent->client)
		return NULL;

	if (ent->client->menu) {
		gi.dprintf("warning, ent already has a menu\n");
		PMenu_Close(ent);
	}

	menu = G_Malloc( sizeof( pmenu_t ) + sizeof( pmenu_entry_t ) * ( num - 1 ) );
	menu->arg = arg;
	for (i = 0; i < num; i++) {
        menu->entries[i].select = entries[i].select;
        menu->entries[i].align = entries[i].align;
		menu->entries[i].text = entries[i].text;
    }

	menu->num = num;

	if (cur < 0 || !entries[cur].select) {
		for (i = 0, p = entries; i < num; i++, p++)
			if (p->select)
				break;
	} else {
		i = cur;
    }

	if (i >= num)
		menu->cur = -1;
	else
		menu->cur = i;

	ent->client->menu = menu;

	PMenu_Write(ent);
	gi.unicast (ent, qtrue);
    ent->client->menu_framenum = level.framenum;
    ent->client->menu_dirty = qfalse;

	return menu;
}

void PMenu_Close( edict_t *ent ) {
	pmenu_t *menu = ent->client->menu;
	//int i;

	if (!menu)
		return;

	/*for (i = 0; i < menu->num; i++)
		if (menu->entries[i].text)
			gi.TagFree(menu->entries[i].text);*/
	gi.TagFree(menu);
	ent->client->menu = NULL;
}

// only use on pmenu's that have been called with PMenu_Open
void PMenu_UpdateEntry(pmenu_entry_t *entry, const char *text, int align, pmenu_select_t select) {
	if (entry->text)
		gi.TagFree(entry->text);
	entry->text = G_CopyString(text);
	entry->align = align;
	entry->select = select;
}

void PMenu_Update( edict_t *ent ) {
	if (!ent->client->menu) {
		return;
	}

    if( !ent->client->menu_dirty ) {
        return;
    }

	if (level.framenum - ent->client->menu_framenum < 1*HZ ) {
        //return;
    }

    // been a second or more since last update, update now
    PMenu_Write( ent );
    gi.unicast( ent, qtrue );
    ent->client->menu_framenum = level.framenum;
    ent->client->menu_dirty = qfalse;
}

void PMenu_Next( edict_t *ent ) {
	pmenu_t *menu = ent->client->menu;
	pmenu_entry_t *p;
	int i;

	if (!menu) {
		return;
	}

	if (menu->cur < 0)
		return; // no selectable entries

	i = menu->cur;
	p = menu->entries + menu->cur;
	do {
		i++, p++;
		if (i == menu->num)
			i = 0, p = menu->entries;
		if (p->select)
			break;
	} while (i != menu->cur);

	menu->cur = i;

	ent->client->menu_dirty = qtrue;
	//PMenu_Update(ent);
}

void PMenu_Prev( edict_t *ent ) {
	pmenu_t *menu = ent->client->menu;
	pmenu_entry_t *p;
	int i;

	if (!menu) {
		return;
	}

	if (menu->cur < 0)
		return; // no selectable entries

	i = menu->cur;
	p = menu->entries + menu->cur;
	do {
		if (i == 0) {
			i = menu->num - 1;
			p = menu->entries + i;
		} else {
			i--, p--;
        }
		if (p->select)
			break;
	} while (i != menu->cur);

	menu->cur = i;

	ent->client->menu_dirty = qtrue;
	//PMenu_Update(ent);
}

void PMenu_Select( edict_t *ent ) {
	pmenu_t *menu = ent->client->menu;
	pmenu_entry_t *p;

	if (!menu) {
		return;
	}

	if (menu->cur < 0)
		return; // no selectable entries

	p = menu->entries + menu->cur;

	if (p->select)
		p->select(ent, menu);
}

