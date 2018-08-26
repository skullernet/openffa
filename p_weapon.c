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
// g_weapon.c

#include "g_local.h"
#include "m_player.h"


static bool     is_quad;
static byte     is_silenced;


static void weapon_grenade_fire(edict_t *ent, bool held);


static void P_ProjectSource(gclient_t *client, vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result)
{
    vec3_t  _distance;

    VectorCopy(distance, _distance);
    if (client->pers.hand == LEFT_HANDED)
        _distance[1] *= -1;
    else if (client->pers.hand == CENTER_HANDED)
        _distance[1] = 0;
    G_ProjectSource(point, _distance, forward, right, result);
}

bool Pickup_Weapon(edict_t *ent, edict_t *other)
{
    int         index;
    gitem_t     *ammo;

    index = ITEM_INDEX(ent->item);

    if (DF(WEAPONS_STAY) && other->client->inventory[index]) {
        if (!(ent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))
            return false;   // leave the weapon for others to pickup
    }

    other->client->inventory[index]++;

    if (!(ent->spawnflags & DROPPED_ITEM)) {
        // give them some ammo with it
        ammo = FindItem(ent->item->ammo);
        if (DF(INFINITE_AMMO))
            Add_Ammo(other, ammo, 1000);
        else
            Add_Ammo(other, ammo, ammo->quantity);

        if (!(ent->spawnflags & DROPPED_PLAYER_ITEM)) {
            if (DF(WEAPONS_STAY))
                ent->flags |= FL_RESPAWN;
            else
                SetRespawn(ent, 30);
        }
    }

    if (other->client->weapon != ent->item &&
        other->client->inventory[index] == 1 &&
        other->client->weapon == INDEX_ITEM(ITEM_BLASTER)) {
        other->client->newweapon = ent->item;
    }

    return true;
}


/*
===============
ChangeWeapon

The old weapon has been dropped all the way, so make the new one
current
===============
*/
void ChangeWeapon(edict_t *ent)
{
    int i;

    //a grenade action is happening
    if (ent->client->grenade_framenum) {
        int bugs = (int)g_bugs->value;

        //but it blew up in their hand or they threw it, allow bug to double explode
        if ((ent->client->grenade_state == GRENADE_BLEW_UP && bugs >= 2) ||
            (ent->client->grenade_state == GRENADE_THROWN && bugs >= 1) ||
            ent->client->grenade_state == GRENADE_NONE) {
            //r1: prevent quad on someone making grenades into quad grenades on death explode
            if (bugs < 1)
                is_quad = (ent->client->quad_framenum > level.framenum);

            ent->client->grenade_framenum = level.framenum;
            weapon_grenade_fire(ent, false);
            ent->client->grenade_framenum = 0;
            ent->client->grenade_state = GRENADE_NONE;
        }
    }

    ent->client->lastweapon = ent->client->weapon;
    ent->client->weapon = ent->client->newweapon;
    ent->client->newweapon = NULL;
    ent->client->machinegun_shots = 0;

    // set visible model
    if (ent->s.modelindex == 255) {
        if (ent->client->weapon)
            i = ((ent->client->weapon->weapmodel & 0xff) << 8);
        else
            i = 0;
        ent->s.skinnum = (ent - g_edicts - 1) | i;
    }

    if (ent->client->weapon && ent->client->weapon->ammo)
        ent->client->ammo_index = ITEM_INDEX(FindItem(ent->client->weapon->ammo));
    else
        ent->client->ammo_index = 0;

    if (!ent->client->weapon) {
        // dead
        ent->client->ps.gunindex = 0;
        return;
    }

    ent->client->weaponstate = WEAPON_ACTIVATING;
    ent->client->weaponframe = 0;
    ent->client->ps.gunframe = 0;
    ent->client->ps.gunindex = gi.modelindex(ent->client->weapon->view_model);

    ent->client->anim_priority = ANIM_PAIN;
    if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
        ent->client->anim_start = FRAME_crpain1 + 1;
        ent->client->anim_end = FRAME_crpain4;
    } else {
        ent->client->anim_start = FRAME_pain301 + 1;
        ent->client->anim_end = FRAME_pain304;
    }
}

