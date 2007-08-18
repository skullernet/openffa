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
#include "m_player.h"


char *ClientTeam (edict_t *ent)
{
	char		*p;
	static char	value[512];

	value[0] = 0;

	if (!ent->client)
		return value;

	strcpy(value, Info_ValueForKey (ent->client->pers.userinfo, "skin"));
	p = strchr(value, '/');
	if (!p)
		return value;

	if ((int)(dmflags->value) & DF_MODELTEAMS)
	{
		*p = 0;
		return value;
	}

	// if ((int)(dmflags->value) & DF_SKINTEAMS)
	return ++p;
}

qboolean OnSameTeam (edict_t *ent1, edict_t *ent2)
{
	char	ent1Team [512];
	char	ent2Team [512];

	if (!((int)(dmflags->value) & (DF_MODELTEAMS | DF_SKINTEAMS)))
		return qfalse;

	strcpy (ent1Team, ClientTeam (ent1));
	strcpy (ent2Team, ClientTeam (ent2));

	if (strcmp(ent1Team, ent2Team) == 0)
		return qtrue;
	return qfalse;
}


void SelectNextItem (edict_t *ent, int itflags)
{
	gclient_t	*cl;
	int			i, index;
	const gitem_t		*it;

	cl = ent->client;

	if (cl->chase_target) {
		ChaseNext(ent);
		return;
	}

	// scan  for the next valid one
	for (i=1 ; i<=ITEM_TOTAL ; i++)
	{
		index = (cl->selected_item + i)%ITEM_TOTAL;
		if (!cl->inventory[index])
			continue;
		it = INDEX_ITEM(index);
		if (!it->use)
			continue;
		if (!(it->flags & itflags))
			continue;

		cl->selected_item = index;
		return;
	}

	cl->selected_item = -1;
}

void SelectPrevItem (edict_t *ent, int itflags)
{
	gclient_t	*cl;
	int			i, index;
	const gitem_t		*it;

	cl = ent->client;

	if (cl->chase_target) {
		ChasePrev(ent);
		return;
	}

	// scan  for the next valid one
	for (i=1 ; i<=ITEM_TOTAL ; i++)
	{
		index = (cl->selected_item + ITEM_TOTAL - i)%ITEM_TOTAL;
		if (!cl->inventory[index])
			continue;
		it = INDEX_ITEM(index);
		if (!it->use)
			continue;
		if (!(it->flags & itflags))
			continue;

		cl->selected_item = index;
		return;
	}

	cl->selected_item = -1;
}

void ValidateSelectedItem (edict_t *ent)
{
	gclient_t	*cl;

	cl = ent->client;

	if (cl->inventory[cl->selected_item])
		return;		// valid

	SelectNextItem (ent, -1);
}


//=================================================================================

static qboolean CheckCheats( edict_t *ent ) {
	if( !sv_cheats->value ) {
		gi.cprintf( ent, PRINT_HIGH, "Cheats are disabled on this server.\n" );
		return qfalse;
	}

    return qtrue;
}

