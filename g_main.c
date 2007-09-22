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

game_locals_t	game;
level_locals_t	level;
game_import_t	gi;
int             serverFeatures;
game_export_t	globals;
spawn_temp_t	st;

int meansOfDeath;

edict_t		*g_edicts;

cvar_t	*g_midair;
cvar_t	*g_ctf;
cvar_t	*dmflags;
cvar_t	*skill;
cvar_t	*fraglimit;
cvar_t	*timelimit;
cvar_t	*maxclients;
cvar_t	*maxentities;
cvar_t	*g_select_empty;
cvar_t	*g_idletime;
cvar_t	*g_vote_mask;
cvar_t	*g_vote_time;
cvar_t	*g_vote_treshold;
cvar_t	*g_vote_limit;
cvar_t	*g_randomize;
cvar_t	*g_item_ban;
cvar_t	*dedicated;

cvar_t	*filterban;

cvar_t	*sv_maxvelocity;
cvar_t	*sv_gravity;

cvar_t	*sv_rollspeed;
cvar_t	*sv_rollangle;
cvar_t	*gun_x;
cvar_t	*gun_y;
cvar_t	*gun_z;

cvar_t	*run_pitch;
cvar_t	*run_roll;
cvar_t	*bob_up;
cvar_t	*bob_pitch;
cvar_t	*bob_roll;

cvar_t	*sv_cheats;

cvar_t	*flood_msgs;
cvar_t	*flood_persecond;
cvar_t	*flood_waitdelay;

cvar_t	*sv_maplist;

void ClientThink (edict_t *ent, usercmd_t *cmd);
qboolean ClientConnect (edict_t *ent, char *userinfo);
void ClientUserinfoChanged (edict_t *ent, char *userinfo);
void ClientDisconnect (edict_t *ent);
void ClientBegin (edict_t *ent);
void ClientCommand (edict_t *ent);
void RunEntity (edict_t *ent);


//===================================================================

/*
=================
ClientEndServerFrames
=================
*/
void ClientEndServerFrames (void) {
	int		i;
    gclient_t *c;

	if( level.intermission_framenum ) {
        // if the end of unit layout is displayed, don't give
        // the player any normal movement attributes
        for( i = 0, c = game.clients; i < game.maxclients; i++, c++ ) {
            if( c->pers.connected <= CONN_CONNECTED ) {
                continue;
            }
            IntermissionEndServerFrame( c->edict );
        }
		return;
	}

	// calc the player views now that all pushing
	// and damage has been added
    for( i = 0, c = game.clients; i < game.maxclients; i++, c++ ) {
        if( c->pers.connected <= CONN_CONNECTED ) {
            continue;
        }
        if( c->pers.connected == CONN_SPECTATOR && c->chase_target ) {
            continue;
        }
		ClientEndServerFrame( c->edict );
	}

    // update chase cam after all stats and positions are calculated
    for( i = 0, c = game.clients; i < game.maxclients; i++, c++ ) {
        if( c->pers.connected != CONN_SPECTATOR || !c->chase_target ) {
            continue;
        }
		ChaseEndServerFrame( c->edict );
    }

}


char *CopyString( const char *in ) {
	char	*out;
	int length;

	if( !in ) {
		return NULL;
	}

	length = strlen( in ) + 1;
	
	out = gi.TagMalloc( TAG_GAME, length );
	strcpy( out, in );

	return out;
}

static int QDECL ScoreCmp( const void *p1, const void *p2 ) {
    score_t *a = *( score_t * const * )p1;
    score_t *b = *( score_t * const * )p2;

    if( a->score > b->score ) {
        return 1;
    }
    if( a->score < b->score ) {
        return -1;
    }
    if( a->time > b->time ) {
        return 1;
    }
    if( a->time < b->time ) {
        return -1;
    }
    return 0;
}

static void RegisterScore( void ) {
	gclient_t	*ranks[MAX_CLIENTS];
	gclient_t	*c;
    score_t *s;
    int total;
    int sec, score;

    total = G_CalcRanks( ranks );
    if( !total ) {
        return;
    }

    // grab our champion
    c = ranks[0];

    // calculate FPH
    sec = ( level.framenum - c->level.enter_framenum ) / HZ;
    if( !sec ) {
        sec = 1;
    }
    score = c->resp.score * 3600 / sec;

    if( score < 1 ) {
        return; // do not record bogus results
    }

    if( level.numscores < MAX_SCORES ) {
        s = &level.scores[level.numscores++];
    } else {
        s = &level.scores[ level.numscores - 1 ];
        if( score < s->score ) {
            return; // result not impressive enough
        }
    }

    strcpy( s->name, c->pers.netname );
    s->score = score;
    time( &s->time );

    qsort( level.scores, level.numscores, sizeof( score_t ), ScoreCmp );

    level.record = s;
}

