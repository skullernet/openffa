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



/*
======================================================================

INTERMISSION

======================================================================
*/

void MoveClientToIntermission (edict_t *ent)
{
	ent->client->showscores = qtrue;
	VectorCopy (level.intermission_origin, ent->s.origin);
	ent->client->ps.pmove.origin[0] = level.intermission_origin[0]*8;
	ent->client->ps.pmove.origin[1] = level.intermission_origin[1]*8;
	ent->client->ps.pmove.origin[2] = level.intermission_origin[2]*8;
	VectorCopy (level.intermission_angle, ent->client->ps.viewangles);
	ent->client->ps.pmove.pm_type = PM_FREEZE;
	ent->client->ps.pmove.pm_flags ^= PMF_TELEPORT_BIT;
	ent->client->ps.gunindex = 0;
	ent->client->ps.blend[3] = 0;
	ent->client->ps.rdflags &= ~RDF_UNDERWATER;
	ent->client->ps.stats[STAT_FLASHES] = 0;

	// clean up powerup info
	ent->client->quad_framenum = 0;
	ent->client->invincible_framenum = 0;
	ent->client->breather_framenum = 0;
	ent->client->enviro_framenum = 0;
	ent->client->grenade_blew_up = qfalse;
	ent->client->grenade_framenum = 0;

	ent->viewheight = 0;
	ent->s.modelindex = 0;
	ent->s.modelindex2 = 0;
	ent->s.modelindex3 = 0;
	ent->s.modelindex = 0;
	ent->s.effects = 0;
	ent->s.sound = 0;
	ent->solid = SOLID_NOT;
    ent->svflags = SVF_NOCLIENT;
    gi.unlinkentity( ent );

	// add the layout
	DeathmatchScoreboardMessage (ent, NULL);
	gi.unicast (ent, qtrue);
}

void BeginIntermission (void)
{
	int		i;
	edict_t	*ent, *client;

	if (level.intermission_framenum)
		return;		// already activated

	// respawn any dead clients
	for (i=0 ; i<maxclients->value ; i++)
	{
		client = g_edicts + 1 + i;
		if (!client->inuse)
			continue;
		if (client->health <= 0)
			respawn(client);
	}

	level.intermission_framenum = level.framenum;
    level.vote.proposal = 0;

	// find an intermission spot
	ent = G_Find (NULL, FOFS(classname), "info_player_intermission");
	if (!ent)
	{	// the map creator forgot to put in an intermission point...
		ent = G_Find (NULL, FOFS(classname), "info_player_start");
		if (!ent)
			ent = G_Find (NULL, FOFS(classname), "info_player_deathmatch");
	}
	else
	{	// chose one of four spots
		i = rand() & 3;
		while (i--)
		{
			ent = G_Find (ent, FOFS(classname), "info_player_intermission");
			if (!ent)	// wrap around the list
				ent = G_Find (ent, FOFS(classname), "info_player_intermission");
		}
	}

	VectorCopy (ent->s.origin, level.intermission_origin);
	VectorCopy (ent->s.angles, level.intermission_angle);

	// move all clients to the intermission point
	for (i=0 ; i<maxclients->value ; i++)
	{
		client = g_edicts + 1 + i;
		if (!client->inuse)
			continue;
		MoveClientToIntermission (client);
	}
}

static int QDECL G_PlayerCmp( const void *p1, const void *p2 ) {
    gclient_t *a = *( gclient_t * const * )p1;
    gclient_t *b = *( gclient_t * const * )p2;

    if( a->resp.score > b->resp.score ) {
        return 1;
    }
    if( a->resp.score < b->resp.score ) {
        return -1;
    }
    if( a->resp.deaths < b->resp.deaths ) {
        return 1;
    }
    if( a->resp.deaths > b->resp.deaths ) {
        return -1;
    }
    return 0;
}

int G_CalcRanks( gclient_t **ranks ) {
    int i, total;

	// sort the clients by score, then by eff
	total = 0;
	for( i = 0; i < game.maxclients; i++ ) {
        if( game.clients[i].pers.connected != CONN_SPAWNED ) {
            continue;
        }

        ranks[total++] = &game.clients[i];
	}

    qsort( ranks, total, sizeof( gclient_t * ), G_PlayerCmp );

    return total;
}