/*
==================
Cmd_Give_f

Give items to a client
==================
*/
void Cmd_Give_f (edict_t *ent)
{
	char		*name;
	gitem_t		*it;
	int			index;
	int			i;
	qboolean	give_all;
	edict_t		*it_ent;

    if( !CheckCheats( ent ) ) {
        return;
    }

	name = gi.args();

	if (Q_stricmp(name, "all") == 0)
		give_all = qtrue;
	else
		give_all = qfalse;

	if (give_all || Q_stricmp(gi.argv(1), "health") == 0)
	{
		if (gi.argc() == 3)
			ent->health = atoi(gi.argv(2));
		else
			ent->health = ent->max_health;
		if (!give_all)
			return;
	}

	if (give_all || Q_stricmp(name, "weapons") == 0)
	{
		for (i=0 ; i<ITEM_TOTAL ; i++)
		{
			it = INDEX_ITEM(i);
			if (!it->pickup)
				continue;
			if (!(it->flags & IT_WEAPON))
				continue;
			ent->client->inventory[i] += 1;
		}
		if (!give_all)
			return;
	}

	if (give_all || Q_stricmp(name, "ammo") == 0)
	{
		for (i=0 ; i<ITEM_TOTAL ; i++)
		{
			it = INDEX_ITEM(i);
			if (!it->pickup)
				continue;
			if (!(it->flags & IT_AMMO))
				continue;
			Add_Ammo (ent, it, 1000);
		}
		if (!give_all)
			return;
	}

	if (give_all || Q_stricmp(name, "armor") == 0)
	{
		gitem_armor_t	*info;

		ent->client->inventory[ITEM_ARMOR_JACKET] = 0;

		ent->client->inventory[ITEM_ARMOR_COMBAT] = 0;

		it = INDEX_ITEM( ITEM_ARMOR_BODY );
		info = (gitem_armor_t *)it->info;
		ent->client->inventory[ITEM_ARMOR_BODY] = info->max_count;

		if (!give_all)
			return;
	}

	if (give_all || Q_stricmp(name, "Power Shield") == 0)
	{
		it = INDEX_ITEM( ITEM_POWER_SHIELD );
		it_ent = G_Spawn();
		it_ent->classname = it->classname;
		SpawnItem (it_ent, it);
        if( it_ent->inuse ) {
    		Touch_Item (it_ent, ent, NULL, NULL);
		    if (it_ent->inuse)
			    G_FreeEdict(it_ent);
        }

		if (!give_all)
			return;
	}

	if (give_all)
	{
		for (i=0 ; i<ITEM_TOTAL ; i++)
		{
			it = INDEX_ITEM(i);
			if (!it->pickup)
				continue;
			if (it->flags & (IT_ARMOR|IT_WEAPON|IT_AMMO))
				continue;
			ent->client->inventory[i] = 1;
		}
		return;
	}

	it = FindItem (name);
	if (!it)
	{
		name = gi.argv(1);
		it = FindItem (name);
		if (!it)
		{
			gi.cprintf (ent, PRINT_HIGH, "unknown item\n");
			return;
		}
	}

	if (!it->pickup)
	{
		gi.cprintf (ent, PRINT_HIGH, "non-pickup item\n");
		return;
	}

	index = ITEM_INDEX(it);

	if (it->flags & IT_AMMO)
	{
		if (gi.argc() == 3)
			ent->client->inventory[index] = atoi(gi.argv(2));
		else
			ent->client->inventory[index] += it->quantity;
	}
	else
	{
		it_ent = G_Spawn();
		it_ent->classname = it->classname;
		SpawnItem (it_ent, it);
        if( it_ent->inuse ) {
    		Touch_Item (it_ent, ent, NULL, NULL);
		    if (it_ent->inuse)
			    G_FreeEdict(it_ent);
        }
	}
}


/*
==================
Cmd_God_f

Sets client to godmode

argv(0) god
==================
*/
void Cmd_God_f (edict_t *ent)
{
	char	*msg;

    if( !CheckCheats( ent ) ) {
        return;
    }

	ent->flags ^= FL_GODMODE;
	if (!(ent->flags & FL_GODMODE) )
		msg = "godmode OFF\n";
	else
		msg = "godmode ON\n";

	gi.cprintf (ent, PRINT_HIGH, msg);
}


/*
==================
Cmd_Notarget_f

Sets client to notarget

argv(0) notarget
==================
*/
void Cmd_Notarget_f (edict_t *ent)
{
	char	*msg;

    if( !CheckCheats( ent ) ) {
        return;
    }

	ent->flags ^= FL_NOTARGET;
	if (!(ent->flags & FL_NOTARGET) )
		msg = "notarget OFF\n";
	else
		msg = "notarget ON\n";

	gi.cprintf (ent, PRINT_HIGH, msg);
}


/*
==================
Cmd_Noclip_f

argv(0) noclip
==================
*/
void Cmd_Noclip_f (edict_t *ent)
{
	char	*msg;

    if( !CheckCheats( ent ) ) {
        return;
    }

	if (ent->movetype == MOVETYPE_NOCLIP)
	{
		ent->movetype = MOVETYPE_WALK;
		msg = "noclip OFF\n";
	}
	else
	{
		ent->movetype = MOVETYPE_NOCLIP;
		msg = "noclip ON\n";
	}

	gi.cprintf (ent, PRINT_HIGH, msg);
}


/*
==================
Cmd_Use_f

Use an inventory item
==================
*/
void Cmd_Use_f (edict_t *ent)
{
	int			index;
	gitem_t		*it;
	char		*s;

	s = gi.args();
	it = FindItem (s);
	if (!it)
	{
		gi.cprintf (ent, PRINT_HIGH, "Unknown item: %s\n", s);
		return;
	}
	if (!it->use)
	{
		gi.cprintf (ent, PRINT_HIGH, "Item is not usable.\n");
		return;
	}
	index = ITEM_INDEX(it);
	if (!ent->client->inventory[index])
	{
		gi.cprintf (ent, PRINT_HIGH, "Out of item: %s\n", s);
		return;
	}

	it->use (ent, it);
}


