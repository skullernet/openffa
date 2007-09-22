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


static void Svcmd_Test_f( void ) {
	gi.cprintf (NULL, PRINT_HIGH, "Svcmd_Test_f()\n");
}

LIST_DECL( g_maplist );

map_entry_t *G_FindMap( const char *name ) {
    map_entry_t *map;

    LIST_FOR_EACH( map_entry_t, map, &g_maplist, entry ) {
        if( !Q_stricmp( map->name, name ) ) {
            return map;
        }
    }
    return NULL;
}

map_entry_t *G_RandomMap( void ) {
    map_entry_t *pool[256], *map;
    int count;

    count = 0;
    LIST_FOR_EACH( map_entry_t, map, &g_maplist, entry ) {
        if( map->flags & MAP_NOAUTO ) {
            continue;
        }
        /*if( numplayers < map->min ) {
            continue;
        }
        if( map->max > 0 && numplayers > map->max ) {
            continue;
        }*/
        pool[count++] = map;
    }

    if( !count ) {
        return NULL;
    }

    map = pool[(rand() ^ ( rand() >> 8 ) ) % count];

    return map;
}

static void Svcmd_Maplist_f( void ) {
    int argc = gi.argc();
    char *cmd, *name;
    map_entry_t *map, *next;
    int length;

    if( argc < 3 ) {
        if( LIST_EMPTY( &g_maplist ) ) {
            Com_Printf( "Map list is empty.\n" );
            return;
        }
        LIST_FOR_EACH( map_entry_t, map, &g_maplist, entry ) {
            Com_Printf( "%-8s %2d %2d %2d\n", map->name, map->min, map->max, map->flags );
        }
        return;
    }

    cmd = gi.argv( 2 );
    if( !strcmp( cmd, "add" ) ) {
        if( argc < 4 ) {
            Com_Printf( "Usage: add <mapname> [min] [max] [flags]\n" );
            return;
        }
        name = gi.argv( 3 );
        map = G_FindMap( name );
        if( map ) {
            Com_Printf( "%s already exists in map list.\n", name );
            return;
        }
        length = strlen( name );
        if( length >= MAX_QPATH ) {
            Com_Printf( "Oversize mapname.\n" );
            return;
        }
        map = gi.TagMalloc( sizeof( *map ) + length, TAG_GAME );
        map->min = atoi( gi.argv( 4 ) );
        map->max = atoi( gi.argv( 5 ) );
        map->flags = atoi( gi.argv( 6 ) );
        strcpy( map->name, name );
        List_Append( &g_maplist, &map->entry );
    } else if( !strcmp( cmd, "del" ) ) {
        if( argc < 4 ) {
            Com_Printf( "Usage: del <mapname>\n" );
            return;
        }
        name = gi.argv( 3 );
        map = G_FindMap( name );
        if( !map ) {
            Com_Printf( "%s not found in map list.\n", name );
            return;
        }
        List_Remove( &map->entry );
        gi.TagFree( map );
    } else if( !strcmp( cmd, "clear" ) ) {
        LIST_FOR_EACH_SAFE( map_entry_t, map, next, &g_maplist, entry ) {
            gi.TagFree( map );
        }
        List_Init( &g_maplist );
    } else {
        Com_Printf( "Unknown maplist command \"%s\".\n", cmd );
    }
}

static void Svcmd_Reset_f( void ) {
    G_ResetLevel();
}

static void Svcmd_NextMap_f( void ) {
    if( gi.argc() != 3 ) {
        Com_Printf( "Usage: nextmap <name>\n" );
        return;
    }
    Q_strncpyz( level.nextmap, gi.argv( 2 ), sizeof( level.nextmap ) );
}

/*
=================
ServerCommand

ServerCommand will be called when an "sv" command is issued.
The game can issue gi.argc() / gi.argv() commands to get the rest
of the parameters
=================
*/
void G_ServerCommand (void) {
	char	*cmd;

    if( gi.argc() < 2 ) {
        Com_Printf( "Usage: sv <command> [arguments ...]\n" );
        return;
    }

	cmd = gi.argv(1);
	if (!strcmp (cmd, "test"))
		Svcmd_Test_f ();
    else if (!strcmp (cmd, "maplist"))
		Svcmd_Maplist_f ();
    else if (!strcmp (cmd, "reset"))
		Svcmd_Reset_f ();
    else if (!strcmp (cmd, "nextmap"))
		Svcmd_NextMap_f ();
	else
		Com_Printf( "Unknown server command \"%s\"\n", cmd);
}