/*
=================
NoAmmoWeaponChange
=================
*/
static void NoAmmoWeaponChange(edict_t *ent)
{
    if (level.framenum >= ent->pain_debounce_framenum) {
        gi.sound(ent, CHAN_VOICE, level.sounds.noammo, 1, ATTN_NORM, 0);
        ent->pain_debounce_framenum = level.framenum + 1 * HZ;
    }

    if (ent->client->inventory[ITEM_SLUGS] &&
        ent->client->inventory[ITEM_RAILGUN]) {
        ent->client->newweapon = INDEX_ITEM(ITEM_RAILGUN);
        return;
    }
    if (ent->client->inventory[ITEM_CELLS]
        &&  ent->client->inventory[ITEM_HYPERBLASTER]) {
        ent->client->newweapon = INDEX_ITEM(ITEM_HYPERBLASTER);
        return;
    }
    if (ent->client->inventory[ITEM_BULLETS]
        && ent->client->inventory[ITEM_CHAINGUN]) {
        ent->client->newweapon = INDEX_ITEM(ITEM_CHAINGUN);
        return;
    }
    if (ent->client->inventory[ITEM_BULLETS]
        &&  ent->client->inventory[ITEM_MACHINEGUN]) {
        ent->client->newweapon = INDEX_ITEM(ITEM_MACHINEGUN);
        return;
    }
    if (ent->client->inventory[ITEM_SHELLS] > 1
        &&  ent->client->inventory[ITEM_SUPERSHOTGUN]) {
        ent->client->newweapon = INDEX_ITEM(ITEM_SUPERSHOTGUN);
        return;
    }
    if (ent->client->inventory[ITEM_SHELLS]
        &&  ent->client->inventory[ITEM_SHOTGUN]) {
        ent->client->newweapon = INDEX_ITEM(ITEM_SHOTGUN);
        return;
    }
    ent->client->newweapon = INDEX_ITEM(ITEM_BLASTER);
}

/*
=================
Think_Weapon

Called by ClientBeginServerFrame and ClientThink
=================
*/
void Think_Weapon(edict_t *ent)
{
    // if just died, put the weapon away
    if (ent->health < 1) {
        ent->client->newweapon = NULL;
        ChangeWeapon(ent);
    }

    // call active weapon think routine
    if (ent->client->weapon && ent->client->weapon->weaponthink) {
        is_quad = (ent->client->quad_framenum > level.framenum);
        if (ent->client->silencer_shots)
            is_silenced = MZ_SILENCED;
        else
            is_silenced = 0;
        ent->client->weapon->weaponthink(ent);
    }
}


/*
================
Use_Weapon

Make the weapon ready if there is ammo
================
*/
void Use_Weapon(edict_t *ent, gitem_t *item)
{
    int         ammo_index;
    gitem_t     *ammo_item;

    // see if we're already using it
    if (item == ent->client->weapon)
        return;

    if (item->ammo && !g_select_empty->value && !(item->flags & IT_AMMO)) {
        ammo_item = FindItem(item->ammo);
        ammo_index = ITEM_INDEX(ammo_item);

        if (!ent->client->inventory[ammo_index]) {
            gi.cprintf(ent, PRINT_HIGH, "No %s for %s.\n", ammo_item->pickup_name, item->pickup_name);
            return;
        }

        if (ent->client->inventory[ammo_index] < item->quantity) {
            gi.cprintf(ent, PRINT_HIGH, "Not enough %s for %s.\n", ammo_item->pickup_name, item->pickup_name);
            return;
        }
    }

    // change to this weapon when down
    ent->client->newweapon = item;
}



/*
================
Drop_Weapon
================
*/
void Drop_Weapon(edict_t *ent, gitem_t *item)
{
    int     index;

    if (DF(WEAPONS_STAY))
        return;

    index = ITEM_INDEX(item);
    // see if we're already using it
    if (((item == ent->client->weapon) || (item == ent->client->newweapon)) && (ent->client->inventory[index] == 1)) {
        gi.cprintf(ent, PRINT_HIGH, "Can't drop current weapon\n");
        return;
    }

    Drop_Item(ent, item);
    ent->client->inventory[index]--;
}


/*
================
Weapon_Generic

A generic function to handle the basics of weapon thinking
================
*/
#define FRAME_FIRE_FIRST        (FRAME_ACTIVATE_LAST + 1)
#define FRAME_IDLE_FIRST        (FRAME_FIRE_LAST + 1)
#define FRAME_DEACTIVATE_FIRST  (FRAME_IDLE_LAST + 1)