/*
=================
EndDMLevel

The timelimit or fraglimit has been exceeded
=================
*/
void EndDMLevel ( void ) {
    int r = g_randomize->value;
    map_entry_t *map;

    RegisterScore();

	BeginIntermission();

    if( level.nextmap[0] ) {
        return; // already set by operator or vote
    }
    
    strcpy( level.nextmap, level.mapname );

	// stay on same level flag
	if( DF( SAME_LEVEL ) ) {
		return;
	}

    // empty map list?
    if( LIST_EMPTY( &g_maplist ) ) {
        return;
    }

    if( r < 1 ) {
        // sequental maplist traversal
        map = G_FindMap( level.mapname );
        if( map ) {
            map = LIST_NEXT_CYCLE( map_entry_t, map, &g_maplist, entry );
        } else {
            map = LIST_FIRST( map_entry_t, &g_maplist, entry );
        }
    } else {
        map = G_RandomMap();
        if( r > 1 && !Q_stricmp( map->name, level.mapname ) ) {
            map = LIST_NEXT_CYCLE( map_entry_t, map, &g_maplist, entry );
        }
    }

    Com_Printf( "Next map is %s.\n", map->name );
    strcpy( level.nextmap, map->name );
}

void G_StartSound( int index ) {
    gi.sound( &g_edicts[0], 0, index, 1, ATTN_NONE, 0 );
}

/*
=================
CheckDMRules
=================
*/
static void CheckDMRules( void ) {
	int			i, remaining;
	gclient_t	*c;

    if( g_item_ban->modified ) {
        G_UpdateItemBans();
    }

	if( timelimit->value > 0 ) {
		if( level.time >= timelimit->value*60 ) {
			gi.bprintf( PRINT_HIGH, "Timelimit hit.\n" );
			EndDMLevel();
			return;
		}
        if( timelimit->modified || ( level.framenum % HZ ) == 0 ) {
            remaining = G_WriteTime();
            gi.multicast( NULL, MULTICAST_ALL );

            // notify
            switch( remaining ) {
            case 10:
			    gi.bprintf( PRINT_HIGH, "10 seconds remaining in match.\n" );
                G_StartSound( level.sounds.count );
                break;
            case 60:
			    gi.bprintf( PRINT_HIGH, "1 minute remaining in match.\n" );
                G_StartSound( level.sounds.secret );
                break;
            case 300:
			    gi.bprintf( PRINT_HIGH, "5 minutes remaining in match.\n" );
                G_StartSound( level.sounds.secret );
                break;
            case 600:
			    gi.bprintf( PRINT_HIGH, "10 minutes remaining in match.\n" );
                G_StartSound( level.sounds.secret );
                break;
            case 900:
			    gi.bprintf( PRINT_HIGH, "15 minutes remaining in match.\n" ); 
                G_StartSound( level.sounds.secret );
                break;
            }
        }
	}

	if( fraglimit->value > 0 ) {
		for( i = 0, c = game.clients; i < game.maxclients; i++, c++ ) {
            if( c->pers.connected != CONN_SPAWNED ) {
                continue;
            }
			if( c->resp.score >= fraglimit->value ) {
				gi.bprintf( PRINT_HIGH, "Fraglimit hit.\n" );
				EndDMLevel();
				return;
			}
		}
	}

    if( fraglimit->modified ) {
		for( i = 0, c = game.clients; i < game.maxclients; i++, c++ ) {
            if( c->pers.connected != CONN_SPAWNED ) {
                continue;
            }
            G_ScoreChanged( c->edict );
        }
        G_UpdateRanks();
    }


    timelimit->modified = qfalse;
    fraglimit->modified = qfalse;
}

/*
=============
ExitLevel
=============
*/
void G_ExitLevel (void) {
	char	command [256];

    if( level.intermission_exit ) {
        return; // already exited
    }

    if( !strcmp( level.nextmap, level.mapname ) ) {
        G_ResetLevel();
        return;
    }

	Com_sprintf (command, sizeof(command), "gamemap \"%s\"\n", level.nextmap);
	gi.AddCommandString (command);

    level.intermission_exit = level.framenum;
}