/*
==================
Cmd_Drop_f

Drop an inventory item
==================
*/
void Cmd_Drop_f (edict_t *ent)
{
	int			index;
	gitem_t		*it;
	char		*s;

	s = gi.args();
	it = FindItem (s);
	if (!it)
	{
		gi.cprintf (ent, PRINT_HIGH, "Unknown item: %s\n", s);
		return;
	}
	if (!it->drop)
	{
		gi.cprintf (ent, PRINT_HIGH, "Item is not dropable.\n");
		return;
	}
	index = ITEM_INDEX(it);
	if (!ent->client->inventory[index])
	{
		gi.cprintf (ent, PRINT_HIGH, "Out of item: %s\n", s);
		return;
	}

	it->drop (ent, it);
}


/*
=================
Cmd_Inven_f
=================
*/
void Cmd_Inven_f (edict_t *ent)
{
	int			i;
	gclient_t	*cl;

	cl = ent->client;

	cl->showscores = qfalse;
	cl->showhelp = qfalse;

	if (cl->showinventory)
	{
		cl->showinventory = qfalse;
		return;
	}

	cl->showinventory = qtrue;

	gi.WriteByte (svc_inventory);
	for (i=0 ; i<MAX_ITEMS ; i++)
	{
		gi.WriteShort (cl->inventory[i]);
	}
	gi.unicast (ent, qtrue);
}

/*
=================
Cmd_InvUse_f
=================
*/
void Cmd_InvUse_f (edict_t *ent)
{
	gitem_t		*it;

	ValidateSelectedItem (ent);

	if (ent->client->selected_item == -1)
	{
		gi.cprintf (ent, PRINT_HIGH, "No item to use.\n");
		return;
	}

	it = INDEX_ITEM( ent->client->selected_item );
	if (!it->use)
	{
		gi.cprintf (ent, PRINT_HIGH, "Item is not usable.\n");
		return;
	}
	it->use (ent, it);
}

/*
=================
Cmd_WeapPrev_f
=================
*/
void Cmd_WeapPrev_f (edict_t *ent)
{
	gclient_t	*cl;
	int			i, index;
	gitem_t		*it;
	int			selected_weapon;

	cl = ent->client;

	if (!cl->weapon)
		return;

	selected_weapon = ITEM_INDEX(cl->weapon);

	// scan  for the next valid one
	for (i=1 ; i<=ITEM_TOTAL ; i++)
	{
		index = (selected_weapon + i)%ITEM_TOTAL;
		if (!cl->inventory[index])
			continue;
		it = INDEX_ITEM( index );
		if (!it->use)
			continue;
		if (! (it->flags & IT_WEAPON) )
			continue;
		it->use (ent, it);
		if (cl->weapon == it)
			return;	// successful
	}
}

/*
=================
Cmd_WeapNext_f
=================
*/
void Cmd_WeapNext_f (edict_t *ent)
{
	gclient_t	*cl;
	int			i, index;
	gitem_t		*it;
	int			selected_weapon;

	cl = ent->client;

	if (!cl->weapon)
		return;

	selected_weapon = ITEM_INDEX(cl->weapon);

	// scan  for the next valid one
	for (i=1 ; i<=ITEM_TOTAL ; i++)
	{
        index = (selected_weapon + ITEM_TOTAL - i)%ITEM_TOTAL;
		if (!cl->inventory[index])
			continue;
		it = INDEX_ITEM( index );
		if (!it->use)
			continue;
		if (! (it->flags & IT_WEAPON) )
			continue;
		it->use (ent, it);
		if (cl->weapon == it)
			return;	// successful
	}
}

/*
=================
Cmd_WeapLast_f
=================
*/
void Cmd_WeapLast_f (edict_t *ent)
{
	gclient_t	*cl;
	int			index;
	gitem_t		*it;

	cl = ent->client;

	if (!cl->weapon || !cl->lastweapon)
		return;

	index = ITEM_INDEX(cl->lastweapon);
	if (!cl->inventory[index])
		return;
	it = INDEX_ITEM( index );
	if (!it->use)
		return;
	if (! (it->flags & IT_WEAPON) )
		return;
	it->use (ent, it);
}

/*
=================
Cmd_InvDrop_f
=================
*/
void Cmd_InvDrop_f (edict_t *ent)
{
	gitem_t		*it;

	ValidateSelectedItem (ent);

	if (ent->client->selected_item == -1)
	{
		gi.cprintf (ent, PRINT_HIGH, "No item to drop.\n");
		return;
	}

	it = INDEX_ITEM( ent->client->selected_item );
	if (!it->drop)
	{
		gi.cprintf (ent, PRINT_HIGH, "Item is not dropable.\n");
		return;
	}
	it->drop (ent, it);
}