static void Weapon_Generic(edict_t *ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, const int *pause_frames, const int *fire_frames, void (*fire)(edict_t *ent))
{
    int     n;

    if (ent->deadflag || ent->s.modelindex != 255) { // VWep animations screw up corpses
        return;
    }

    if (ent->client->weaponstate == WEAPON_DROPPING) {
        if (ent->client->weaponframe == FRAME_DEACTIVATE_LAST) {
            ChangeWeapon(ent);
            return;
        } else if ((FRAME_DEACTIVATE_LAST - ent->client->weaponframe) == 4) {
            ent->client->anim_priority = ANIM_REVERSE;
            if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
                ent->client->anim_start = FRAME_crpain4;
                ent->client->anim_end = FRAME_crpain1;
            } else {
                ent->client->anim_start = FRAME_pain304;
                ent->client->anim_end = FRAME_pain301;

            }
        }

        ent->client->weaponframe++;
        return;
    }

    if (ent->client->weaponstate == WEAPON_ACTIVATING) {
        if (ent->client->weaponframe == FRAME_ACTIVATE_LAST) {
            ent->client->weaponstate = WEAPON_READY;
            ent->client->weaponframe = FRAME_IDLE_FIRST;
            return;
        }

        ent->client->weaponframe++;
        return;
    }

    if ((ent->client->newweapon) && (ent->client->weaponstate != WEAPON_FIRING)) {
        ent->client->weaponstate = WEAPON_DROPPING;
        ent->client->weaponframe = FRAME_DEACTIVATE_FIRST;

        if ((FRAME_DEACTIVATE_LAST - FRAME_DEACTIVATE_FIRST) < 4) {
            ent->client->anim_priority = ANIM_REVERSE;
            if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
                ent->client->anim_start = FRAME_crpain4;
                ent->client->anim_end = FRAME_crpain1;
            } else {
                ent->client->anim_start = FRAME_pain304;
                ent->client->anim_end = FRAME_pain301;

            }
        }
        return;
    }

    if (ent->client->weaponstate == WEAPON_READY) {
        if (((ent->client->latched_buttons | ent->client->buttons) & BUTTON_ATTACK)) {
            ent->client->latched_buttons &= ~BUTTON_ATTACK;
            if ((!ent->client->ammo_index) ||
                (ent->client->inventory[ent->client->ammo_index] >= ent->client->weapon->quantity)) {
                ent->client->weaponframe = FRAME_FIRE_FIRST;
                ent->client->weaponstate = WEAPON_FIRING;

                // start the animation
                ent->client->anim_priority = ANIM_ATTACK;
                if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
                    ent->client->anim_start = FRAME_crattak1;
                    ent->client->anim_end = FRAME_crattak9;
                } else {
                    ent->client->anim_start = FRAME_attack1;
                    ent->client->anim_end = FRAME_attack8;
                }
            } else {
                NoAmmoWeaponChange(ent);
            }
        } else {
            if (ent->client->weaponframe == FRAME_IDLE_LAST) {
                ent->client->weaponframe = FRAME_IDLE_FIRST;
                return;
            }

            if (pause_frames) {
                for (n = 0; pause_frames[n]; n++) {
                    if (ent->client->weaponframe == pause_frames[n]) {
                        if (rand_byte() & 15)
                            return;
                    }
                }
            }

            ent->client->weaponframe++;
            return;
        }
    }

    if (ent->client->weaponstate == WEAPON_FIRING) {
        for (n = 0; fire_frames[n]; n++) {
            if (ent->client->weaponframe == fire_frames[n]) {
                if (ent->client->quad_framenum > level.framenum)
                    gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);

                fire(ent);
                break;
            }
        }

        if (!fire_frames[n])
            ent->client->weaponframe++;

        if (ent->client->weaponframe == FRAME_IDLE_FIRST + 1)
            ent->client->weaponstate = WEAPON_READY;
    }
}


/*
======================================================================

GRENADE

======================================================================
*/

#define GRENADE_TIMER       (3 * HZ)
#define GRENADE_MINSPEED    400
#define GRENADE_MAXSPEED    800

static void weapon_grenade_fire(edict_t *ent, bool held)
{
    vec3_t  offset;
    vec3_t  forward, right;
    vec3_t  start;
    int     damage = 125;
    int     timer;
    int     speed;
    float   radius;

    radius = damage + 40;
    if (is_quad)
        damage *= 4;

    VectorSet(offset, 8, 8, ent->viewheight - 8);
    AngleVectors(ent->client->v_angle, forward, right, NULL);
    P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

    timer = ent->client->grenade_framenum - level.framenum;
    speed = GRENADE_MINSPEED + (GRENADE_TIMER - timer) * ((GRENADE_MAXSPEED - GRENADE_MINSPEED) / GRENADE_TIMER);
    fire_grenade2(ent, start, forward, damage, speed, timer, radius, held);

    if (!DF(INFINITE_AMMO))
        ent->client->inventory[ent->client->ammo_index]--;

    ent->client->resp.frags[FRAG_GRENADES].atts++;

    ent->client->grenade_framenum = level.framenum + 1 * HZ;

    if (ent->deadflag || ent->s.modelindex != 255) { // VWep animations screw up corpses
        return;
    }

    if (ent->health <= 0)
        return;

    if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
        ent->client->anim_priority = ANIM_ATTACK;
        ent->client->anim_start = FRAME_crattak1;
        ent->client->anim_end = FRAME_crattak3;
    } else {
        ent->client->anim_priority = ANIM_REVERSE;
        ent->client->anim_start = FRAME_wave08 - 1;
        ent->client->anim_end = FRAME_wave01;
    }
}

