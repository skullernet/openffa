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



static  edict_t     *current_player;
static  gclient_t   *current_client;

static  vec3_t  forward, right, up;
static  float   xyspeed;

static float    bobmove;
static int      bobcycle;       // odd cycles are right foot going forward
static float    bobfracsin;     // sin(bobfrac*M_PI)

/*
===============
P_CalcRoll

===============
*/
static float P_CalcRoll(vec3_t angles, vec3_t velocity)
{
    float   sign;
    float   side;
    float   value;

    side = DotProduct(velocity, right);
    sign = side < 0 ? -1 : 1;
    side = fabsf(side);

    value = sv_rollangle->value;

    if (side < sv_rollspeed->value)
        side = side * value / sv_rollspeed->value;
    else
        side = value;

    return side * sign;
}


/*
===============
P_DamageFeedback

Handles color blends and view kicks
===============
*/
static void P_DamageFeedback(edict_t *player)
{
    gclient_t   *client;
    float   side;
    float   realcount, count, kick;
    vec3_t  v;
    int     r, l;
    static  const vec3_t power_color = {0.0, 1.0, 0.0};
    static  const vec3_t acolor = {1.0, 1.0, 1.0};
    static  const vec3_t bcolor = {1.0, 0.0, 0.0};

    if (!FRAMESYNC)
        return;

    client = player->client;

    // flash the backgrounds behind the status numbers
    client->ps.stats[STAT_FLASHES] = 0;
    if (client->damage_blood)
        client->ps.stats[STAT_FLASHES] |= 1;
    if (client->damage_armor && !(player->flags & FL_GODMODE) && (client->invincible_framenum <= level.framenum))
        client->ps.stats[STAT_FLASHES] |= 2;

    // total points of damage shot at the player this frame
    count = (client->damage_blood + client->damage_armor + client->damage_parmor);
    if (count == 0)
        return;     // didn't take any damage

    // start a pain animation if still in the player model
    if (client->anim_priority < ANIM_PAIN && player->s.modelindex == 255) {
        static int      i;

        client->anim_priority = ANIM_PAIN;
        if (client->ps.pmove.pm_flags & PMF_DUCKED) {
            client->anim_start = FRAME_crpain1;
            client->anim_end = FRAME_crpain4;
        } else {
            i = (i + 1) % 3;
            switch (i) {
            case 0:
                client->anim_start = FRAME_pain101;
                client->anim_end = FRAME_pain104;
                break;
            case 1:
                client->anim_start = FRAME_pain201;
                client->anim_end = FRAME_pain204;
                break;
            case 2:
                client->anim_start = FRAME_pain301;
                client->anim_end = FRAME_pain304;
                break;
            }
        }
    }

    realcount = count;
    if (count < 10)
        count = 10; // always make a visible effect

    // play an apropriate pain sound
    if ((level.framenum > player->pain_debounce_framenum) && !(player->flags & FL_GODMODE) && (client->invincible_framenum <= level.framenum)) {
        r = Q_rand() & 1;
        player->pain_debounce_framenum = level.framenum + 0.7f * HZ;
        if (player->health < 25)
            l = 0;
        else if (player->health < 50)
            l = 1;
        else if (player->health < 75)
            l = 2;
        else
            l = 3;
        gi.sound(player, CHAN_VOICE, level.sounds.pain[l][r], 1, ATTN_NORM, 0);
    }

    // the total alpha of the blend is always proportional to count
    if (client->damage_alpha < 0)
        client->damage_alpha = 0;
    client->damage_alpha += count * 0.01f;
    if (client->damage_alpha < 0.2f)
        client->damage_alpha = 0.2f;
    if (client->damage_alpha > 0.6f)
        client->damage_alpha = 0.6f;    // don't go too saturated

    // the color of the blend will vary based on how much was absorbed
    // by different armors
    VectorClear(v);
    if (client->damage_parmor)
        VectorMA(v, (float)client->damage_parmor / realcount, power_color, v);
    if (client->damage_armor)
        VectorMA(v, (float)client->damage_armor / realcount,  acolor, v);
    if (client->damage_blood)
        VectorMA(v, (float)client->damage_blood / realcount,  bcolor, v);
    VectorCopy(v, client->damage_blend);


    //
    // calculate view angle kicks
    //
    kick = abs(client->damage_knockback);
    if (kick && player->health > 0) { // kick of 0 means no view adjust at all
        kick = kick * 100 / player->health;

        if (kick < count * 0.5f)
            kick = count * 0.5f;
        if (kick > 50)
            kick = 50;

        VectorSubtract(client->damage_from, player->s.origin, v);
        VectorNormalize(v);

        side = DotProduct(v, right);
        client->v_dmg_roll = kick * side * 0.3f;

        side = -DotProduct(v, forward);
        client->v_dmg_pitch = kick * side * 0.3f;

        client->v_dmg_time = level.time + DAMAGE_TIME;
    }

    //
    // clear totals
    //
    client->damage_blood = 0;
    client->damage_armor = 0;
    client->damage_parmor = 0;
    client->damage_knockback = 0;
}