/*
=================
Cmd_Kill_f
=================
*/
void Cmd_Kill_f (edict_t *ent)
{
	if(level.framenum - ent->client->respawn_framenum < 5*HZ)
		return;
	ent->flags &= ~FL_GODMODE;
	ent->health = 0;
	meansOfDeath = MOD_SUICIDE;
	player_die (ent, ent, ent, 100000, vec3_origin);
}

/*
=================
Cmd_PutAway_f
=================
*/
void Cmd_PutAway_f (edict_t *ent)
{
	ent->client->showscores = qfalse;
	ent->client->showhelp = qfalse;
	ent->client->showinventory = qfalse;
}


/*
=================
Cmd_Wave_f
=================
*/
void Cmd_Wave_f (edict_t *ent)
{
	int		i;

	i = atoi (gi.argv(1));

	// can't wave when ducked
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
		return;

	if (ent->client->anim_priority > ANIM_WAVE)
		return;

	ent->client->anim_priority = ANIM_WAVE;

	switch (i)
	{
	case 0:
		gi.cprintf (ent, PRINT_HIGH, "flipoff\n");
		ent->s.frame = FRAME_flip01-1;
		ent->client->anim_end = FRAME_flip12;
		break;
	case 1:
		gi.cprintf (ent, PRINT_HIGH, "salute\n");
		ent->s.frame = FRAME_salute01-1;
		ent->client->anim_end = FRAME_salute11;
		break;
	case 2:
		gi.cprintf (ent, PRINT_HIGH, "taunt\n");
		ent->s.frame = FRAME_taunt01-1;
		ent->client->anim_end = FRAME_taunt17;
		break;
	case 3:
		gi.cprintf (ent, PRINT_HIGH, "wave\n");
		ent->s.frame = FRAME_wave01-1;
		ent->client->anim_end = FRAME_wave11;
		break;
	case 4:
	default:
		gi.cprintf (ent, PRINT_HIGH, "point\n");
		ent->s.frame = FRAME_point01-1;
		ent->client->anim_end = FRAME_point12;
		break;
	}
}

/*
==================
Cmd_Say_f
==================
*/
void Cmd_Say_f (edict_t *ent, qboolean team, qboolean arg0)
{
	int		i, j;
	edict_t	*other;
	char	text[150];
	gclient_t *cl = ent->client;

	if (gi.argc () < 2 && !arg0)
		return;

	if (!((int)(dmflags->value) & (DF_MODELTEAMS | DF_SKINTEAMS)))
		team = qfalse;

	if (team)
		Com_sprintf (text, sizeof(text), "(%s): ", cl->pers.netname);
	else
		Com_sprintf (text, sizeof(text), "%s: ", cl->pers.netname);

	if (arg0) {
		Q_strcat (text, sizeof(text), gi.argv(0));
		Q_strcat (text, sizeof(text), " ");
    }
	Q_strcat (text, sizeof(text), gi.args());

    j = flood_msgs->value;
	if (j > 0) {
        if (level.framenum < cl->flood_locktill) {
			gi.cprintf(ent, PRINT_HIGH, "You can't talk for %d more seconds\n",
				( cl->flood_locktill - level.framenum ) / HZ );
            return;
        }
        i = cl->flood_whenhead - j + 1;
		if (i >= 0 && level.framenum - cl->flood_when[i % FLOOD_MSGS] < flood_persecond->value*HZ) {
            j = flood_waitdelay->value;
			cl->flood_locktill = level.framenum + j*HZ;
			gi.cprintf(ent, PRINT_CHAT, "Flood protection: "
                "You can't talk for %d seconds.\n", j);
            return;
        }
		cl->flood_when[++cl->flood_whenhead % FLOOD_MSGS] = level.framenum;
	}

	if (dedicated->value)
		gi.cprintf(NULL, PRINT_CHAT, "%s\n", text);

	for (j = 1; j <= game.maxclients; j++)
	{
		other = &g_edicts[j];
		if (!other->inuse)
			continue;
		if (!other->client)
			continue;
		if (team)
		{
			if (!OnSameTeam(ent, other))
				continue;
		}
		gi.cprintf(other, PRINT_CHAT, "%s\n", text);
	}
}

