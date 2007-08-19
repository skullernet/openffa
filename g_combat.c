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
// g_combat.c

#include "g_local.h"

/*
============
CanDamage

Returns qtrue if the inflictor can directly damage the target.  Used for
explosions and melee attacks.
============
*/
qboolean CanDamage (edict_t *targ, edict_t *inflictor)
{
	vec3_t	dest;
	trace_t	trace;
    vec_t *bounds[2];
    int i;

// bmodels need special checking because their origin is 0,0,0
	if (targ->movetype == MOVETYPE_PUSH)
	{
		VectorAdd (targ->absmin, targ->absmax, dest);
		VectorScale (dest, 0.5, dest);
		gi_trace (&trace, inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
		if (trace.fraction == 1.0)
			return qtrue;
		if (trace.ent == targ)
			return qtrue;
		return qfalse;
	}
	
	gi_trace( &trace, inflictor->s.origin, vec3_origin, vec3_origin, targ->s.origin, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return qtrue;
    
    bounds[0] = targ->absmin;
    bounds[1] = targ->absmax;
    for( i = 0; i < 8; i++ ) {
		dest[0] = bounds[(i>>0)&1][0];
		dest[1] = bounds[(i>>1)&1][1];
		dest[2] = bounds[(i>>2)&1][2];

        gi_trace( &trace, inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
        if (trace.fraction == 1.0)
            return qtrue;
    }

	return qfalse;
}


/*
============
Killed
============
*/
void Killed (edict_t *targ, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (targ->health < -999)
		targ->health = -999;

	targ->enemy = attacker;
	targ->die (targ, inflictor, attacker, damage, point);
}


/*
================
SpawnDamage
================
*/
void SpawnDamage (int type, vec3_t origin, vec3_t normal, int damage)
{
	if (damage > 255)
		damage = 255;
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (type);
//	gi.WriteByte (damage);
	gi.WritePosition (origin);
	gi.WriteDir (normal);
	gi.multicast (origin, MULTICAST_PVS);
}


/*
============
T_Damage

targ		entity that is being damaged
inflictor	entity that is causing the damage
attacker	entity that caused the inflictor to damage targ
	example: targ=monster, inflictor=rocket, attacker=player

dir			direction of the attack
point		point at which the damage is being inflicted
normal		normal vector from that point
damage		amount of damage being inflicted
knockback	force to be applied against targ as a result of the damage

dflags		these flags are used to control how T_Damage works
	DAMAGE_RADIUS			damage was indirect (from a nearby explosion)
	DAMAGE_NO_ARMOR			armor does not protect from this damage
	DAMAGE_ENERGY			damage is from an energy based weapon
	DAMAGE_NO_KNOCKBACK		do not affect velocity, just view angles
	DAMAGE_BULLET			damage is from a bullet (used for ricochets)
	DAMAGE_NO_PROTECTION	kills godmode, armor, everything
============
*/
static int CheckPowerArmor (edict_t *ent, vec3_t point, vec3_t normal, int damage, int dflags)
{
	gclient_t	*client;
	int			save;
	int			power_armor_type;
	int			damagePerCell;
	int			pa_te_type;
	int			power;
	int			power_used;

	if (!damage)
		return 0;

	client = ent->client;
	if (!client) {
        return 0;
    }

	if (dflags & DAMAGE_NO_ARMOR)
		return 0;

    power_armor_type = PowerArmorType (ent);
    if (power_armor_type != POWER_ARMOR_NONE)
    {
        power = client->inventory[ITEM_CELLS];
    }

	if (power_armor_type == POWER_ARMOR_NONE)
		return 0;
	if (!power)
		return 0;

	if (power_armor_type == POWER_ARMOR_SCREEN)
	{
		vec3_t		vec;
		float		dot;
		vec3_t		forward;

		// only works if damage point is in front
		AngleVectors (ent->s.angles, forward, NULL, NULL);
		VectorSubtract (point, ent->s.origin, vec);
		VectorNormalize (vec);
		dot = DotProduct (vec, forward);
		if (dot <= 0.3)
			return 0;

		damagePerCell = 1;
		pa_te_type = TE_SCREEN_SPARKS;
		damage = damage / 3;
	}
	else
	{
		damagePerCell = 2;
		pa_te_type = TE_SHIELD_SPARKS;
		damage = (2 * damage) / 3;
	}

	save = power * damagePerCell;
	if (!save)
		return 0;
	if (save > damage)
		save = damage;

	SpawnDamage (pa_te_type, point, normal, save);
	client->powerarmor_framenum = level.framenum + 2;

	power_used = save / damagePerCell;

	client->inventory[ITEM_CELLS] -= power_used;
	return save;
}

static int CheckArmor (edict_t *ent, vec3_t point, vec3_t normal, int damage, int te_sparks, int dflags)
{
	gclient_t	*client;
	int			save;
	int			index;
	gitem_t		*armor;

	if (!damage)
		return 0;

	client = ent->client;

	if (!client)
		return 0;

	if (dflags & DAMAGE_NO_ARMOR)
		return 0;

	index = ArmorIndex (ent);
	if (!index)
		return 0;

	armor = INDEX_ITEM(index);

	if (dflags & DAMAGE_ENERGY)
		save = ceil(((gitem_armor_t *)armor->info)->energy_protection*damage);
	else
		save = ceil(((gitem_armor_t *)armor->info)->normal_protection*damage);
	if (save >= client->inventory[index])
		save = client->inventory[index];

	if (!save)
		return 0;

	client->inventory[index] -= save;
	SpawnDamage (te_sparks, point, normal, save);

	return save;
}

qboolean CheckTeamDamage (edict_t *targ, edict_t *attacker)
{
		//FIXME make the next line real and uncomment this block
		// if ((ability to damage a teammate == OFF) && (targ's team == attacker's team))
	return qfalse;
}

void T_Damage (edict_t *targ, edict_t *inflictor, edict_t *attacker, vec3_t dir, vec3_t point, vec3_t normal, int damage, int knockback, int dflags, int mod)
{
	gclient_t	*client;
	int			take;
	int			save;
	int			asave;
	int			psave;
	int			te_sparks;



	if (!targ->takedamage)
		return;

//	if (targ == attacker)    //atu Думает
//		damage *= 0.5;

	// friendly fire avoidance
	// if enabled you can't hurt teammates (but you can hurt yourself)
	// knockback still occurs
	if( (targ != attacker) && ((int)(dmflags->value) & (DF_MODELTEAMS|DF_SKINTEAMS)))
	{
		if (OnSameTeam (targ, attacker))
		{
			if (DF(NO_FRIENDLY_FIRE))
				damage = 0;
			else
				mod |= MOD_FRIENDLY_FIRE;
		}
	}
	meansOfDeath = mod;

	client = targ->client;

	if (dflags & DAMAGE_BULLET)
		te_sparks = TE_BULLET_SPARKS;
	else
		te_sparks = TE_SPARKS;

	VectorNormalize(dir);

	if (targ->flags & FL_NO_KNOCKBACK)
		knockback = 0;

	//--------------------------------------------------------midair-------------------------------------------
	if (midair->value)
	{	
		float		playerheight, minheight = 45, midheight = 0;
		qboolean	lowheight = qfalse, midairshot = qtrue;

		playerheight = MidAir_Height(targ);
		midheight = targ->s.origin[2] - inflictor->s.old_origin[2];

        if (!knockback&&(mod==MOD_ROCKET))
			knockback=damage; //ну если демедж от ракеты то всётаки мы будем отбрасывать

		if ((knockback) && (targ->movetype != MOVETYPE_NONE) && (targ->movetype != MOVETYPE_BOUNCE) && (targ->movetype != MOVETYPE_PUSH) && (targ->movetype != MOVETYPE_STOP))
		{
			float	mass = 50.0;
			float   push;

			if ( playerheight < minheight )
				lowheight = qtrue;
			else
			{
				damage *= ( 1 + ( playerheight - minheight ) / 64 );
				lowheight = qfalse;
			}

			if (targ->mass > 50)
				mass = targ->mass;

			knockback = damage;

			push = 1600.0f * ((float)knockback / mass); //типа как в QW
				
	        VectorMA( targ->velocity, push, dir, targ->velocity );
			//gi.bprintf (PRINT_MEDIUM,"targ mass: %i .\n", (int)mass) ;
			//gi.bprintf (PRINT_MEDIUM,"Vector Length: %i .   Vertical Scalar : %i .\n", (int)VectorLength (targ->velocity), (int)targ->velocity[2]) ;

			if (lowheight)
			{
				targ->velocity[2] *= 1.5;
			}
			if(inflictor != world)
			{  
				if ((playerheight>minheight) && (attacker!=targ) && ((mod==MOD_ROCKET) || (mod==MOD_GRENADE)))
				{
					char		*message;
					//gi.bprintf (PRINT_HIGH, "%1.f (midheight)\n", midheight) ;
					if (midheight > 190)
					{
						if ( midheight > 900 )
						{
						//if (level.status ==  MATCH_STATE_PLAYTIME)
							attacker->client->resp.score += 8;
						message = "d1am0nd midair";
						}
						else if ( midheight > 500 )
						{
							//if (level.status ==  MATCH_STATE_PLAYTIME)
								attacker->client->resp.score += 4;
							message = "g0ld midair";
						}
						else if ( midheight > 380 )
						{
							//if (level.status ==  MATCH_STATE_PLAYTIME)
								attacker->client->resp.score += 2;
							message = "s1lver midair";
						}
						else
						{
							//if (level.status ==  MATCH_STATE_PLAYTIME)
								attacker->client->resp.score++;
							message = "midair";
						}
						gi.bprintf (PRINT_CHAT, "%s got %s. %1.f (midheight)\n", attacker->client->pers.netname, message, midheight) ;
					}
				}
			}
			else return;
		}
	}
	else //-------------------------------------------------End of Midair------------------------------------------
	{
	// figure momentum add
		if (!(dflags & DAMAGE_NO_KNOCKBACK))
		{
			if ((knockback) && (targ->movetype != MOVETYPE_NONE) && (targ->movetype != MOVETYPE_BOUNCE) && (targ->movetype != MOVETYPE_PUSH) && (targ->movetype != MOVETYPE_STOP))
			{
				float	mass;
				float push;

				if (targ->mass < 50)
					mass = 50;
				else
					mass = targ->mass;
	
				if (targ->client  && attacker == targ)
					push = 1600.0f * ((float)knockback / mass);
				else
					push = 500.0f * ((float)knockback / mass);

				VectorMA( targ->velocity, push, dir, targ->velocity );
			}
		}
	}

//	if (midair->value && level.status != MATCH_STATE_PLAYTIME) //во время вармапа в мидейре нельзя нанести дамадж
//	{
//		take = 0;
//		save = damage;
//	}
//	else
//	{
		take = damage;
		save = 0;
//	}

	// check for godmode
	if ( (targ->flags & FL_GODMODE) && !(dflags & DAMAGE_NO_PROTECTION) )
	{
		take = 0;
		save = damage;
		SpawnDamage (te_sparks, point, normal, save);
	}

	// check for invincibility
	if ((client && client->invincible_framenum > level.framenum ) && !(dflags & DAMAGE_NO_PROTECTION))
	{
		if (targ->pain_debounce_framenum < level.framenum)
		{
			gi.sound(targ, CHAN_ITEM, gi.soundindex("items/protect4.wav"), 1, ATTN_NORM, 0);
			targ->pain_debounce_framenum = level.framenum + 2*HZ;
		}
		take = 0;
		save = damage;
	}

	psave = CheckPowerArmor (targ, point, normal, take, dflags);
	take -= psave;

	asave = CheckArmor (targ, point, normal, take, te_sparks, dflags);
	take -= asave;

	//treat cheat/powerup savings the same as armor
	asave += save;

	// team damage avoidance
	if (!(dflags & DAMAGE_NO_PROTECTION) && CheckTeamDamage (targ, attacker))
		return;

    // add to client weapon statistics
    if( attacker->client && targ->client && targ != attacker ) {
        G_AccountDamage( targ, inflictor, attacker, take );
    }

// do the damage
	if (take)
	{
		if (client)
			if(targ == attacker)
				SpawnDamage (TE_BLOOD, targ->s.origin, normal, take);
			else
				SpawnDamage (TE_BLOOD, point, normal, take);
		else
			if(targ == attacker)
				SpawnDamage (TE_SPARKS, targ->s.origin, normal, take);
			else
				SpawnDamage (TE_SPARKS, point, normal, take);

		targ->health -= take;
			
		if (targ->health <= 0)
		{
			if (client)
				targ->flags |= FL_NO_KNOCKBACK;
			Killed (targ, inflictor, attacker, take, point);
			return;
		}
	}

	if (client)
	{
		if (!(targ->flags & FL_GODMODE) && (take))
			targ->pain (targ, attacker, knockback, take);
	}
	else if (take)
	{
		if (targ->pain)
			targ->pain (targ, attacker, knockback, take);
	}

	// add to the damage inflicted on a player this frame
	// the total will be turned into screen blends and view angle kicks
	// at the end of the frame
	if (client)
	{
		client->damage_parmor += psave;
		client->damage_armor += asave;
		client->damage_blood += take;
		client->damage_knockback += knockback;
		VectorCopy (point, client->damage_from);
	}
}


/*
============
T_RadiusDamage
============
*/
void T_RadiusDamage (edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod)
{
	float	points;
	edict_t	*ent = NULL;
	vec3_t	v;
	vec3_t	dir;
	if (midair->value)
		radius = damage + 40;

	while ((ent = findradius(ent, inflictor->s.origin, radius)) != NULL)
	{
		if (ent == ignore)
			continue;
		if (!ent->takedamage)
			continue;

		VectorAdd (ent->mins, ent->maxs, v);
		VectorMA (ent->s.origin, 0.5, v, v);
		VectorSubtract (inflictor->s.origin, v, v);
		points = damage - 0.5 * VectorLength (v);
		if (ent == attacker)
			points = points * 0.5;
		if (points > 0)
		{
			if (CanDamage (ent, inflictor))
			{
				VectorSubtract (ent->s.origin, inflictor->s.origin, dir);
				T_Damage (ent, inflictor, attacker, dir, inflictor->s.origin, vec3_origin, (int)points, (int)points, DAMAGE_RADIUS, mod);
			}
		}
	}
}

//-------------------------------------------------------------------------------
float MidAir_Height (edict_t *targ)
{
	trace_t trace;
	
	trace = gi.trace(targ->s.origin, targ->mins, targ->maxs, tv(targ->s.origin[0], targ->s.origin[1], targ->s.origin[2] - 16000), targ, MASK_SOLID);

	if( trace.fraction == 1 || trace.allsolid )
	{
		return 0.0f;
	} else
		return targ->s.origin[2] - trace.endpos[2];
}