/*
===============
P_CalcViewOffset

Auto pitching on slopes?

  fall from 128: 400 = 160000
  fall from 256: 580 = 336400
  fall from 384: 720 = 518400
  fall from 512: 800 = 640000
  fall from 640: 960 =

  damage = deltavelocity*deltavelocity  * 0.0001

===============
*/
static void P_CalcViewOffset(edict_t *ent)
{
    float       *angles;
    float       bob;
    float       ratio;
    float       delta;
    vec3_t      v;

    if (ent->movetype == MOVETYPE_NOCLIP) {
        // don't add any kicks/bobs for spectators
        VectorClear(ent->client->ps.kick_angles);
        VectorSet(ent->client->ps.viewoffset, 0, 0, ent->viewheight);
        return;
    }

    // if dead, fix the angle
    if (ent->deadflag) {
        ent->client->ps.viewangles[ROLL] = 40;
        ent->client->ps.viewangles[PITCH] = -15;
        ent->client->ps.viewangles[YAW] = ent->client->killer_yaw;
    }

    if (!FRAMESYNC)
        return;

//===================================

    // base angles
    angles = ent->client->ps.kick_angles;

    // if dead, don't add any kick
    if (ent->deadflag) {
        VectorClear(angles);
    } else {
        // add angles based on weapon kick
        VectorCopy(ent->client->kick_angles, angles);

        // add angles based on damage kick

        ratio = (ent->client->v_dmg_time - level.time) / DAMAGE_TIME;
        if (ratio < 0) {
            ratio = 0;
            ent->client->v_dmg_pitch = 0;
            ent->client->v_dmg_roll = 0;
        }
        angles[PITCH] += ratio * ent->client->v_dmg_pitch;
        angles[ROLL] += ratio * ent->client->v_dmg_roll;

        // add pitch based on fall kick

        ratio = (ent->client->fall_time - level.time) / FALL_TIME;
        if (ratio < 0)
            ratio = 0;
        angles[PITCH] += ratio * ent->client->fall_value;

        // add angles based on velocity

        delta = DotProduct(ent->velocity, forward);
        angles[PITCH] += delta * run_pitch->value;

        delta = DotProduct(ent->velocity, right);
        angles[ROLL] += delta * run_roll->value;

        // add angles based on bob

        delta = bobfracsin * bob_pitch->value * xyspeed;
        if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
            delta *= 6;     // crouching
        angles[PITCH] += delta;
        delta = bobfracsin * bob_roll->value * xyspeed;
        if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
            delta *= 6;     // crouching
        if (bobcycle & 1)
            delta = -delta;
        angles[ROLL] += delta;
    }

//===================================

    // base origin

    VectorClear(v);

    // add view height

    v[2] += ent->viewheight;

    // add fall height

    ratio = (ent->client->fall_time - level.time) / FALL_TIME;
    if (ratio < 0)
        ratio = 0;
    v[2] -= ratio * ent->client->fall_value * 0.4f;

    // add bob height

    bob = bobfracsin * xyspeed * bob_up->value;
    if (bob > 6)
        bob = 6;
    //gi.DebugGraph (bob *2, 255);
    v[2] += bob;

    // add kick offset

    VectorAdd(v, ent->client->kick_origin, v);

    // absolutely bound offsets
    // so the view can never be outside the player box

    clamp(v[0], -14, 14);
    clamp(v[1], -14, 14);
    clamp(v[2], -22, 30);

    VectorCopy(v, ent->client->ps.viewoffset);
}

