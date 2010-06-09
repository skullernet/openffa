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

static void Svcmd_Reset_f( void ) {
    G_ResetLevel();
}

static void Svcmd_NextMap_f( void ) {
    if( gi.argc() != 3 ) {
        Com_Printf( "Usage: nextmap <name>\n" );
        return;
    }
    Q_strlcpy( level.nextmap, gi.argv( 2 ), sizeof( level.nextmap ) );
}

static void Svcmd_MapList_f( void ) {
    map_entry_t *map;

    if( LIST_EMPTY( &g_map_list ) ) {
        Com_Printf( "Map list is empty\n" );
        return;
    }

    Com_Printf( "map             min max fl hits   in  out\n"
                "--------------- --- --- -- ---- ---- ----\n" );
    LIST_FOR_EACH( map_entry_t, map, &g_map_list, list ) {
        Com_Printf( "%-15.15s %3d %3d %c%c %4d %4d %4d\n",
            map->name, map->min_players, map->max_players,
            ( map->flags & MAP_NOAUTO ) ? ' ' : 'A',
            ( map->flags & MAP_NOVOTE ) ? ' ' : 'V',
            map->num_hits, map->num_in, map->num_out );
    }
}

static void Svcmd_MapQueue_f( void ) {
    map_entry_t *map;
    int total;

    if( LIST_EMPTY( &g_map_queue ) ) {
        Com_Printf( "Map queue is empty\n" );
        return;
    }

    total = G_CalcRanks( NULL );

    Com_Printf( "map             min max\n"
                "--------------- --- ---\n" );
    LIST_FOR_EACH( map_entry_t, map, &g_map_queue, queue ) {
        Com_Printf( "%-15.15s %3d %3d %s\n",
            map->name, map->min_players, map->max_players,
            ( total >= map->min_players && total <= map->max_players ) ? "*" : "" );
    }
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
    char    *cmd;

    if( gi.argc() < 2 ) {
        Com_Printf( "Usage: sv <command> [arguments ...]\n" );
        return;
    }

    cmd = gi.argv(1);
    if (!strcmp (cmd, "test"))
        Svcmd_Test_f ();
    else if (!strcmp (cmd, "addip"))
        G_AddIP_f( NULL );
    else if (!strcmp (cmd, "removeip"))
        G_RemoveIP_f( NULL );
    else if (!strcmp (cmd, "listip"))
        G_ListIP_f( NULL );
    else if (!strcmp (cmd, "writeip"))
        G_WriteIP_f();
    else if (!strcmp (cmd, "reset"))
        Svcmd_Reset_f ();
    else if (!strcmp (cmd, "nextmap"))
        Svcmd_NextMap_f ();
    else if (!strcmp (cmd, "maplist"))
        Svcmd_MapList_f ();
    else if (!strcmp (cmd, "mapqueue"))
        Svcmd_MapQueue_f ();
    else if (!strcmp (cmd, "players") || !strcmp (cmd, "playerlist"))
        Cmd_Players_f( NULL );
    else if (!strcmp (cmd, "highscores"))
        Cmd_HighScores_f( NULL );
    else if (!strcmp (cmd, "stats") || !strcmp (cmd, "accuracy"))
        Cmd_Stats_f( NULL, qtrue );
    else if (!strcmp (cmd, "settings") || !strcmp (cmd, "matchinfo"))
        Cmd_Settings_f( NULL );
    else
        Com_Printf( "Unknown server command \"%s\"\n", cmd);
}

