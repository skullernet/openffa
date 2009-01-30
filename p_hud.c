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
    PMenu_Close( ent );

	ent->client->layout = LAYOUT_SCORES;
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
	ent->client->grenade_state = GRENADE_NONE;
	ent->client->grenade_framenum = 0;

	ent->watertype = 0;
	ent->waterlevel = 0;
	ent->viewheight = 0;
	ent->s.modelindex = 0;
	ent->s.modelindex2 = 0;
	ent->s.modelindex3 = 0;
	ent->s.modelindex = 0;
	ent->s.effects = 0;
	ent->s.sound = 0;
	ent->s.event = 0;
	ent->solid = SOLID_NOT;
    ent->svflags = SVF_NOCLIENT;
    gi.unlinkentity( ent );

    if( PLAYER_SPAWNED( ent ) || ent->client->chase_target ) {
        Cmd_Stats_f( ent, qfalse );
    }

	// add the layout
	DeathmatchScoreboardMessage (ent);
	gi.unicast (ent, qtrue);

    if( ent->client->pers.uf & UF_AUTOSCREENSHOT ) {
        gi.WriteByte( svc_stufftext );
        gi.WriteString( "wait; screenshot\n" );
	    gi.unicast (ent, qtrue);
    }
}

void BeginIntermission (void)
{
	int		i;
	edict_t	*ent, *client;

	if (level.intermission_framenum)
		return;		// already activated

	level.intermission_framenum = level.framenum;
    level.vote.proposal = 0;

	// respawn any dead clients
	for (i=0 ; i<game.maxclients ; i++)
	{
		client = g_edicts + 1 + i;
		if (!client->inuse)
			continue;
		if (client->health <= 0)
			respawn(client);
	}

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

    if( ent ) {
    	VectorCopy (ent->s.origin, level.intermission_origin);
	    VectorCopy (ent->s.angles, level.intermission_angle);
    }

	// move all clients to the intermission point
	for (i=0 ; i<game.maxclients ; i++)
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
        return -1;
    }
    if( a->resp.score < b->resp.score ) {
        return 1;
    }
    if( a->resp.deaths < b->resp.deaths ) {
        return -1;
    }
    if( a->resp.deaths > b->resp.deaths ) {
        return 1;
    }
    return 0;
}

int G_CalcRanks( gclient_t **ranks ) {
    int i, total;

	// sort the clients by score, then by eff
	total = 0;
	for( i = 0; i < game.maxclients; i++ ) {
        if( game.clients[i].pers.connected == CONN_SPAWNED ) {
            if( ranks ) {
                ranks[total] = &game.clients[i];
            }
            total++;
        }
	}

    if( ranks ) {
        qsort( ranks, total, sizeof( gclient_t * ), G_PlayerCmp );
    }

    return total;
}


void HighScoresMessage( void ) {
	char	entry[MAX_STRING_CHARS];
	char	string[MAX_STRING_CHARS];
    char    date[MAX_QPATH];
    struct tm   *tm;
    score_t *s;
	size_t	total, len;
	int		i;
    int     y;

	total = Q_snprintf( string, sizeof( string ),
        "xv 0 "
        "yv 0 "
        "cstring \"High Scores for %s\""
        "yv 16 "
        "cstring2 \"  # Name             FPH Date      \"",
        level.mapname );

    y = 24;
	for( i = 0; i < level.numscores; i++ ) {
        s = &level.scores[i];

        tm = localtime( &s->time );
        len = strftime( date, sizeof( date ), "%Y-%m-%d", tm );
        if( len < 1 ) {
            strcpy( date, "???" );
        }
		len = Q_snprintf( entry, sizeof( entry ),
		    "yv %d cstring \"%c%2d %-15.15s %4d %-10s\"",
            y, s->time == level.record ? '*' : ' ',
            i + 1, s->name, s->score, date );
        if( len >= sizeof( entry ) ) {
            continue;
        }
        if( total + len >= MAX_STRING_CHARS )
            break;
        memcpy( string + total, entry, len );
        total += len;
        y += 8;
    }
	string[total] = 0;

	gi.WriteByte( svc_layout );
	gi.WriteString( string );
}