void Cmd_Observe_f(edict_t *ent) {
    if( level.framenum - ent->client->respawn_framenum < 5*HZ ) {
        return;
    }
    if( ent->client->pers.connected == CONN_PREGAME ) {
        ent->client->pers.connected = CONN_SPECTATOR;
		gi.cprintf( ent, PRINT_HIGH, "Changed to spectator mode.\n" );
        return;
    }
    if( ent->client->pers.connected == CONN_SPECTATOR ) {
        ent->client->pers.connected = CONN_SPAWNED;
    } else {
        ent->client->pers.connected = CONN_SPECTATOR;
    }
    spectator_respawn( ent );
}

edict_t *G_SetPlayer( edict_t *ent, int arg ) {
    edict_t     *other;
	int			i;
	char		*s;

	s = gi.argv(arg);

	// numeric values are just slot numbers
	for( i = 0; s[i]; i++ ) {
		if( !Q_isdigit( s[i] ) ) {
			break;
		}
	} 
	if( !s[i] ) {
		i = atoi(s);
		if (i < 0 || i >= game.maxclients) {
			gi.cprintf (ent, PRINT_HIGH, "Bad client slot number: %d\n", i);
			return NULL;
		}

        other = &g_edicts[ i + 1 ];
		if (!other->client || other->client->pers.connected <= CONN_CONNECTED) {
			gi.cprintf (ent, PRINT_HIGH, "Client #%d is not active.\n", i);
			return NULL;
		}
		return other;
	}

	// check for a name match
    for( i = 0; i < game.maxclients; i++ ) {
        other = &g_edicts[ i + 1 ];
		if (!other->client ) {
            continue;
        }
        if( other->client->pers.connected <= CONN_CONNECTED) {
            continue;
        }
		if (!strcmp(other->client->pers.netname, s)) {
			return other;
		}
	}

	Com_Printf ("Client \"%s\" is not on the server.\n", s);
	return NULL;
}

static const char weapnames[WEAP_TOTAL][16] = {
    "None", "Blaster", "Shotgun", "S.Shotgun", "Machinegun",
    "Chaingun", "Grenades", "G.Launcher", "R.Launcher",
    "H.Blaster", "Railgun", "BFG10K"
};

static void Cmd_Stats_f( edict_t *ent ) {
    int i;
    weapstat_t *s;
    char acc[16];
    char hits[16];
    char frgs[16];
    char dths[16];
    edict_t *other;

    if( gi.argc() > 1 ) {
        other = G_SetPlayer( ent, 1 );
        if( !other ) {
            return;
        }
    } else {
        other = ent;
    }

    for( i = WEAP_SHOTGUN; i < WEAP_BFG; i++ ) {
        s = &other->client->resp.stats[i];
        if( s->atts || s->deaths ) {
            break;
        }
    }
    if( i == WEAP_BFG ) {
        gi.cprintf( ent, PRINT_HIGH, "No stats available for %s.\n",
            other->client->pers.netname );
        return;
    }

    gi.cprintf( ent, PRINT_HIGH,
        "Statistics for %s:\n"
        "Weapon     Acc%% Hits/Atts Frgs Dths\n"
        "---------- ---- --------- ---- ----\n",
        other->client->pers.netname );

    for( i = WEAP_SHOTGUN; i < WEAP_BFG; i++ ) {
        s = &other->client->resp.stats[i];
        if( !s->atts && !s->deaths ) {
            continue;
        }
        if( s->atts ) {
            sprintf( acc, "%3i%%", s->hits * 100 / s->atts );
            sprintf( hits, "%4d/%-4d", s->hits, s->atts );
            if( s->frags ) {
                sprintf( frgs, "%4d", s->frags );
            } else {
                strcpy( frgs, "    " );
            }
        }
        if( s->deaths ) {
            sprintf( dths, "%4d", s->deaths );
        } else {
            strcpy( dths, "    " );
        }

        gi.cprintf( ent, PRINT_HIGH,
            "%-10s %s %s %s %s\n",
            weapnames[i], acc, hits, frgs, dths );
    }

    gi.cprintf( ent, PRINT_HIGH,
        "Total damage given/recvd: %d/%d\n",
        other->client->resp.damage_given,
        other->client->resp.damage_recvd );
}

static void Cmd_CastVote_f( edict_t *ent, qboolean accepted ) {
    if( !level.vote.framenum ) {
        gi.cprintf( ent, PRINT_HIGH, "There is no vote in progress.\n" );
        return;
    }
    if( ent->client->level.vote.index == level.vote.index ) {
        gi.cprintf( ent, PRINT_HIGH, "You have already voted.\n" );
        return;
    }

    ent->client->level.vote.index = level.vote.index;
    ent->client->level.vote.accepted = accepted;

    if( G_CheckVote() ) {
        return;
    }
    
    gi.cprintf( ent, PRINT_HIGH, "Vote cast.\n" );
}