void Weapon_Grenade(edict_t *ent)
{
    if ((ent->client->newweapon) && (ent->client->weaponstate == WEAPON_READY)) {
        ChangeWeapon(ent);
        return;
    }

    if (ent->client->weaponstate == WEAPON_ACTIVATING) {
        ent->client->weaponstate = WEAPON_READY;
        ent->client->weaponframe = 16;
        return;
    }

    if (ent->client->weaponstate == WEAPON_READY) {
        if (((ent->client->latched_buttons | ent->client->buttons) & BUTTON_ATTACK)) {
            ent->client->latched_buttons &= ~BUTTON_ATTACK;
            if (ent->client->inventory[ent->client->ammo_index]) {
                ent->client->weaponframe = 1;
                ent->client->weaponstate = WEAPON_FIRING;
                ent->client->grenade_framenum = 0;
                ent->client->grenade_state = GRENADE_NONE;
            } else {
                NoAmmoWeaponChange(ent);
            }
            return;
        }

        if ((ent->client->weaponframe == 29) || (ent->client->weaponframe == 34) || (ent->client->weaponframe == 39) || (ent->client->weaponframe == 48)) {
            if (rand_byte() & 15)
                return;
        }

        if (++ent->client->weaponframe > 48)
            ent->client->weaponframe = 16;
        return;
    }

    if (ent->client->weaponstate == WEAPON_FIRING) {
        if (ent->client->weaponframe == 5)
            gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/hgrena1b.wav"), 1, ATTN_NORM, 0);

        if (ent->client->weaponframe == 11) {
            if (!ent->client->grenade_framenum) {
                ent->client->grenade_framenum = level.framenum + GRENADE_TIMER + 2;
                ent->client->weapon_sound = gi.soundindex("weapons/hgrenc1b.wav");
            }

            // they waited too long, detonate it in their hand
            if (ent->client->grenade_state != GRENADE_BLEW_UP && level.framenum >= ent->client->grenade_framenum) {
                ent->client->weapon_sound = 0;
                weapon_grenade_fire(ent, true);
                ent->client->grenade_state = GRENADE_BLEW_UP;
            }

            if (ent->client->buttons & BUTTON_ATTACK)
                return;

            if (ent->client->grenade_state == GRENADE_BLEW_UP) {
                if (level.framenum >= ent->client->grenade_framenum) {
                    ent->client->weaponframe = 15;
                    ent->client->grenade_state = GRENADE_NONE;
                } else {
                    return;
                }
            }
        }

        if (ent->client->weaponframe == 12) {
            ent->client->weapon_sound = 0;
            weapon_grenade_fire(ent, false);
            ent->client->grenade_state = GRENADE_THROWN;
        }

        if ((ent->client->weaponframe == 15) && (level.framenum < ent->client->grenade_framenum))
            return;

        ent->client->weaponframe++;

        if (ent->client->weaponframe == 16) {
            ent->client->grenade_framenum = 0;
            ent->client->grenade_state = GRENADE_NONE;
            ent->client->weaponstate = WEAPON_READY;
        }
    }
}

/*
======================================================================

GRENADE LAUNCHER

======================================================================
*/

static void weapon_grenadelauncher_fire(edict_t *ent)
{
    vec3_t  offset;
    vec3_t  forward, right;
    vec3_t  start;
    int     damage = 120;
    float   radius;

    radius = damage + 40;
    if (is_quad)
        damage *= 4;

    VectorSet(offset, 8, 8, ent->viewheight - 8);
    AngleVectors(ent->client->v_angle, forward, right, NULL);
    P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

    VectorScale(forward, -2, ent->client->kick_origin);
    ent->client->kick_angles[0] = -1;

    fire_grenade(ent, start, forward, damage, 600, 2.5f * HZ, radius);

    gi.WriteByte(svc_muzzleflash);
    gi.WriteShort(ent - g_edicts);
    gi.WriteByte(MZ_GRENADE | is_silenced);
    gi.multicast(ent->s.origin, MULTICAST_PVS);

    ent->client->weaponframe++;

    if (ent->client->silencer_shots) {
        ent->client->silencer_shots--;
    }

    if (!DF(INFINITE_AMMO))
        ent->client->inventory[ent->client->ammo_index]--;

    ent->client->resp.frags[FRAG_GRENADELAUNCHER].atts++;
}

void Weapon_GrenadeLauncher(edict_t *ent)
{
    static const int    pause_frames[]  = {34, 51, 59, 0};
    static const int    fire_frames[]   = {6, 0};

    Weapon_Generic(ent, 5, 16, 59, 64, pause_frames, fire_frames, weapon_grenadelauncher_fire);
}

/*
======================================================================

ROCKET

======================================================================
*/