/*
==================
DeathmatchScoreboardMessage

==================
*/
void DeathmatchScoreboardMessage( edict_t *ent ) {
	char	entry[MAX_STRING_CHARS];
	char	string[MAX_STRING_CHARS];
    char    status[MAX_QPATH];
	size_t	total, len;
	int		i, j, numranks;
    int     y, sec, eff;
	gclient_t	*ranks[MAX_CLIENTS];
	gclient_t	*c;
    time_t      t;
    struct tm   *tm;

    t = time( NULL );
    tm = localtime( &t );
    strftime( status, sizeof( status ), "[%Y-%m-%d %H:%M]", tm );

	total = Q_scnprintf( string, sizeof( string ),
        "xv 0 "
        "yv 18 "
        "cstring \"%s\""
        "xv -16 "
        "yv 26 "
        "string \"Player          Frg Dth Eff%% FPH Time Ping\""
        "xv -40 ", status );

    numranks = G_CalcRanks( ranks );

	// add the clients sorted by rank
    y = 34;
	for( i = 0; i < numranks; i++ ) {
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

		len = Q_snprintf( entry, sizeof( entry ),
		    "yv %d string%s \"%2d %-15s %3d %3d %3d %4d %4d %4d\"",
            y, c == ent->client ? "" : "2", i + 1,
            c->pers.netname, c->resp.score, c->resp.deaths, eff,
            c->resp.score * 3600 / sec, sec / 60, c->ping );
        if( len >= sizeof( entry ) ) {
            continue;
        }
        if( total + len >= MAX_STRING_CHARS )
            break;
        memcpy( string + total, entry, len );
        total += len;
        y += 8;
    }

    // add spectators in fixed order
	for( i = 0, j = 0; i < game.maxclients; i++ ) {
        c = &game.clients[i];
        if( c->pers.connected != CONN_PREGAME && c->pers.connected != CONN_SPECTATOR ) {
            continue;
        }
        if( c->pers.flags & CPF_MVDSPEC ) {
            continue;
        }

        sec = ( level.framenum - c->level.enter_framenum ) / HZ;
        if( !sec ) {
            sec = 1;
        }

        if( c->chase_target ) {
            Q_snprintf( status, sizeof( status ), "(chasing %.8s)",
                c->chase_target->client->pers.netname );
        } else {
            strcpy( status, "(observing)" );
        }

		len = Q_snprintf( entry, sizeof( entry ),
		    "yv %d string%s \"   %-15s %-18s%3d %4d\"",
            y, c == ent->client ? "" : "2",
            c->pers.netname, status, sec / 60, c->ping );
        if( len >= sizeof( entry ) ) {
            continue;
        }
        if( total + len >= MAX_STRING_CHARS )
            break;
        memcpy( string + total, entry, len );
        total += len;
        y += 8;
        j++;
    }

    // add server info
    if( sv_hostname && sv_hostname->string[0] ) {
        len = Q_scnprintf( entry, sizeof( entry ), "xl 8 yb -37 string2 \"%s\"",
            sv_hostname->string );
        if( total + len < MAX_STRING_CHARS ) {
            memcpy( string + total, entry, len );
            total += len;
        }
    }

    string[total] = 0;

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
	DeathmatchScoreboardMessage (ent);
	gi.unicast (ent, qtrue);
}