typedef struct {
    char name[16];
    int bit;
    qboolean (*func)( edict_t * );
} vote_proposal_t;

static qboolean Vote_Timelimit( edict_t *ent ) {
    int num = atoi( gi.argv( 2 ) );

    if( num < 0 ) {
        gi.cprintf( ent, PRINT_HIGH, "Timelimit %d is invalid.\n", num );
        return qfalse;
    }
    if( num == timelimit->value ) {
        gi.cprintf( ent, PRINT_HIGH, "Timelimit is already set to %d.\n", num );
        return qfalse;
    }
    level.vote.value = num;
    return qtrue;
}

static qboolean Vote_Fraglimit( edict_t *ent ) {
    int num = atoi( gi.argv( 2 ) );

    if( num < 0 ) {
        gi.cprintf( ent, PRINT_HIGH, "Fraglimit %d is invalid.\n", num );
        return qfalse;
    }
    if( num == fraglimit->value ) {
        gi.cprintf( ent, PRINT_HIGH, "Fraglimit is already set to %d.\n", num );
        return qfalse;
    }
    level.vote.value = num;
    return qtrue;
}

int G_CalcVote( int *acc, int *rej ) {
    int i;
    gclient_t *client;
    int total = 0, accepted = 0, rejected = 0;

    for( i = 0, client = game.clients; i < game.maxclients; i++, client++ ) {
        if( client->pers.connected <= CONN_CONNECTED ) {
            continue;
        }
        total++;
        if( client->level.vote.index == level.vote.index ) {
            if( client->level.vote.accepted ) {
                accepted++;
            } else {
                rejected++;
            }
        }
    }

    if( !total ) {
        *acc = *rej = 0;
        return 0;
    }

    *acc = accepted * 100 / total;
    *rej = rejected * 100 / total;

    return total;
}

qboolean G_CheckVote( void ) {
    int treshold = g_vote_treshold->value;
    int acc, rej;
    
    // is our victim gone?
    if( level.vote.victim && !level.vote.victim->pers.connected ) {
        gi.bprintf( PRINT_HIGH, "Vote aborted.\n" );
        goto finish;
    }

    if( !G_CalcVote( &acc, &rej ) ) {
        goto finish;
    }

    if( acc > treshold ) {
        switch( level.vote.proposal ) {
        case VOTE_TIMELIMIT:
            gi.bprintf( PRINT_HIGH, "Vote passed. Timelimit set to %d.\n", level.vote.value );
            gi.AddCommandString( va( "set timelimit %d\n", level.vote.value ) );
            break;
        case VOTE_FRAGLIMIT:
            gi.bprintf( PRINT_HIGH, "Vote passed. Fraglimit set to %d.\n", level.vote.value );
            gi.AddCommandString( va( "set fraglimit %d\n", level.vote.value ) );
            break;
        case VOTE_KICK:
            gi.bprintf( PRINT_HIGH, "Vote passed. Kicking %s...\n", level.vote.victim->pers.netname );
            gi.AddCommandString( va( "kick %d\n", level.vote.victim - game.clients ) );
            break;
        case VOTE_MUTE:
            gi.bprintf( PRINT_HIGH, "Vote passed. Muting %s...\n", level.vote.victim->pers.netname );
//            level.vote.victim->pers.muted = qtrue;
            break;
        case VOTE_MAP:
            gi.bprintf( PRINT_HIGH, "Vote passed. Next map set to %s.\n", level.nextmap );
            BeginIntermission();
            break;
        default:
            break;
        }
        goto finish;
    }

    if( rej > treshold ) {
        gi.bprintf( PRINT_HIGH, "Vote failed.\n" );
        goto finish;
    }

    return qfalse;

finish:
    level.vote.proposal = 0;
    level.vote.framenum = level.framenum;
    return qtrue;
}


void G_BuildProposal( char *buffer ) {
    switch( level.vote.proposal ) {
    case VOTE_TIMELIMIT:
        sprintf( buffer, "set timelimit to %d", level.vote.value );
        break;
    case VOTE_FRAGLIMIT:
        sprintf( buffer, "set fraglimit to %d", level.vote.value );
        break;
    case VOTE_KICK:
        sprintf( buffer, "kick player \"%s\"", level.vote.victim->pers.netname );
        break;
    case VOTE_MUTE:
        sprintf( buffer, "mute player \"%s\"", level.vote.victim->pers.netname );
        break;
    case VOTE_MAP:
        sprintf( buffer, "change map to \"%s\"", level.nextmap );
        break;
    default:
        strcpy( buffer, "unknown" );
        break;
    }
}