static void weapon_rocketlauncher_fire(edict_t *ent)
{
    vec3_t  offset, start;
    vec3_t  forward, right;
    int     damage;
    float   damage_radius;
    int     radius_damage;

    damage = 100 + (int)(random() * 20.0f);
    radius_damage = 120;
    damage_radius = 120;
    if (is_quad) {
        damage *= 4;
        radius_damage *= 4;
    }

    AngleVectors(ent->client->v_angle, forward, right, NULL);

    VectorScale(forward, -2, ent->client->kick_origin);
    ent->client->kick_angles[0] = -1;

    VectorSet(offset, 8, 8, ent->viewheight - 8);
    P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);
    fire_rocket(ent, start, forward, damage, 650, damage_radius, radius_damage);

    // send muzzle flash
    gi.WriteByte(svc_muzzleflash);
    gi.WriteShort(ent - g_edicts);
    gi.WriteByte(MZ_ROCKET | is_silenced);
    gi.multicast(ent->s.origin, MULTICAST_PVS);

    ent->client->weaponframe++;

    if (ent->client->silencer_shots) {
        ent->client->silencer_shots--;
    }

    if (!DF(INFINITE_AMMO))
        ent->client->inventory[ent->client->ammo_index]--;

    ent->client->resp.frags[FRAG_ROCKETLAUNCHER].atts++;
}

void Weapon_RocketLauncher(edict_t *ent)
{
    static const int    pause_frames[]  = {25, 33, 42, 50, 0};
    static const int    fire_frames[]   = {5, 0};

    Weapon_Generic(ent, 4, 12, 50, 54, pause_frames, fire_frames, weapon_rocketlauncher_fire);
}


/*
======================================================================

BLASTER / HYPERBLASTER

======================================================================
*/

static void blaster_fire(edict_t *ent, vec3_t g_offset, int damage, bool hyper, int effect)
{
    vec3_t  forward, right;
    vec3_t  start;
    vec3_t  offset;

    if (is_quad)
        damage *= 4;
    AngleVectors(ent->client->v_angle, forward, right, NULL);
    VectorSet(offset, 24, 8, ent->viewheight - 8);
    VectorAdd(offset, g_offset, offset);
    P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

    VectorScale(forward, -2, ent->client->kick_origin);
    ent->client->kick_angles[0] = -1;

    fire_blaster(ent, start, forward, damage, 1000, effect, hyper);

    // send muzzle flash
    gi.WriteByte(svc_muzzleflash);
    gi.WriteShort(ent - g_edicts);
    if (hyper)
        gi.WriteByte(MZ_HYPERBLASTER | is_silenced);
    else
        gi.WriteByte(MZ_BLASTER | is_silenced);
    gi.multicast(ent->s.origin, MULTICAST_PVS);

    if (ent->client->silencer_shots) {
        ent->client->silencer_shots--;
    }
}

static void weapon_blaster_fire(edict_t *ent)
{
    blaster_fire(ent, vec3_origin, 15, false, EF_BLASTER);
    ent->client->resp.frags[FRAG_BLASTER].atts++;
    ent->client->weaponframe++;
}

void Weapon_Blaster(edict_t *ent)
{
    static const int    pause_frames[]  = {19, 32, 0};
    static const int    fire_frames[]   = {5, 0};

    Weapon_Generic(ent, 4, 8, 52, 55, pause_frames, fire_frames, weapon_blaster_fire);
}


static void weapon_hyperblaster_fire(edict_t *ent)
{
    float   rotation;
    vec3_t  offset;
    int     effect;

    ent->client->weapon_sound = gi.soundindex("weapons/hyprbl1a.wav");

    if (!(ent->client->buttons & BUTTON_ATTACK)) {
        ent->client->weaponframe++;
    } else {
        if (! ent->client->inventory[ent->client->ammo_index]) {
            NoAmmoWeaponChange(ent);
        } else {
            rotation = (ent->client->weaponframe - 5) * 2 * M_PI / 6;
            offset[0] = -4 * sin(rotation);
            offset[1] = 0;
            offset[2] = 4 * cos(rotation);

            if ((ent->client->weaponframe == 6) || (ent->client->weaponframe == 9))
                effect = EF_HYPERBLASTER;
            else
                effect = 0;
            blaster_fire(ent, offset, 15, true, effect);
            if (!DF(INFINITE_AMMO))
                ent->client->inventory[ent->client->ammo_index]--;

            ent->client->resp.frags[FRAG_HYPERBLASTER].atts++;

            ent->client->anim_priority = ANIM_ATTACK;
            if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
                ent->client->anim_start = FRAME_crattak1;
                ent->client->anim_end = FRAME_crattak9;
            } else {
                ent->client->anim_start = FRAME_attack1;
                ent->client->anim_end = FRAME_attack8;
            }
        }

        ent->client->weaponframe++;
        if (ent->client->weaponframe == 12 && ent->client->inventory[ent->client->ammo_index])
            ent->client->weaponframe = 6;
    }

    if (ent->client->weaponframe == 12) {
        gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/hyprbd1a.wav"), 1, ATTN_NORM, 0);
        ent->client->weapon_sound = 0;
    }

}