void HighScoresMessage( void ) {
	char	entry[MAX_STRING_CHARS];
	char	string[MAX_STRING_CHARS];
    char    date[MAX_QPATH];
    struct tm   *tm;
    score_t *s;
	int		stringlength;
	int		i, j;
    int     y;

	stringlength = Com_sprintf( string, sizeof( string ),
        "xv 0 "
        "yv 0 "
        "cstring \"High Scores for %s\""
        "yv 16 "
        "cstring2 \"  # Name             FPH Date     \"",
        level.mapname );

    y = 24;
	for( i = 0; i < level.numscores; i++ ) {
        s = &level.scores[i];

        tm = localtime( &s->time );
        j = strftime( date, sizeof( date ), "%d %b %y", tm );
        if( j <= 0 ) {
            strcpy( date, "???" );
        }
		j = Com_sprintf( entry, sizeof( entry ),
		    "yv %d cstring \"%c%2d %-15.15s %4d %-8s\"",
            y, s->time == level.record ? '*' : ' ',
            i + 1, s->name, s->score, date );

        if( stringlength + j >= MAX_STRING_CHARS )
            break;
        strcpy( string + stringlength, entry );
        stringlength += j;
        y += 8;
    }

	gi.WriteByte( svc_layout );
	gi.WriteString( string );
}


/*
==================
DeathmatchScoreboardMessage

==================
*/
void DeathmatchScoreboardMessage( edict_t *ent, edict_t *killer ) {
	char	entry[MAX_STRING_CHARS];
	char	string[MAX_STRING_CHARS];
    char    status[MAX_QPATH];
	int		stringlength;
	int		i, j, total;
    int     y, sec, eff;
	gclient_t	*ranks[MAX_CLIENTS];
	gclient_t	*c;

	strcpy( string,
        "xv -16 "
        "yv 26 "
        "string \"Player          Frg Dth Eff% FPH Time Ping\""
        "xv -40 " );
	stringlength = strlen( string );

    total = G_CalcRanks( ranks );

	// add the clients sorted by rank
    y = 34;
	for( i = 0; i < total; i++ ) {
		c = ranks[i];

        sec = ( level.framenum - c->level.enter_framenum ) / HZ;
        if( !sec ) {
            sec = 1;
        }

        if( c->resp.score > 0 ) {
            j = c->resp.score + c->resp.deaths;
            eff = j ? c->resp.score * 100 / j : 100;
        } else {
            eff = 0;
        }

		j = Com_sprintf( entry, sizeof( entry ),
		    "yv %d string%s \"%2d %-15s %3d %3d %3d %4d %4d %4d\"",
            y, c == ent->client ? "" : "2", i + 1,
            c->pers.netname, c->resp.score, c->resp.deaths, eff,
            c->resp.score * 3600 / sec, sec / 60, c->ping );

        if( stringlength + j >= MAX_STRING_CHARS )
            break;
        strcpy( string + stringlength, entry );
        stringlength += j;
        y += 8;
    }

    // add spectators in no particular order
	for( i = 0; i < game.maxclients; i++ ) {
        c = &game.clients[i];
        if( c->pers.connected != CONN_PREGAME && c->pers.connected != CONN_SPECTATOR ) {
            continue;
        }

        sec = ( level.framenum - c->level.enter_framenum ) / HZ;
        if( !sec ) {
            sec = 1;
        }

        if( c->chase_target ) {
            Com_sprintf( status, sizeof( status ), "(chasing %s)",
                c->chase_target->client->pers.netname );
        } else {
            strcpy( status, "(observing)" );
        }

		j = Com_sprintf( entry, sizeof( entry ),
		    "yv %d string%s \"%2d %-15s %-16s %4d %4d\"",
            y, c == ent->client ? "" : "2", i + 1,
            c->pers.netname, status, sec / 60, c->ping );

        if( stringlength + j >= MAX_STRING_CHARS )
            break;
        strcpy( string + stringlength, entry );
        stringlength += j;
        y += 8;
    }

	gi.WriteByte( svc_layout );
	gi.WriteString( string );
}