/*
==============
P_CalcGunOffset
==============
*/
static void P_CalcGunOffset(edict_t *ent)
{
    int     i;
    float   delta;

    if (!FRAMESYNC)
        return;

    ent->client->ps.gunframe = ent->client->weaponframe;

    // gun angles from bobbing
    ent->client->ps.gunangles[ROLL] = xyspeed * bobfracsin * 0.005f;
    ent->client->ps.gunangles[YAW] = xyspeed * bobfracsin * 0.01f;
    if (bobcycle & 1) {
        ent->client->ps.gunangles[ROLL] = -ent->client->ps.gunangles[ROLL];
        ent->client->ps.gunangles[YAW] = -ent->client->ps.gunangles[YAW];
    }

    ent->client->ps.gunangles[PITCH] = xyspeed * bobfracsin * 0.005f;

    // gun angles from delta movement
    for (i = 0; i < 3; i++) {
        delta = ent->client->oldviewangles[i] - ent->client->ps.viewangles[i];
        if (delta > 180)
            delta -= 360;
        if (delta < -180)
            delta += 360;
        clamp(delta, -45, 45);
        if (i == YAW)
            ent->client->ps.gunangles[ROLL] += 0.1f * delta;
        ent->client->ps.gunangles[i] += 0.2f * delta;
    }

    // gun height
    VectorClear(ent->client->ps.gunoffset);
//  ent->ps->gunorigin[2] += bob;

    // gun_x / gun_y / gun_z are development tools
    for (i = 0; i < 3; i++) {
        ent->client->ps.gunoffset[i] += forward[i] * (gun_y->value);
        ent->client->ps.gunoffset[i] += right[i] * gun_x->value;
        ent->client->ps.gunoffset[i] += up[i] * (-gun_z->value);
    }
}


/*
=============
P_AddBlend
=============
*/
static void P_AddBlend(float r, float g, float b, float a, float *v_blend)
{
    float   a2, a3;

    if (a <= 0)
        return;
    a2 = v_blend[3] + (1 - v_blend[3]) * a; // new total alpha
    a3 = v_blend[3] / a2;   // fraction of color from old

    v_blend[0] = v_blend[0] * a3 + r * (1 - a3);
    v_blend[1] = v_blend[1] * a3 + g * (1 - a3);
    v_blend[2] = v_blend[2] * a3 + b * (1 - a3);
    v_blend[3] = a2;
}