void Weapon_HyperBlaster(edict_t *ent)
{
    static const int    pause_frames[]  = {0};
    static const int    fire_frames[]   = {6, 7, 8, 9, 10, 11, 0};

    Weapon_Generic(ent, 5, 20, 49, 53, pause_frames, fire_frames, weapon_hyperblaster_fire);
}

/*
======================================================================

MACHINEGUN / CHAINGUN

======================================================================
*/

static void weapon_machinegun_fire(edict_t *ent)
{
    int i;
    vec3_t      start;
    vec3_t      forward, right;
    vec3_t      angles;
    int         damage = 8;
    int         kick = 2;
    vec3_t      offset;

    if (!(ent->client->buttons & BUTTON_ATTACK)) {
        ent->client->machinegun_shots = 0;
        ent->client->weaponframe++;
        return;
    }

    if (ent->client->weaponframe == 5)
        ent->client->weaponframe = 4;
    else
        ent->client->weaponframe = 5;

    if (ent->client->inventory[ent->client->ammo_index] < 1) {
        ent->client->weaponframe = 6;
        NoAmmoWeaponChange(ent);
        return;
    }

    if (is_quad) {
        damage *= 4;
        kick *= 4;
    }

    for (i = 1; i < 3; i++) {
        ent->client->kick_origin[i] = crandom() * 0.35f;
        ent->client->kick_angles[i] = crandom() * 0.7f;
    }
    ent->client->kick_origin[0] = crandom() * 0.35f;
    ent->client->kick_angles[0] = ent->client->machinegun_shots * -1.5f;

    // get start / end positions
    VectorAdd(ent->client->v_angle, ent->client->kick_angles, angles);
    AngleVectors(angles, forward, right, NULL);
    VectorSet(offset, 0, 8, ent->viewheight - 8);
    P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);
    G_BeginDamage();
    fire_bullet(ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_MACHINEGUN);
    G_EndDamage();

    gi.WriteByte(svc_muzzleflash);
    gi.WriteShort(ent - g_edicts);
    gi.WriteByte(MZ_MACHINEGUN | is_silenced);
    gi.multicast(ent->s.origin, MULTICAST_PVS);

    if (ent->client->silencer_shots) {
        ent->client->silencer_shots--;
    }

    if (!DF(INFINITE_AMMO))
        ent->client->inventory[ent->client->ammo_index]--;

    ent->client->resp.frags[FRAG_MACHINEGUN].atts++;

    ent->client->anim_priority = ANIM_ATTACK;
    if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
        ent->client->anim_start = FRAME_crattak1 + 1 - (int)(random() + 0.25f);
        ent->client->anim_end = FRAME_crattak9;
    } else {
        ent->client->anim_start = FRAME_attack1 + 1 - (int)(random() + 0.25f);
        ent->client->anim_end = FRAME_attack8;
    }
}

void Weapon_Machinegun(edict_t *ent)
{
    static const int    pause_frames[]  = {23, 45, 0};
    static const int    fire_frames[]   = {4, 5, 0};

    Weapon_Generic(ent, 3, 5, 45, 49, pause_frames, fire_frames, weapon_machinegun_fire);
}