#if 0
static void Vote_Items( edict_t *ent ) {
    if( *s == '+' || *s == '-' ) {
        for( v = vote_subjects; v->bit; v++ ) {
            if( !( v->bit & VOTE_TOGGLE ) ) {
                continue;
            }
            if( !strcmp( s + 1, v->name ) ) {
                break;
            }
        }
        if( !v->bit ) {
            gi.cprintf( ent, PRINT_HIGH,
                "Unknown item '%s'. Type 'vote help' for usage.\n", s + 1 );
            return;
        }
    }
}
#endif

static qboolean Vote_Victim( edict_t *ent ) {
    edict_t *other = G_SetPlayer( ent, 2 );
    if( !other ) {
        return qfalse;
    }

    if( other == ent ) {
        gi.cprintf( ent, PRINT_HIGH, "You can't kick yourself.\n" );
        return qfalse;
    }

    level.vote.victim = other->client;
    return qtrue;
}

static qboolean Vote_Map( edict_t *ent ) {
    return qfalse;
}

static const vote_proposal_t vote_proposals[] = {
    { "timelimit", VOTE_TIMELIMIT, Vote_Timelimit },
    { "tl", VOTE_TIMELIMIT, Vote_Timelimit },
    { "fraglimit", VOTE_FRAGLIMIT, Vote_Fraglimit },
    { "fl", VOTE_FRAGLIMIT, Vote_Fraglimit },
//    { "items", VOTE_ITEMS, Vote_Items },
    { "kick", VOTE_KICK, Vote_Victim },
    { "mute", VOTE_MUTE, Vote_Victim },
    { "map", VOTE_MAP, Vote_Map },
    {}
};

static void Cmd_Vote_f( edict_t *ent ) {
    char buffer[MAX_QPATH];
    const vote_proposal_t *v;
    int mask = g_vote_mask->value;
    int limit = g_vote_limit->value;
    int treshold = g_vote_treshold->value;
    int argc = gi.argc();
    int acc, rej;
    char *s;

    if( !mask ) {
        gi.cprintf( ent, PRINT_HIGH, "Voting is disabled.\n" );
        return;
    }

    if( argc < 2 ) {
        if( !level.vote.proposal ) {
            gi.cprintf( ent, PRINT_HIGH, "No vote in progress.\n" );
            return;
        }
        G_BuildProposal( buffer );
        G_CalcVote( &acc, &rej );
        gi.cprintf( ent, PRINT_HIGH,
            "Proposal: %s\n"
            "Accepted: %d%%\n"
            "Rejected: %d%%\n"
            "Treshold: %d%%\n",
            ( level.vote.framenum - level.framenum ) / HZ,
            buffer, acc, rej, treshold );
        return;
    }

    s = gi.argv( 1 );

//
// generic commands
//
    if( !strcmp( s, "help" ) ) {
        return;
    }
    if( !strcmp( s, "yes" ) || !strcmp( s, "y" ) ) {
        Cmd_CastVote_f( ent, qtrue );
        return;
    }
    if( !strcmp( s, "no" ) || !strcmp( s, "n" ) ) {
        Cmd_CastVote_f( ent, qfalse );
        return;
    }

//
// proposals
//
    if( level.vote.proposal ) {
        gi.cprintf( ent, PRINT_HIGH, "Vote is already in progress.\n" );
        return;
    }

    if( level.framenum - level.vote.framenum < 5*HZ ) {
        gi.cprintf( ent, PRINT_HIGH, "You may not initiate votes too soon.\n" );
        return;
    }

    if( limit > 0 && ent->client->level.vote.count >= limit ) {
        gi.cprintf( ent, PRINT_HIGH, "You may not initiate too many votes.\n" );
        return;
    }

    for( v = vote_proposals; v->bit; v++ ) {
        if( !strcmp( s, v->name ) ) {
            break;
        }
    }
    if( !v->bit ) {
        gi.cprintf( ent, PRINT_HIGH, "Unknown proposal. Try 'vote help'.\n" );
        return;
    }

    if( !( mask & v->bit ) ) {
        gi.cprintf( ent, PRINT_HIGH, "Voting on '%s' is disabled.\n", v->name );
        return;
    }

    if( argc < 3 ) {
        gi.cprintf( ent, PRINT_HIGH, "Insufficient arguments. Try 'vote help'.\n" );
        return;
    }

    if( !v->func( ent ) ) {
        return;
    }

    level.vote.proposal = v->bit;
    level.vote.framenum = level.framenum + g_vote_time->value * HZ;
    level.vote.index++;

    G_BuildProposal( buffer );

    gi.bprintf( PRINT_CHAT, "%s has initiated a vote!\n",
        ent->client->pers.netname );
    gi.bprintf( PRINT_HIGH, "Proposal: %s\n", buffer );
    ent->client->level.vote.index = level.vote.index;
    ent->client->level.vote.accepted = qtrue;
    ent->client->level.vote.count++;

    G_CheckVote();
}