/*
=============
P_CalcBlend
=============
*/
static void P_CalcBlend(edict_t *ent)
{
    int     contents;
    vec3_t  vieworg;
    int     remaining;

    Vector4Clear(ent->client->ps.blend);

    // add for contents
    VectorAdd(ent->s.origin, ent->client->ps.viewoffset, vieworg);
    contents = gi.pointcontents(vieworg);
    if (contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER))
        ent->client->ps.rdflags |= RDF_UNDERWATER;
    else
        ent->client->ps.rdflags &= ~RDF_UNDERWATER;

    if (contents & (CONTENTS_SOLID | CONTENTS_LAVA))
        P_AddBlend(1.0f, 0.3f, 0.0f, 0.6f, ent->client->ps.blend);
    else if (contents & CONTENTS_SLIME)
        P_AddBlend(0.0f, 0.1f, 0.05f, 0.6f, ent->client->ps.blend);
    else if (contents & CONTENTS_WATER)
        P_AddBlend(0.5f, 0.3f, 0.2f, 0.4f, ent->client->ps.blend);

    // add for powerups
    if (ent->client->quad_framenum > level.framenum) {
        remaining = ent->client->quad_framenum - level.framenum;
        if (remaining == 3 * HZ)  // beginning to fade
            gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage2.wav"), 1, ATTN_NORM, 0);
        if (remaining > 3 * HZ || ((remaining / FRAMEDIV) & 4))
            P_AddBlend(0, 0, 1, 0.08f, ent->client->ps.blend);
    } else if (ent->client->invincible_framenum > level.framenum) {
        remaining = ent->client->invincible_framenum - level.framenum;
        if (remaining == 3 * HZ)  // beginning to fade
            gi.sound(ent, CHAN_ITEM, gi.soundindex("items/protect2.wav"), 1, ATTN_NORM, 0);
        if (remaining > 3 * HZ || ((remaining / FRAMEDIV) & 4))
            P_AddBlend(1, 1, 0, 0.08f, ent->client->ps.blend);
    } else if (ent->client->enviro_framenum > level.framenum) {
        remaining = ent->client->enviro_framenum - level.framenum;
        if (remaining == 3 * HZ)  // beginning to fade
            gi.sound(ent, CHAN_ITEM, gi.soundindex("items/airout.wav"), 1, ATTN_NORM, 0);
        if (remaining > 3 * HZ || ((remaining / FRAMEDIV) & 4))
            P_AddBlend(0, 1, 0, 0.08f, ent->client->ps.blend);
    } else if (ent->client->breather_framenum > level.framenum) {
        remaining = ent->client->breather_framenum - level.framenum;
        if (remaining == 3 * HZ)  // beginning to fade
            gi.sound(ent, CHAN_ITEM, gi.soundindex("items/airout.wav"), 1, ATTN_NORM, 0);
        if (remaining > 3 * HZ || ((remaining / FRAMEDIV) & 4))
            P_AddBlend(0.4f, 1, 0.4f, 0.04f, ent->client->ps.blend);
    }

    // add for damage
    if (ent->client->damage_alpha > 0)
        P_AddBlend(ent->client->damage_blend[0], ent->client->damage_blend[1],
                   ent->client->damage_blend[2], ent->client->damage_alpha,
                   ent->client->ps.blend);

    if (ent->client->bonus_alpha > 0)
        P_AddBlend(0.85f, 0.7f, 0.3f, ent->client->bonus_alpha, ent->client->ps.blend);

    // drop the damage value
    ent->client->damage_alpha -= 0.6f * FRAMETIME;
    if (ent->client->damage_alpha < 0)
        ent->client->damage_alpha = 0;

    // drop the bonus value
    ent->client->bonus_alpha -= 1.0f * FRAMETIME;
    if (ent->client->bonus_alpha < 0)
        ent->client->bonus_alpha = 0;
}