static void weapon_chaingun_fire(edict_t *ent)
{
    int         i;
    int         shots;
    vec3_t      start;
    vec3_t      forward, right, up;
    float       r, u;
    vec3_t      offset;
    int         damage = 6;
    int         kick = 2;

    if (ent->client->weaponframe == 5)
        gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_IDLE, 0);

    if ((ent->client->weaponframe == 14) && !(ent->client->buttons & BUTTON_ATTACK)) {
        ent->client->weaponframe = 32;
        ent->client->weapon_sound = 0;
        return;
    } else if ((ent->client->weaponframe == 21) && (ent->client->buttons & BUTTON_ATTACK)
               && ent->client->inventory[ent->client->ammo_index]) {
        ent->client->weaponframe = 15;
    } else {
        ent->client->weaponframe++;
    }

    if (ent->client->weaponframe == 22) {
        ent->client->weapon_sound = 0;
        gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnd1a.wav"), 1, ATTN_IDLE, 0);
    } else {
        ent->client->weapon_sound = gi.soundindex("weapons/chngnl1a.wav");
    }

    ent->client->anim_priority = ANIM_ATTACK;
    if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
        ent->client->anim_start = FRAME_crattak1 + 1 - (ent->client->weaponframe & 1);
        ent->client->anim_end = FRAME_crattak9;
    } else {
        ent->client->anim_start = FRAME_attack1 + 1 - (ent->client->weaponframe & 1);
        ent->client->anim_end = FRAME_attack8;
    }

    if (ent->client->weaponframe <= 9)
        shots = 1;
    else if (ent->client->weaponframe <= 14) {
        if (ent->client->buttons & BUTTON_ATTACK)
            shots = 2;
        else
            shots = 1;
    } else
        shots = 3;

    if (ent->client->inventory[ent->client->ammo_index] < shots)
        shots = ent->client->inventory[ent->client->ammo_index];

    if (!shots) {
        NoAmmoWeaponChange(ent);
        return;
    }

    if (is_quad) {
        damage *= 4;
        kick *= 4;
    }

    for (i = 0; i < 3; i++) {
        ent->client->kick_origin[i] = crandom() * 0.35f;
        ent->client->kick_angles[i] = crandom() * 0.7f;
    }

    G_BeginDamage();
    for (i = 0; i < shots; i++) {
        // get start / end positions
        AngleVectors(ent->client->v_angle, forward, right, up);
        r = 7 + crandom() * 4;
        u = crandom() * 4;
        VectorSet(offset, 0, r, u + ent->viewheight - 8);
        P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

        fire_bullet(ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_CHAINGUN);
    }
    G_EndDamage();

    // send muzzle flash
    gi.WriteByte(svc_muzzleflash);
    gi.WriteShort(ent - g_edicts);
    gi.WriteByte((MZ_CHAINGUN1 + shots - 1) | is_silenced);
    gi.multicast(ent->s.origin, MULTICAST_PVS);

    if (ent->client->silencer_shots) {
        ent->client->silencer_shots--;
    }

    if (!DF(INFINITE_AMMO))
        ent->client->inventory[ent->client->ammo_index] -= shots;

    ent->client->resp.frags[FRAG_CHAINGUN].atts++;
}


void Weapon_Chaingun(edict_t *ent)
{
    static const int    pause_frames[]  = {38, 43, 51, 61, 0};
    static const int    fire_frames[]   = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 0};

    Weapon_Generic(ent, 4, 31, 61, 64, pause_frames, fire_frames, weapon_chaingun_fire);
}


/*
======================================================================

SHOTGUN / SUPERSHOTGUN

======================================================================
*/

static void weapon_shotgun_fire(edict_t *ent)
{
    vec3_t      start;
    vec3_t      forward, right;
    vec3_t      offset;
    int         damage = 4;
    int         kick = 8;

    if (ent->client->weaponframe == 9) {
        ent->client->weaponframe++;
        return;
    }

    AngleVectors(ent->client->v_angle, forward, right, NULL);

    VectorScale(forward, -2, ent->client->kick_origin);
    ent->client->kick_angles[0] = -2;

    VectorSet(offset, 0, 8,  ent->viewheight - 8);
    P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

    if (is_quad) {
        damage *= 4;
        kick *= 4;
    }

    G_BeginDamage();
    fire_shotgun(ent, start, forward, damage, kick, 500, 500, DEFAULT_DEATHMATCH_SHOTGUN_COUNT, MOD_SHOTGUN);
    G_EndDamage();

    // send muzzle flash
    gi.WriteByte(svc_muzzleflash);
    gi.WriteShort(ent - g_edicts);
    gi.WriteByte(MZ_SHOTGUN | is_silenced);
    gi.multicast(ent->s.origin, MULTICAST_PVS);

    ent->client->weaponframe++;

    if (ent->client->silencer_shots) {
        ent->client->silencer_shots--;
    }

    if (!DF(INFINITE_AMMO))
        ent->client->inventory[ent->client->ammo_index]--;

    ent->client->resp.frags[FRAG_SHOTGUN].atts++;
}

void Weapon_Shotgun(edict_t *ent)
{
    static const int    pause_frames[]  = {22, 28, 34, 0};
    static const int    fire_frames[]   = {8, 9, 0};

    Weapon_Generic(ent, 7, 18, 36, 39, pause_frames, fire_frames, weapon_shotgun_fire);
}


static void weapon_supershotgun_fire(edict_t *ent)
{
    vec3_t      start;
    vec3_t      forward, right;
    vec3_t      offset;
    vec3_t      v;
    int         damage = 6;
    int         kick = 12;

    AngleVectors(ent->client->v_angle, forward, right, NULL);

    VectorScale(forward, -2, ent->client->kick_origin);
    ent->client->kick_angles[0] = -2;

    VectorSet(offset, 0, 8,  ent->viewheight - 8);
    P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

    if (is_quad) {
        damage *= 4;
        kick *= 4;
    }

    G_BeginDamage();
    v[PITCH] = ent->client->v_angle[PITCH];
    v[YAW]   = ent->client->v_angle[YAW] - 5;
    v[ROLL]  = ent->client->v_angle[ROLL];
    AngleVectors(v, forward, NULL, NULL);
    fire_shotgun(ent, start, forward, damage, kick, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD, DEFAULT_SSHOTGUN_COUNT / 2, MOD_SSHOTGUN);
    v[YAW]   = ent->client->v_angle[YAW] + 5;
    AngleVectors(v, forward, NULL, NULL);
    fire_shotgun(ent, start, forward, damage, kick, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD, DEFAULT_SSHOTGUN_COUNT / 2, MOD_SSHOTGUN);
    G_EndDamage();

    // send muzzle flash
    gi.WriteByte(svc_muzzleflash);
    gi.WriteShort(ent - g_edicts);
    gi.WriteByte(MZ_SSHOTGUN | is_silenced);
    gi.multicast(ent->s.origin, MULTICAST_PVS);

    ent->client->weaponframe++;

    if (ent->client->silencer_shots) {
        ent->client->silencer_shots--;
    }

    if (!DF(INFINITE_AMMO))
        ent->client->inventory[ent->client->ammo_index] -= 2;

    ent->client->resp.frags[FRAG_SUPERSHOTGUN].atts++;
}