/*
================
G_RunFrame

Advances the world by 0.1 seconds
================
*/
void G_RunFrame (void)
{
	int		i, delta;
	edict_t	*ent;

	level.framenum++;
	level.time = level.framenum*FRAMETIME;

	//
	// treat each object in turn
	// even the world gets a chance to think
	//
	ent = &g_edicts[0];
	for (i=0 ; i<globals.num_edicts ; i++, ent++) {
		if (!ent->inuse)
			continue;

		level.current_entity = ent;

		VectorCopy (ent->s.origin, ent->s.old_origin);

		// if the ground entity moved, make sure we are still on it
		if ((ent->groundentity) && (ent->groundentity->linkcount != ent->groundentity_linkcount)) {
			ent->groundentity = NULL;
		}

		if (i > 0 && i <= game.maxclients) {
			ClientBeginServerFrame (ent);
			continue;
		}

		G_RunEntity (ent);
	}

    if( level.intermission_exit ) {
        if( level.framenum > level.intermission_exit + 5 ) {
            G_ResetLevel(); // in case gamemap failed, reset the level
        }
    } else if( level.intermission_framenum ) {
        delta = level.framenum - level.intermission_framenum;
        if( delta == 1*HZ ) {
            if( rand() & 1 ) {
                G_StartSound( level.sounds.xian );
            } else {
                G_StartSound( level.sounds.makron );
            }
        } else if( delta == 1200*HZ ) {
		    // auto exit intermission after 20 minutes
	    	G_ExitLevel();
        } else if( delta % ( 5 * HZ ) == 0 ) {
            delta /= 5 * HZ;
            if( level.numscores && ( delta & 1 ) ) {
                HighScoresMessage();
                gi.multicast( NULL, MULTICAST_ALL_R );
            } else {
                ent = &g_edicts[1];
                for( i = 1; i <= game.maxclients; i++, ent++ ) {
                    if( ent->client->pers.connected > CONN_CONNECTED ) {
                        DeathmatchScoreboardMessage( ent, NULL );
                        gi.unicast( ent, qtrue );
                    }
                }
            }
        }
    } else {
	    // see if it is time to end a deathmatch
	    CheckDMRules();

        // check vote timeout
        if( level.vote.proposal && level.framenum > level.vote.framenum ) {
            gi.bprintf( PRINT_HIGH, "Vote failed.\n" );
            level.vote.proposal = 0;
        }
    }

	// build the playerstate_t structures for all players
	ClientEndServerFrames ();
}


static void G_Shutdown (void) {
	gi.dprintf ("==== ShutdownGame ====\n");

	gi.FreeTags (TAG_LEVEL);
	gi.FreeTags (TAG_GAME);

    List_Init( &g_maplist );
}