/*
=================
P_FallingDamage
=================
*/
static void P_FallingDamage(edict_t *ent)
{
    float   delta;
    int     damage;
    vec3_t  dir;

    if (ent->s.modelindex != 255)
        return;     // not in the player model

    if (ent->movetype == MOVETYPE_NOCLIP)
        return;

    if (!FRAMESYNC)
        return;

    if ((ent->client->oldvelocity[2] < 0) && (ent->velocity[2] > ent->client->oldvelocity[2]) && (!ent->groundentity)) {
        delta = ent->client->oldvelocity[2];
    } else {
        if (!ent->groundentity)
            return;
        delta = ent->velocity[2] - ent->client->oldvelocity[2];
    }
    delta = delta * delta * 0.0001f;

    // never take falling damage if completely underwater
    if (ent->waterlevel == 3)
        return;
    if (ent->waterlevel == 2)
        delta *= 0.25f;
    if (ent->waterlevel == 1)
        delta *= 0.5f;

    if (delta < 1)
        return;

    if (delta < 15) {
        if (!ent->s.event)
            ent->s.event = EV_FOOTSTEP;
        return;
    }

    ent->client->fall_value = delta * 0.5f;
    if (ent->client->fall_value > 40)
        ent->client->fall_value = 40;
    ent->client->fall_time = level.time + FALL_TIME;

    if (delta > 30) {
        if (ent->health > 0) {
            if (delta >= 55)
                ent->s.event = EV_FALLFAR;
            else
                ent->s.event = EV_FALL;
        }
        ent->pain_debounce_framenum = KEYFRAME(FRAMEDIV);   // no normal pain sound
        damage = (delta - 30) / 2;
        if (damage < 1)
            damage = 1;
        VectorSet(dir, 0, 0, 1);

        if (!DF(NO_FALLING))
            T_Damage(ent, world, world, dir, ent->s.origin, vec3_origin, damage, 0, 0, MOD_FALLING);
    } else {
        ent->s.event = EV_FALLSHORT;
        return;
    }
}