void Weapon_SuperShotgun(edict_t *ent)
{
    static const int    pause_frames[]  = {29, 42, 57, 0};
    static const int    fire_frames[]   = {7, 0};

    Weapon_Generic(ent, 6, 17, 57, 61, pause_frames, fire_frames, weapon_supershotgun_fire);
}



/*
======================================================================

RAILGUN

======================================================================
*/

static void weapon_railgun_fire(edict_t *ent)
{
    vec3_t      start;
    vec3_t      forward, right;
    vec3_t      offset;
    int         damage = 100;
    int         kick = 200;

    if (is_quad) {
        damage *= 4;
        kick *= 4;
    }

    AngleVectors(ent->client->v_angle, forward, right, NULL);

    VectorScale(forward, -3, ent->client->kick_origin);
    ent->client->kick_angles[0] = -3;

    VectorSet(offset, 0, 7,  ent->viewheight - 8);
    P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);
    fire_rail(ent, start, forward, damage, kick);

    // send muzzle flash
    gi.WriteByte(svc_muzzleflash);
    gi.WriteShort(ent - g_edicts);
    gi.WriteByte(MZ_RAILGUN | is_silenced);
    gi.multicast(ent->s.origin, MULTICAST_PVS);

    ent->client->weaponframe++;

    if (ent->client->silencer_shots) {
        ent->client->silencer_shots--;
    }

    if (!DF(INFINITE_AMMO))
        ent->client->inventory[ent->client->ammo_index]--;

    ent->client->resp.frags[FRAG_RAILGUN].atts++;
}


void Weapon_Railgun(edict_t *ent)
{
    static const int    pause_frames[]  = {56, 0};
    static const int    fire_frames[]   = {4, 0};

    Weapon_Generic(ent, 3, 18, 56, 61, pause_frames, fire_frames, weapon_railgun_fire);
}


/*
======================================================================

BFG10K

======================================================================
*/

static void weapon_bfg_fire(edict_t *ent)
{
    vec3_t  offset, start;
    vec3_t  forward, right;
    int     damage = 200;
    float   damage_radius = 1000;

    if (ent->client->weaponframe == 9) {
        // send muzzle flash
        gi.WriteByte(svc_muzzleflash);
        gi.WriteShort(ent - g_edicts);
        gi.WriteByte(MZ_BFG | is_silenced);
        gi.multicast(ent->s.origin, MULTICAST_PVS);

        ent->client->weaponframe++;

        if (ent->client->silencer_shots) {
            ent->client->silencer_shots--;
        }
        return;
    }

    // cells can go down during windup (from power armor hits), so
    // check again and abort firing if we don't have enough now
    if (ent->client->inventory[ent->client->ammo_index] < 50) {
        ent->client->weaponframe++;
        return;
    }

    if (is_quad)
        damage *= 4;

    AngleVectors(ent->client->v_angle, forward, right, NULL);

    VectorScale(forward, -2, ent->client->kick_origin);

    // make a big pitch kick with an inverse fall
    ent->client->v_dmg_pitch = -40;
    ent->client->v_dmg_roll = crandom() * 8;
    ent->client->v_dmg_time = level.time + DAMAGE_TIME;

    VectorSet(offset, 8, 8, ent->viewheight - 8);
    P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);
    fire_bfg(ent, start, forward, damage, 400, damage_radius);

    ent->client->weaponframe++;

    if (ent->client->silencer_shots) {
        ent->client->silencer_shots--;
    }

    if (!DF(INFINITE_AMMO))
        ent->client->inventory[ent->client->ammo_index] -= 50;
}

void Weapon_BFG(edict_t *ent)
{
    static const int    pause_frames[]  = {39, 45, 50, 55, 0};
    static const int    fire_frames[]   = {9, 17, 0};

    Weapon_Generic(ent, 8, 32, 55, 58, pause_frames, fire_frames, weapon_bfg_fire);
}


//======================================================================