/*
============
InitGame

This will be called when the dll is first loaded, which
only happens when a new game is started or a save game
is loaded.
============
*/
static void G_Init (void) {
	gi.dprintf ("==== InitGame ====\n");

	gun_x = gi.cvar ("gun_x", "0", 0);
	gun_y = gi.cvar ("gun_y", "0", 0);
	gun_z = gi.cvar ("gun_z", "0", 0);

	//FIXME: sv_ prefix is wrong for these
	sv_rollspeed = gi.cvar ("sv_rollspeed", "200", 0);
	sv_rollangle = gi.cvar ("sv_rollangle", "2", 0);
	sv_maxvelocity = gi.cvar ("sv_maxvelocity", "2000", 0);
	sv_gravity = gi.cvar ("sv_gravity", "800", 0);

	// noset vars
	dedicated = gi.cvar ("dedicated", "0", CVAR_NOSET);

	// latched vars
	sv_cheats = gi.cvar ("cheats", "0", CVAR_SERVERINFO|CVAR_LATCH);
	gi.cvar ("gamename", GAMEVERSION , CVAR_SERVERINFO | CVAR_LATCH);
	gi.cvar ("gamedate", __DATE__ , CVAR_SERVERINFO | CVAR_LATCH);

	maxclients = gi.cvar ("maxclients", "4", CVAR_SERVERINFO | CVAR_LATCH);
	g_midair = gi.cvar ("g_midair", "0", CVAR_LATCH);
	g_ctf = gi.cvar ("g_ctf", "0", CVAR_LATCH);
	maxentities = gi.cvar ("maxentities", "1024", CVAR_LATCH);

	// change anytime vars
	dmflags = gi.cvar ("dmflags", "0", CVAR_SERVERINFO);
	fraglimit = gi.cvar ("fraglimit", "0", CVAR_SERVERINFO);
	timelimit = gi.cvar ("timelimit", "0", CVAR_SERVERINFO);

	g_select_empty = gi.cvar ("g_select_empty", "0", CVAR_ARCHIVE);
	g_idletime = gi.cvar ("g_idletime", "0", 0);
	g_vote_mask = gi.cvar ("g_vote_mask", "0", 0);
	g_vote_time = gi.cvar ("g_vote_time", "120", 0);
	g_vote_treshold = gi.cvar ("g_vote_treshold", "50", 0);
	g_vote_limit = gi.cvar ("g_vote_limit", "0", 0);
	g_randomize = gi.cvar ("g_randomize", "1", 0);
	g_item_ban = gi.cvar ("g_item_ban", "0", 0);

	run_pitch = gi.cvar ("run_pitch", "0.002", 0);
	run_roll = gi.cvar ("run_roll", "0.005", 0);
	bob_up  = gi.cvar ("bob_up", "0.005", 0);
	bob_pitch = gi.cvar ("bob_pitch", "0.002", 0);
	bob_roll = gi.cvar ("bob_roll", "0.002", 0);

	// flood control
	flood_msgs = gi.cvar ("flood_msgs", "4", 0);
	flood_persecond = gi.cvar ("flood_persecond", "4", 0);
	flood_waitdelay = gi.cvar ("flood_waitdelay", "10", 0);

	// dm map list
	sv_maplist = gi.cvar ("sv_maplist", "", 0);

    // force deathmatch
    //gi.cvar_set( "coop", "0" ); //atu
    //gi.cvar_set( "deathmatch", "1" );

	// initialize all entities for this game
	game.maxentities = maxentities->value;
	g_edicts =  gi.TagMalloc (game.maxentities * sizeof(g_edicts[0]), TAG_GAME);
	globals.edicts = g_edicts;
	globals.max_edicts = game.maxentities;

	// initialize all clients for this game
	game.maxclients = maxclients->value;
	game.clients = gi.TagMalloc (game.maxclients * sizeof(game.clients[0]), TAG_GAME);
	globals.num_edicts = game.maxclients+1;
}

static void G_WriteGame (const char *filename, qboolean autosave) {
}

static void G_ReadGame (const char *filename) {
}

static void G_WriteLevel (const char *filename) {
}

static void G_ReadLevel (const char *filename) {
}

//======================================================================

#ifndef GAME_HARD_LINKED

// this is only here so the functions in q_shared.c can link
void Com_Printf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	gi.dprintf( "%s", text );
}

void Com_DPrintf( const char *fmt, ... ) {
}

void Com_WPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	gi.dprintf( "WARNING: %s", text );
}

void Com_EPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	gi.dprintf( "ERROR: %s", text );
}

void Com_Error( comErrorType_t err_level, const char *error, ... ) {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];

	va_start( argptr, error );
	Q_vsnprintf( text, sizeof( text ), error, argptr );
	va_end( argptr );

	gi.error( "%s", text );
}

#endif

/*
=================
GetGameAPI

Returns a pointer to the structure with all entry points
and global variables
=================
*/
EXPORTED game_export_t *GetGameAPI (game_import_t *import)
{
	gi = *import;

	globals.apiversion = GAME_API_VERSION;
	globals.Init = G_Init;
	globals.Shutdown = G_Shutdown;
	globals.SpawnEntities = G_SpawnEntities;

	globals.WriteGame = G_WriteGame;
	globals.ReadGame = G_ReadGame;
	globals.WriteLevel = G_WriteLevel;
	globals.ReadLevel = G_ReadLevel;

	globals.ClientThink = ClientThink;
	globals.ClientConnect = ClientConnect;
	globals.ClientUserinfoChanged = ClientUserinfoChanged;
	globals.ClientDisconnect = ClientDisconnect;
	globals.ClientBegin = ClientBegin;
	globals.ClientCommand = ClientCommand;

	globals.RunFrame = G_RunFrame;

	globals.ServerCommand = G_ServerCommand;

	globals.edict_size = sizeof(edict_t);

	return &globals;
}

EXPORTED int GetGameFeatures( int features ) {
    serverFeatures = features;
    return GAME_FEATURE_CLIENTNUM;
}