/*
==================
DeathmatchScoreboard

Draw instead of help message.
Note that it isn't that hard to overflow the 1400 byte message limit!
==================
*/
void DeathmatchScoreboard (edict_t *ent) {
	DeathmatchScoreboardMessage (ent, ent->enemy);
	gi.unicast (ent, qtrue);
}


/*
==================
Cmd_Score_f

Display the scoreboard
==================
*/
void Cmd_Score_f (edict_t *ent) {
	ent->client->showinventory = qfalse;
	ent->client->showhelp = qfalse;

	if (ent->client->showscores) {
		ent->client->showscores = qfalse;
		return;
	}

	ent->client->showscores = qtrue;
	DeathmatchScoreboard (ent);
}
/*
==================
Cmd_Help_f

Display the current help message
==================
*/
void Cmd_Help_f (edict_t *ent) {
	// this is for backwards compatability
	Cmd_Score_f (ent);
}


//=======================================================================

void G_PrivateString( edict_t *ent, int index, const char *string ) {
    gclient_t *client;
    int i;

    if( index < 0 || index >= MAX_PRIVATE ) {
        gi.error( "G_PrivateString: index %d out of range", index );
    }

    if( !strcmp( ent->client->level.strings[index], string ) ) {
        return; // not changed
    }

    // save new string
    Q_strncpyz( ent->client->level.strings[index], string, MAX_NETNAME );

    gi.WriteByte( svc_configstring );
    gi.WriteShort( CS_PRIVATE + index );
    gi.WriteString( string );
    gi.unicast( ent, qtrue );

    // send it to chasecam clients too
    if( ent->client->chase_target ) {
        return;
    }
    for( i = 0, client = game.clients; i < game.maxclients; i++, client++ ) {
        if( client->pers.connected != CONN_SPECTATOR ) {
            continue;
        }
        if( client->chase_target == ent ) {
            G_PrivateString( client->edict, index, string );
        }
    }
}