/*
==================
Cmd_Score_f

Display the scoreboard
==================
*/
void Cmd_Score_f (edict_t *ent) {
	if (ent->client->layout == LAYOUT_SCORES) {
		ent->client->layout = 0;
		return;
	}

	ent->client->layout = LAYOUT_SCORES;
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
        gi.error( "%s: index %d out of range", __func__, index );
    }

    if( !strcmp( ent->client->level.strings[index], string ) ) {
        return; // not changed
    }

    // save new string
    Q_strlcpy( ent->client->level.strings[index], string, MAX_NETNAME );

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
=============
visible

returns 1 if the entity is visible to self, even if not infront ()
=============
*/
static qboolean visible (edict_t *self, edict_t *other, int mask) {
    vec3_t  spot1;
    vec3_t  spot2;
    trace_t trace;
    int     i;

    VectorCopy (self->s.origin, spot1);
    spot1[2] += self->viewheight;

    VectorCopy (other->s.origin, spot2);
    spot2[2] += other->viewheight;

    for (i = 0; i < 10; i++) {
        gi_trace (&trace, spot1, vec3_origin, vec3_origin, spot2, self, mask);

        if (trace.fraction == 1.0)
            return qtrue;

        // entire move is inside water volume
        if (trace.allsolid && (trace.contents & MASK_WATER)) {
            mask &= ~MASK_WATER;
            continue;
        }

        // hit transparent water
        if (trace.ent == world && trace.surface &&
            (trace.surface->flags & (SURF_TRANS33|SURF_TRANS66)))
        {
            mask &= ~MASK_WATER;
            VectorCopy (trace.endpos, spot1);
            continue;
        }

        break;
    }
    return qfalse;
}

/*
==============
TDM_GetPlayerIdView

Find the best player for the id view and return configstring index.
Code below comes from OpenTDM.
==============
*/
static edict_t *find_by_tracing( edict_t *ent ) {
    edict_t     *ignore;
    vec3_t      forward;
    trace_t     tr;
    vec3_t      start;
    vec3_t      mins = { -4, -4, -4 };
    vec3_t      maxs = { 4, 4, 4 };
    int         i;
    int         tracemask;

    VectorCopy (ent->s.origin, start);
    start[2] += ent->viewheight;

    AngleVectors (ent->client->v_angle, forward, NULL, NULL);

    VectorScale (forward, 4096, forward);
    VectorAdd (ent->s.origin, forward, forward);

    ignore = ent;

    tracemask = CONTENTS_SOLID|CONTENTS_MONSTER|MASK_WATER;

    // find best player through tracing
    for (i = 0; i < 10; i++) {
        gi_trace (&tr, start, mins, maxs, forward, ignore, tracemask);

        // entire move is inside water volume
        if (tr.allsolid && (tr.contents & MASK_WATER)) {
            tracemask &= ~MASK_WATER;
            continue;
        }

        // hit transparent water
        if (tr.ent == world && tr.surface &&
            (tr.surface->flags & (SURF_TRANS33|SURF_TRANS66)))
        {
            tracemask &= ~MASK_WATER;
            VectorCopy (tr.endpos, start);
            continue;
        }

        if (tr.ent == world || tr.fraction == 1.0f)
            break;

        // we hit something that's a player and it's alive!
        // note, we trace twice so we hit water planes
        if (tr.ent && tr.ent->client && tr.ent->health > 0 &&
            visible (tr.ent, ent, CONTENTS_SOLID | MASK_WATER))
        {
            return tr.ent;
        }

        VectorCopy (tr.endpos, start);
        ignore = tr.ent;
    }

    return NULL;
}