/*
=================
ClientCommand
=================
*/
void ClientCommand (edict_t *ent)
{
	char	*cmd;

	if (!ent->client)
		return;		// not fully in game yet

    if( ent->client->pers.connected <= CONN_CONNECTED ) {
        return;
    }

	cmd = gi.argv(0);

	if (Q_stricmp (cmd, "say") == 0)
	{
		Cmd_Say_f (ent, qfalse, qfalse);
		return;
	}
	if (Q_stricmp (cmd, "say_team") == 0)
	{
		Cmd_Say_f (ent, qtrue, qfalse);
		return;
	}
	if (Q_stricmp (cmd, "score") == 0)
	{
		Cmd_Score_f (ent);
		return;
	}
	if (Q_stricmp (cmd, "help") == 0)
	{
		Cmd_Help_f (ent);
		return;
	}

	if (level.intermission_framenum)
		return;

	if (Q_stricmp (cmd, "use") == 0)
		Cmd_Use_f (ent);
	else if (Q_stricmp (cmd, "drop") == 0)
		Cmd_Drop_f (ent);
	else if (Q_stricmp (cmd, "give") == 0)
		Cmd_Give_f (ent);
	else if (Q_stricmp (cmd, "god") == 0)
		Cmd_God_f (ent);
	else if (Q_stricmp (cmd, "notarget") == 0)
		Cmd_Notarget_f (ent);
	else if (Q_stricmp (cmd, "noclip") == 0)
		Cmd_Noclip_f (ent);
	else if (Q_stricmp (cmd, "inven") == 0)
		Cmd_Inven_f (ent);
	else if (Q_stricmp (cmd, "invnext") == 0)
		SelectNextItem (ent, -1);
	else if (Q_stricmp (cmd, "invprev") == 0)
		SelectPrevItem (ent, -1);
	else if (Q_stricmp (cmd, "invnextw") == 0)
		SelectNextItem (ent, IT_WEAPON);
	else if (Q_stricmp (cmd, "invprevw") == 0)
		SelectPrevItem (ent, IT_WEAPON);
	else if (Q_stricmp (cmd, "invnextp") == 0)
		SelectNextItem (ent, IT_POWERUP);
	else if (Q_stricmp (cmd, "invprevp") == 0)
		SelectPrevItem (ent, IT_POWERUP);
	else if (Q_stricmp (cmd, "invuse") == 0)
		Cmd_InvUse_f (ent);
	else if (Q_stricmp (cmd, "invdrop") == 0)
		Cmd_InvDrop_f (ent);
	else if (Q_stricmp (cmd, "weapprev") == 0)
		Cmd_WeapPrev_f (ent);
	else if (Q_stricmp (cmd, "weapnext") == 0)
		Cmd_WeapNext_f (ent);
	else if (Q_stricmp (cmd, "weaplast") == 0)
		Cmd_WeapLast_f (ent);
	else if (Q_stricmp (cmd, "kill") == 0)
		Cmd_Kill_f (ent);
	else if (Q_stricmp (cmd, "putaway") == 0)
		Cmd_PutAway_f (ent);
	else if (Q_stricmp (cmd, "wave") == 0)
		Cmd_Wave_f (ent);
	else if (Q_stricmp(cmd, "observe") == 0)
		Cmd_Observe_f(ent);
	else if (Q_stricmp(cmd, "stats") == 0)
		Cmd_Stats_f(ent);
	else if (Q_stricmp(cmd, "vote") == 0)
		Cmd_Vote_f(ent);
	else if (Q_stricmp(cmd, "yes") == 0)
		Cmd_CastVote_f(ent, qtrue);
	else if (Q_stricmp(cmd, "no") == 0)
		Cmd_CastVote_f(ent, qfalse);
	else	// anything that doesn't match a command will be a chat
		Cmd_Say_f (ent, qfalse, qtrue);
}