/*
===============
G_SetStats
===============
*/
void G_SetStats (edict_t *ent)
{
	const gitem_t	*item;
	int			index, cells;
	int			power_armor_type;

	//
	// health
	//
	ent->client->ps.stats[STAT_HEALTH_ICON] = level.images.health;
	ent->client->ps.stats[STAT_HEALTH] = ent->health;

	//
	// ammo
	//
	if (!ent->client->ammo_index /* || !ent->client->pers.inventory[ent->client->ammo_index] */)
	{
		ent->client->ps.stats[STAT_AMMO_ICON] = 0;
		ent->client->ps.stats[STAT_AMMO] = 0;
	}
	else
	{
		item = INDEX_ITEM( ent->client->ammo_index );
		ent->client->ps.stats[STAT_AMMO_ICON] = gi.imageindex (item->icon);
		ent->client->ps.stats[STAT_AMMO] = ent->client->inventory[ent->client->ammo_index];
	}
	
	//
	// armor
	//
    cells = 0;
	power_armor_type = PowerArmorType (ent);
	if (power_armor_type)
	{
		cells = ent->client->inventory[ITEM_CELLS];
		if (cells == 0)
		{	// ran out of cells for power armor
			ent->flags &= ~FL_POWER_ARMOR;
			gi.sound(ent, CHAN_ITEM, gi.soundindex("misc/power2.wav"), 1, ATTN_NORM, 0);
			power_armor_type = 0;
		}
	}

	index = ArmorIndex (ent);
	if (power_armor_type && (!index || (level.framenum & 8) ) )
	{	// flash between power armor and other armor icon
		ent->client->ps.stats[STAT_ARMOR_ICON] = level.images.powershield;
		ent->client->ps.stats[STAT_ARMOR] = cells;
	}
	else if (index)
	{
		item = INDEX_ITEM(index);
		ent->client->ps.stats[STAT_ARMOR_ICON] = gi.imageindex (item->icon);
		ent->client->ps.stats[STAT_ARMOR] = ent->client->inventory[index];
	}
	else
	{
		ent->client->ps.stats[STAT_ARMOR_ICON] = 0;
		ent->client->ps.stats[STAT_ARMOR] = 0;
	}

	//
	// pickup message
	//
	if (level.framenum > ent->client->pickup_framenum)
	{
		ent->client->ps.stats[STAT_PICKUP_ICON] = 0;
		ent->client->ps.stats[STAT_PICKUP_STRING] = 0;
	}

	//
	// timers
	//
	if (ent->client->quad_framenum > level.framenum)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = level.images.quad;
		ent->client->ps.stats[STAT_TIMER] = (ent->client->quad_framenum - level.framenum)/HZ;
	}
	else if (ent->client->invincible_framenum > level.framenum)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = level.images.invulnerability;
		ent->client->ps.stats[STAT_TIMER] = (ent->client->invincible_framenum - level.framenum)/HZ;
	}
	else if (ent->client->enviro_framenum > level.framenum)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = level.images.envirosuit;
		ent->client->ps.stats[STAT_TIMER] = (ent->client->enviro_framenum - level.framenum)/HZ;
	}
	else if (ent->client->breather_framenum > level.framenum)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = level.images.rebreather;
		ent->client->ps.stats[STAT_TIMER] = (ent->client->breather_framenum - level.framenum)/HZ;
	}
	else
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = 0;
		ent->client->ps.stats[STAT_TIMER] = 0;
	}

	//
	// selected item
	//
	if (ent->client->selected_item == -1) {
		ent->client->ps.stats[STAT_SELECTED_ICON] = 0;
    } else {
        item = INDEX_ITEM( ent->client->selected_item );
		ent->client->ps.stats[STAT_SELECTED_ICON] = gi.imageindex (item->icon);
    }

	ent->client->ps.stats[STAT_SELECTED_ITEM] = ent->client->selected_item;

	//
	// layouts
	//
	ent->client->ps.stats[STAT_LAYOUTS] = 0;

    if (ent->client->health <= 0 || level.intermission_framenum || ent->client->showscores)
        ent->client->ps.stats[STAT_LAYOUTS] |= 1;
    if (ent->client->showinventory && ent->client->health > 0)
        ent->client->ps.stats[STAT_LAYOUTS] |= 2;

	//
	// frags
	//
	ent->client->ps.stats[STAT_FRAGS] = ent->client->resp.score;

	//
	// help icon / current weapon if not shown
	//
	if ( (ent->client->pers.hand == CENTER_HANDED || ent->client->ps.fov > 91) && ent->client->weapon)
		ent->client->ps.stats[STAT_HELPICON] = gi.imageindex (ent->client->weapon->icon);
	else
		ent->client->ps.stats[STAT_HELPICON] = 0;

	ent->client->ps.stats[STAT_SPECTATOR] = 0;
	ent->client->ps.stats[STAT_CHASE] = 0;

    if( level.intermission_framenum ) {
	    ent->client->ps.stats[STAT_TIME_STRING] = 0;
        ent->client->ps.stats[STAT_FRAGS_STRING] = 0;
        ent->client->ps.stats[STAT_DELTA_STRING] = 0;
        ent->client->ps.stats[STAT_RANK_STRING] = 0;
    } else {
        if( timelimit->value > 0 ) {
    	    ent->client->ps.stats[STAT_TIME_STRING] = CS_TIME;
        }
        if( ent->client->pers.connected == CONN_SPAWNED ) {
            ent->client->ps.stats[STAT_FRAGS_STRING] = CS_PRIVATE + PCS_FRAGS;
            ent->client->ps.stats[STAT_DELTA_STRING] = CS_PRIVATE + PCS_DELTA;
            ent->client->ps.stats[STAT_RANK_STRING] = CS_PRIVATE + PCS_RANK;
        } else {
            ent->client->ps.stats[STAT_FRAGS_STRING] = CS_OBSERVE;
            ent->client->ps.stats[STAT_DELTA_STRING] = 0;
            ent->client->ps.stats[STAT_RANK_STRING] = 0;
        }
    }

}