/*
=============
P_WorldEffects
=============
*/
static void P_WorldEffects(void)
{
    bool        breather;
    bool        envirosuit;
    int         waterlevel, old_waterlevel;
    int         r;

    if (current_player->movetype == MOVETYPE_NOCLIP) {
        current_player->air_finished_framenum = level.framenum + 12 * HZ; // don't need air
        return;
    }

    waterlevel = current_player->waterlevel;
    old_waterlevel = current_client->old_waterlevel;
    current_client->old_waterlevel = waterlevel;

    breather = current_client->breather_framenum > level.framenum;
    envirosuit = current_client->enviro_framenum > level.framenum;

    //
    // if just entered a water volume, play a sound
    //
    if (!old_waterlevel && waterlevel) {
        if (current_player->watertype & CONTENTS_LAVA)
            gi.sound(current_player, CHAN_BODY, level.sounds.lava_in, 1, ATTN_NORM, 0);
        else if (current_player->watertype & (CONTENTS_WATER | CONTENTS_SLIME))
            gi.sound(current_player, CHAN_BODY, level.sounds.watr_in, 1, ATTN_NORM, 0);
        current_player->flags |= FL_INWATER;
    }

    //
    // if just completely exited a water volume, play a sound
    //
    if (old_waterlevel && ! waterlevel) {
        gi.sound(current_player, CHAN_BODY, level.sounds.watr_out, 1, ATTN_NORM, 0);
        current_player->flags &= ~FL_INWATER;
    }

    //
    // check for head just going under water
    //
    if (old_waterlevel != 3 && waterlevel == 3) {
        gi.sound(current_player, CHAN_BODY, level.sounds.watr_un, 1, ATTN_NORM, 0);
    }

    //
    // check for head just coming out of water
    //
    if (old_waterlevel == 3 && waterlevel != 3) {
        if (current_player->air_finished_framenum < level.framenum) {
            // gasp for air
            gi.sound(current_player, CHAN_VOICE, level.sounds.gasp[0], 1, ATTN_NORM, 0);
        } else  if (current_player->air_finished_framenum < level.framenum + 11 * HZ) {
            // just break surface
            gi.sound(current_player, CHAN_VOICE, level.sounds.gasp[1], 1, ATTN_NORM, 0);
        }
    }

    //
    // check for drowning
    //
    if (waterlevel == 3) {
        // breather or envirosuit give air
        if (breather || envirosuit) {
            current_player->air_finished_framenum = level.framenum + 10 * HZ;

            if (((current_client->breather_framenum - level.framenum) % (25 * FRAMEDIV)) == 0) {
                if (!current_client->breather_sound)
                    gi.sound(current_player, CHAN_AUTO, level.sounds.breath[0], 1, ATTN_NORM, 0);
                else
                    gi.sound(current_player, CHAN_AUTO, level.sounds.breath[1], 1, ATTN_NORM, 0);
                current_client->breather_sound ^= 1;
                //FIXME: release a bubble?
            }
        }

        // if out of air, start drowning
        if (current_player->air_finished_framenum < level.framenum) {
            // drown!
            if (current_player->client->next_drown_framenum < level.framenum
                && current_player->health > 0) {
                current_player->client->next_drown_framenum = level.framenum + 1 * HZ;

                // take more damage the longer underwater
                current_player->dmg += 2;
                if (current_player->dmg > 15)
                    current_player->dmg = 15;

                // play a gurp sound instead of a normal pain sound
                if (current_player->health <= current_player->dmg) {
                    gi.sound(current_player, CHAN_VOICE, level.sounds.drown, 1, ATTN_NORM, 0);
                } else {
                    r = Q_rand() & 1;
                    gi.sound(current_player, CHAN_VOICE, level.sounds.gurp[r], 1, ATTN_NORM, 0);
                }

                current_player->pain_debounce_framenum = KEYFRAME(FRAMEDIV);

                T_Damage(current_player, world, world, vec3_origin, current_player->s.origin, vec3_origin, current_player->dmg, 0, DAMAGE_NO_ARMOR, MOD_WATER);
            }
        }
    } else {
        current_player->air_finished_framenum = level.framenum + 12 * HZ;
        current_player->dmg = 2;
    }

    //
    // check for sizzle damage
    //
    if (waterlevel && (current_player->watertype & (CONTENTS_LAVA | CONTENTS_SLIME))) {
        if (current_player->watertype & CONTENTS_LAVA) {
            if (current_player->health > 0
                && current_player->pain_debounce_framenum <= level.framenum
                && current_client->invincible_framenum < level.framenum) {
                r = Q_rand() & 1;
                gi.sound(current_player, CHAN_VOICE, level.sounds.burn[r], 1, ATTN_NORM, 0);
                current_player->pain_debounce_framenum = level.framenum + 1 * HZ;
            }

            if (FRAMESYNC) {
                if (envirosuit) // take 1/3 damage with envirosuit
                    T_Damage(current_player, world, world, vec3_origin, current_player->s.origin, vec3_origin, 1 * waterlevel, 0, 0, MOD_LAVA);
                else
                    T_Damage(current_player, world, world, vec3_origin, current_player->s.origin, vec3_origin, 3 * waterlevel, 0, 0, MOD_LAVA);
            }
        }

        if (current_player->watertype & CONTENTS_SLIME) {
            if (!envirosuit && FRAMESYNC) {
                // no damage from slime with envirosuit
                T_Damage(current_player, world, world, vec3_origin, current_player->s.origin, vec3_origin, 1 * waterlevel, 0, 0, MOD_SLIME);
            }
        }
    }
}


/*
===============
P_SetEffects
===============
*/
static void P_SetEffects(edict_t *ent)
{
    int     pa_type;
    int     remaining;

    ent->s.effects = 0;
    ent->s.renderfx = 0;

    if (ent->health <= 0 || level.intermission_framenum)
        return;

    if (ent->client->powerarmor_framenum > level.framenum) {
        pa_type = PowerArmorIndex(ent);
        if (pa_type == ITEM_POWER_SCREEN) {
            ent->s.effects |= EF_POWERSCREEN;
        } else if (pa_type == ITEM_POWER_SHIELD) {
            ent->s.effects |= EF_COLOR_SHELL;
            ent->s.renderfx |= RF_SHELL_GREEN;
        }
    }

    if (ent->client->quad_framenum > level.framenum) {
        remaining = ent->client->quad_framenum - level.framenum;
        if (remaining > 3 * HZ || ((remaining / FRAMEDIV) & 4))
            ent->s.effects |= EF_QUAD;
    }

    if (ent->client->invincible_framenum > level.framenum) {
        remaining = ent->client->invincible_framenum - level.framenum;
        if (remaining > 3 * HZ || ((remaining / FRAMEDIV) & 4))
            ent->s.effects |= EF_PENT;
    }

    // show cheaters!!!
    if (ent->flags & FL_GODMODE) {
        ent->s.effects |= EF_COLOR_SHELL;
        ent->s.renderfx |= (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE);
    }
}


