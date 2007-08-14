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
	gclient_t *cl;

	if (gi.argc () < 2 && !arg0)
		return;

	if (!((int)(dmflags->value) & (DF_MODELTEAMS | DF_SKINTEAMS)))
		team = qfalse;

	if (team)
		Com_sprintf (text, sizeof(text), "(%s): ", ent->client->pers.netname);
	else
		Com_sprintf (text, sizeof(text), "%s: ", ent->client->pers.netname);

	if (arg0) {
		Q_strcat (text, sizeof(text), gi.argv(0));
		Q_strcat (text, sizeof(text), " ");
    }
	Q_strcat (text, sizeof(text), gi.args());

	if (flood_msgs->value) {
		cl = ent->client;

        if (level.time < cl->flood_locktill) {
			gi.cprintf(ent, PRINT_HIGH, "You can't talk for %d more seconds\n",
				(int)(cl->flood_locktill - level.time));
            return;
        }
        i = cl->flood_whenhead - flood_msgs->value + 1;
        if (i < 0)
            i = (sizeof(cl->flood_when)/sizeof(cl->flood_when[0])) + i;
		if (cl->flood_when[i] && 
			level.time - cl->flood_when[i] < flood_persecond->value) {
			cl->flood_locktill = level.time + flood_waitdelay->value;
			gi.cprintf(ent, PRINT_CHAT, "Flood protection:  You can't talk for %d seconds.\n",
				(int)flood_waitdelay->value);
            return;
        }
		cl->flood_whenhead = (cl->flood_whenhead + 1) %
			(sizeof(cl->flood_when)/sizeof(cl->flood_when[0]));
		cl->flood_when[cl->flood_whenhead] = level.time;
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

void spectator_respawn (edict_t *ent);

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

static const char *const weapnames[WEAP_TOTAL] = {
    "None",
    "Blaster",
    "Shotgun",
    "S.Shotgun",
    "Machinegun",
    "Chaingun",
    "Grenades",
    "G.Launcher",
    "R.Launcher",
    "H.Blaster",
    "Railgun",
    "BFG10K"
};

void Cmd_Stats_f( edict_t *ent ) {
    int i;
    weapstat_t *s;
    char acc[16];
    char hits[16];
    char frgs[16];
    char dths[16];

    for( i = 1; i < WEAP_TOTAL; i++ ) {
        s = &ent->client->resp.stats[i];
        if( s->atts || s->deaths ) {
            break;
        }
    }
    if( i == WEAP_TOTAL ) {
        gi.cprintf( ent, PRINT_HIGH, "No weapon statistics available.\n" );
        return;
    }

    gi.cprintf( ent, PRINT_HIGH,
        "/------------+------+-----------+------+------\\\n"
        "| Weapon     | Acc%% | Hits/Atts | Frgs | Dths |\n"
        "+------------+------+-----------+------+------+\n" );

    for( i = 1; i < WEAP_TOTAL; i++ ) {
        s = &ent->client->resp.stats[i];
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
            "| %-10s | %s | %s | %s | %s |\n",
            weapnames[i], acc, hits, frgs, dths );
    }

    gi.cprintf( ent, PRINT_HIGH,
        "\\------------+------+-----------+------+------/\n"
        "Total damage given: %d\n"
        "Total damage recvd: %d\n",
        ent->client->resp.damage_given,
        ent->client->resp.damage_recvd );
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

	if (level.intermissiontime)
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
	else	// anything that doesn't match a command will be a chat
		Cmd_Say_f (ent, qfalse, qtrue);
}