static edict_t *find_by_angles( edict_t *ent ) {
    vec3_t      forward;
    edict_t     *who, *best;
    vec3_t      dir;
    float       distance, bdistance = 0.0f;
    float       bd = 0.0f, d;

    AngleVectors (ent->client->v_angle, forward, NULL, NULL);
    best = NULL;

    // if trace was unsuccessful, try guessing based on angles
    for (who = g_edicts + 1; who <= g_edicts + game.maxclients; who++) {
        if (!who->inuse)
            continue;
        if (!PLAYER_SPAWNED(who))
            continue;
        if (who->health <= 0)
            continue;
        
        if (who == ent)
            continue;

        VectorSubtract (who->s.origin, ent->s.origin, dir);
        distance = VectorLength (dir);

        VectorNormalize (dir);
        d = DotProduct (forward, dir);

        // note, we trace twice so we hit water planes
        if (d > bd &&
            visible (ent, who, CONTENTS_SOLID | MASK_WATER) &&
            visible (who, ent, CONTENTS_SOLID | MASK_WATER))
        {
            bdistance = distance;
            bd = d;
            best = who;
        }
    }

    if (!best) {
        return NULL;
    }

    // allow variable slop based on proximity
    if ((bdistance < 150 && bd > 0.50f) ||
        (bdistance < 250 && bd > 0.90f) ||
        (bdistance < 600 && bd > 0.96f) ||
        bd > 0.98f)
    {
        return best;
    }

    return NULL;
}

int G_GetPlayerIdView( edict_t *ent ) {
    edict_t *target;

    target = find_by_tracing( ent );
    if( !target ) {
        target = find_by_angles( ent );
        if( !target ) {
            return 0;
        }
    }

    return CS_PLAYERNAMES + ( target - g_edicts ) - 1;
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
	// timer 1 (quad, enviro, breather)
	//
	if (ent->client->quad_framenum > level.framenum)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = level.images.quad;
		ent->client->ps.stats[STAT_TIMER] = (ent->client->quad_framenum - level.framenum)/HZ;
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
	// timer 2 (pent)
	//
	ent->client->ps.stats[STAT_TIMER2_ICON] = 0;
	ent->client->ps.stats[STAT_TIMER2] = 0;
	if (ent->client->invincible_framenum > level.framenum)
	{
		if( ent->client->ps.stats[STAT_TIMER_ICON] ) {
    		ent->client->ps.stats[STAT_TIMER2_ICON] = level.images.invulnerability;
		    ent->client->ps.stats[STAT_TIMER2] = (ent->client->invincible_framenum - level.framenum)/HZ;
        } else {
    		ent->client->ps.stats[STAT_TIMER_ICON] = level.images.invulnerability;
		    ent->client->ps.stats[STAT_TIMER] = (ent->client->invincible_framenum - level.framenum)/HZ;
        }
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

    if (ent->client->health <= 0 || level.intermission_framenum || ent->client->layout )
        ent->client->ps.stats[STAT_LAYOUTS] |= 1;

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

    if( ent->client->pers.connected == CONN_PREGAME ) {
	    ent->client->ps.stats[STAT_SPECTATOR] = CS_PREGAME;
    } else if( ent->client->pers.connected == CONN_SPECTATOR ) {
    	ent->client->ps.stats[STAT_SPECTATOR] = CS_SPECMODE;
    } else {
    	ent->client->ps.stats[STAT_SPECTATOR] = 0;
    }
	ent->client->ps.stats[STAT_CHASE] = 0;

    if( level.intermission_framenum ) {
	    ent->client->ps.stats[STAT_TIME_STRING] = 0;
        ent->client->ps.stats[STAT_FRAGS_STRING] = 0;
        ent->client->ps.stats[STAT_DELTA_STRING] = 0;
        ent->client->ps.stats[STAT_RANK_STRING] = 0;
        ent->client->ps.stats[STAT_VIEWID] = 0;
    } else {
        if( timelimit->value > 0 ) {
    	    ent->client->ps.stats[STAT_TIME_STRING] = CS_TIME;
        } else {
    	    ent->client->ps.stats[STAT_TIME_STRING] = 0;
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
        if( ent->client->pers.flags & CPF_NOVIEWID ) {
            ent->client->ps.stats[STAT_VIEWID] = 0;
        } else {
            ent->client->ps.stats[STAT_VIEWID] = G_GetPlayerIdView( ent );
        }
    }

}