/*
===============
P_SetEvent
===============
*/
static void P_SetEvent(edict_t *ent)
{
    if (ent->s.event)
        return;

    if (!FRAMESYNC)
        return;

    if (ent->groundentity && xyspeed > 225) {
        if ((int)(current_client->bobtime + bobmove) != bobcycle)
            ent->s.event = EV_FOOTSTEP;
    }
}

/*
===============
P_SetSound
===============
*/
static void P_SetSound(edict_t *ent)
{
    int weap;

    if (ent->client->weapon)
        weap = ent->client->weapon->weapmodel;
    else
        weap = 0;

    if (ent->waterlevel && (ent->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
        ent->s.sound = level.sounds.fry;
    else if (weap == WEAP_RAILGUN)
        ent->s.sound = level.sounds.rg_hum;
    else if (weap == WEAP_BFG)
        ent->s.sound = level.sounds.bfg_hum;
    else if (ent->client->weapon_sound)
        ent->s.sound = ent->client->weapon_sound;
    else
        ent->s.sound = 0;
}

/*
===============
P_SetFrame
===============
*/
static void P_SetFrame(edict_t *ent)
{
    gclient_t   *client;
    bool        duck, run;

    if (ent->s.modelindex != 255)
        return;     // not in the player model

    if (!FRAMESYNC)
        return;

    client = ent->client;

    if (client->ps.pmove.pm_flags & PMF_DUCKED)
        duck = true;
    else
        duck = false;
    if (xyspeed)
        run = true;
    else
        run = false;

    // check for stand/duck and stop/go transitions
    if (duck != client->anim_duck && client->anim_priority < ANIM_DEATH)
        goto newanim;
    if (run != client->anim_run && client->anim_priority == ANIM_BASIC)
        goto newanim;
    if (!ent->groundentity && client->anim_priority <= ANIM_WAVE)
        goto newanim;

    if (client->anim_start) {
        ent->s.frame = client->anim_start;
        client->anim_start = 0;
        return;
    }

    if (client->anim_priority == ANIM_REVERSE) {
        if (ent->s.frame > client->anim_end) {
            ent->s.frame--;
            return;
        }
    } else if (ent->s.frame < client->anim_end) {
        // continue an animation
        ent->s.frame++;
        return;
    }

    if (client->anim_priority == ANIM_DEATH)
        return;     // stay there
    if (client->anim_priority == ANIM_JUMP) {
        if (!ent->groundentity)
            return;     // stay there
        ent->client->anim_priority = ANIM_WAVE;
        ent->s.frame = FRAME_jump3;
        ent->client->anim_end = FRAME_jump6;
        return;
    }

newanim:
    // return to either a running or standing frame
    client->anim_priority = ANIM_BASIC;
    client->anim_duck = duck;
    client->anim_run = run;

    if (!ent->groundentity) {
        client->anim_priority = ANIM_JUMP;
        if (ent->s.frame != FRAME_jump2)
            ent->s.frame = FRAME_jump1;
        client->anim_end = FRAME_jump2;
    } else if (run) {
        // running
        if (duck) {
            ent->s.frame = FRAME_crwalk1;
            client->anim_end = FRAME_crwalk6;
        } else {
            ent->s.frame = FRAME_run1;
            client->anim_end = FRAME_run6;
        }
    } else {
        // standing
        if (duck) {
            ent->s.frame = FRAME_crstnd01;
            client->anim_end = FRAME_crstnd19;
        } else {
            ent->s.frame = FRAME_stand01;
            client->anim_end = FRAME_stand40;
        }
    }
}

static void P_CalcBob(edict_t *ent)
{
    float   bobtime;

    if (!FRAMESYNC)
        return;

    xyspeed = sqrtf(ent->velocity[0] * ent->velocity[0] + ent->velocity[1] * ent->velocity[1]);

    if (xyspeed < 5) {
        bobmove = 0;
        current_client->bobtime = 0;    // start at beginning of cycle again
    } else if (ent->groundentity) { // so bobbing only cycles when on ground
        if (xyspeed > 210)
            bobmove = 0.25f;
        else if (xyspeed > 100)
            bobmove = 0.125f;
        else
            bobmove = 0.0625f;
    } else {
        bobmove = 0;
    }

    bobtime = (current_client->bobtime += bobmove);

    if (current_client->ps.pmove.pm_flags & PMF_DUCKED)
        bobtime *= 4;

    bobcycle = (int)bobtime;
    bobfracsin = fabs(sin(bobtime * M_PI));
}

void IntermissionEndServerFrame(edict_t *ent)
{
    ent->client->ps.stats[STAT_FLASHES] = 0;
    ent->client->ps.blend[3] = 0;
    ent->client->ps.fov = 90;
    VectorClear(ent->client->ps.kick_angles);
    G_SetStats(ent);
}


/*
=================
ClientEndServerFrame

Called for each player at the end of the server frame
and right after spawning
=================
*/
void ClientEndServerFrame(edict_t *ent)
{
    int     i;

    current_player = ent;
    current_client = ent->client;

    //
    // If the origin or velocity have changed since ClientThink(),
    // update the pmove values.  This will happen when the client
    // is pushed by a bmodel or kicked by an explosion.
    //
    // If it wasn't updated here, the view position would lag a frame
    // behind the body position when pushed -- "sinking into plats"
    //
    for (i = 0; i < 3; i++) {
        ent->client->ps.pmove.origin[i] = COORD2SHORT(ent->s.origin[i]);
        ent->client->ps.pmove.velocity[i] = COORD2SHORT(ent->velocity[i]);
    }

    AngleVectors(ent->client->v_angle, forward, right, up);

    // burn from lava, etc
    P_WorldEffects();

    //
    // set model angles from view angles so other things in
    // the world can tell which direction you are looking
    //
    if (ent->client->v_angle[PITCH] > 180)
        ent->s.angles[PITCH] = (-360 + ent->client->v_angle[PITCH]) / 3;
    else
        ent->s.angles[PITCH] = ent->client->v_angle[PITCH] / 3;
    ent->s.angles[YAW] = ent->client->v_angle[YAW];
    ent->s.angles[ROLL] = 0;
    ent->s.angles[ROLL] = P_CalcRoll(ent->s.angles, ent->velocity) * 4;

    //
    // calculate speed and cycle to be used for
    // all cyclic walking effects
    //
    P_CalcBob(ent);

    // detect hitting the floor
    P_FallingDamage(ent);

    // apply all the damage taken this frame
    P_DamageFeedback(ent);

    // determine the view offsets
    P_CalcViewOffset(ent);

    // determine the gun offsets
    P_CalcGunOffset(ent);

    // determine the full screen color blend
    // must be after viewoffset, so eye contents can be
    // accurately determined
    // FIXME: with client prediction, the contents
    // should be determined by the client
    P_CalcBlend(ent);

    P_SetEvent(ent);

    P_SetEffects(ent);

    P_SetSound(ent);

    P_SetFrame(ent);

    G_SetStats(ent);

    if (!FRAMESYNC)
        return;

    VectorCopy(ent->velocity, ent->client->oldvelocity);
    VectorCopy(ent->client->ps.viewangles, ent->client->oldviewangles);

    // clear weapon kicks
    VectorClear(ent->client->kick_origin);
    VectorClear(ent->client->kick_angles);
}
