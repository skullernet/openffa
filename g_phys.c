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
// g_phys.c

#include "g_local.h"

/*
pushmove objects do not obey gravity, and do not interact with each other or
trigger fields, but block normal movement and push normal objects when they
move.

onground is set for toss objects when they come to a complete rest. it is set
for steping or walking objects

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.
*/


/*
============
SV_TestEntityPosition

============
*/
static edict_t *SV_TestEntityPosition(edict_t *ent)
{
    trace_t trace;
    int     mask;

    if (ent->clipmask)
        mask = ent->clipmask;
    else
        mask = MASK_SOLID;
    trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin, ent, mask);

    if (trace.startsolid)
        return g_edicts;

    return NULL;
}


/*
================
SV_CheckVelocity
================
*/
static void SV_CheckVelocity(edict_t *ent)
{
    int     i;

//
// bound velocity
//
    for (i = 0; i < 3; i++) {
        if (ent->velocity[i] > sv_maxvelocity->value)
            ent->velocity[i] = sv_maxvelocity->value;
        else if (ent->velocity[i] < -sv_maxvelocity->value)
            ent->velocity[i] = -sv_maxvelocity->value;
    }
}

/*
=============
SV_RunThink

Runs thinking code for this frame if necessary
=============
*/
static bool SV_RunThink(edict_t *ent)
{
    int     thinkframe;

    thinkframe = ent->nextthink;
    if (thinkframe <= 0)
        return true;
    if (thinkframe > level.framenum)
        return true;

    ent->nextthink = 0;
    if (!ent->think)
        gi.error("NULL ent->think");
    ent->think(ent);

    return false;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
static void SV_Impact(edict_t *e1, trace_t *trace)
{
    edict_t     *e2;
//  cplane_t    backplane;

    e2 = trace->ent;

    if (e1->touch && e1->solid != SOLID_NOT)
        e1->touch(e1, e2, &trace->plane, trace->surface);

    if (e2->touch && e2->solid != SOLID_NOT)
        e2->touch(e2, e1, NULL, NULL);
}


/*
==================
ClipVelocity

Slide off of the impacting object
returns the blocked flags (1 = floor, 2 = step / wall)
==================
*/
#define STOP_EPSILON    0.1f

static int ClipVelocity(vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
    float   backoff;
    float   change;
    int     i, blocked;

    blocked = 0;
    if (normal[2] > 0)
        blocked |= 1;       // floor
    if (!normal[2])
        blocked |= 2;       // step

    backoff = DotProduct(in, normal) * overbounce;

    for (i = 0; i < 3; i++) {
        change = normal[i] * backoff;
        out[i] = in[i] - change;
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
            out[i] = 0;
    }

    return blocked;
}

/*
============
SV_AddGravity

============
*/
static void SV_AddGravity(edict_t *ent)
{
    ent->velocity[2] -= ent->gravity * sv_gravity->value * FRAMETIME;
}

/*
===============================================================================

PUSHMOVE

===============================================================================
*/

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
static trace_t SV_PushEntity(edict_t *ent, vec3_t push)
{
    trace_t trace;
    vec3_t  start;
    vec3_t  end;
    int     mask;

    VectorCopy(ent->s.origin, start);
    VectorAdd(start, push, end);

retry:
    if (ent->clipmask)
        mask = ent->clipmask;
    else
        mask = MASK_SOLID;

    trace = gi.trace(start, ent->mins, ent->maxs, end, ent, mask);

    VectorCopy(trace.endpos, ent->s.origin);
    gi.linkentity(ent);

    if (trace.fraction != 1.0f || (trace.allsolid && (int)g_bugs->value < 2)) {
        SV_Impact(ent, &trace);

        // if the pushed entity went away and the pusher is still there
        if (!trace.ent->inuse && ent->inuse) {
            // move the pusher back and try again
            VectorCopy(start, ent->s.origin);
            gi.linkentity(ent);
            goto retry;
        }
    }

    if (ent->inuse)
        G_TouchTriggers(ent);

    return trace;
}


typedef struct {
    edict_t *ent;
    vec3_t  origin;
    vec3_t  angles;
} pushed_t;

static pushed_t pushed[MAX_EDICTS], *pushed_p;

static edict_t  *obstacle;

/*
============
SV_Push

Objects need to be moved back on a failed push,
otherwise riders would continue to slide.
============
*/
static bool SV_Push(edict_t *pusher, vec3_t move, vec3_t amove)
{
    int         i, e;
    edict_t     *check, *block;
    vec3_t      mins, maxs;
    pushed_t    *p;
    vec3_t      org, org2, move2, forward, right, up;

    // clamp the move to 1/8 units, so the position will
    // be accurate for client side prediction
    for (i = 0; i < 3; i++) {
        float   temp;
        temp = move[i] * 8.0f;
        move[i] = 0.125f * Q_rint(temp);
    }

    // find the bounding box
    for (i = 0; i < 3; i++) {
        mins[i] = pusher->absmin[i] + move[i];
        maxs[i] = pusher->absmax[i] + move[i];
    }

// we need this for pushing things later
    VectorSubtract(vec3_origin, amove, org);
    AngleVectors(org, forward, right, up);

// save the pusher's original position
    pushed_p->ent = pusher;
    VectorCopy(pusher->s.origin, pushed_p->origin);
    VectorCopy(pusher->s.angles, pushed_p->angles);
    pushed_p++;

// move the pusher to it's final position
    VectorAdd(pusher->s.origin, move, pusher->s.origin);
    VectorAdd(pusher->s.angles, amove, pusher->s.angles);
    gi.linkentity(pusher);

// see if any solid entities are inside the final position
    check = g_edicts + 1;
    for (e = 1; e < globals.num_edicts; e++, check++) {
        if (!check->inuse)
            continue;
        if (check->movetype == MOVETYPE_PUSH
            || check->movetype == MOVETYPE_STOP
            || check->movetype == MOVETYPE_NONE
            || check->movetype == MOVETYPE_NOCLIP)
            continue;

        if (!check->area.prev)
            continue;       // not linked in anywhere

        // if the entity is standing on the pusher, it will definitely be moved
        if (check->groundentity != pusher) {
            // see if the ent needs to be tested
            if (check->absmin[0] >= maxs[0]
                || check->absmin[1] >= maxs[1]
                || check->absmin[2] >= maxs[2]
                || check->absmax[0] <= mins[0]
                || check->absmax[1] <= mins[1]
                || check->absmax[2] <= mins[2])
                continue;

            // see if the ent's bbox is inside the pusher's final position
            if (!SV_TestEntityPosition(check))
                continue;
        }

        if ((pusher->movetype == MOVETYPE_PUSH) || (check->groundentity == pusher)) {
            // move this entity
            pushed_p->ent = check;
            VectorCopy(check->s.origin, pushed_p->origin);
            VectorCopy(check->s.angles, pushed_p->angles);
            pushed_p++;

            // try moving the contacted entity
            VectorAdd(check->s.origin, move, check->s.origin);

            // figure movement due to the pusher's amove
            VectorSubtract(check->s.origin, pusher->s.origin, org);
            org2[0] = DotProduct(org, forward);
            org2[1] = -DotProduct(org, right);
            org2[2] = DotProduct(org, up);
            VectorSubtract(org2, org, move2);
            VectorAdd(check->s.origin, move2, check->s.origin);

            // may have pushed them off an edge
            if (check->groundentity != pusher)
                check->groundentity = NULL;

            block = SV_TestEntityPosition(check);
            if (!block) {
                // pushed ok
                gi.linkentity(check);
                // impact?
                continue;
            }

            // if it is ok to leave in the old position, do it
            // this is only relevent for riding entities, not pushed
            // FIXME: this doesn't acount for rotation
            VectorSubtract(check->s.origin, move, check->s.origin);
            block = SV_TestEntityPosition(check);
            if (!block) {
                pushed_p--;
                continue;
            }
        }

        // save off the obstacle so we can call the block function
        obstacle = check;

        // move back any entities we already moved
        // go backwards, so if the same entity was pushed
        // twice, it goes back to the original position
        for (i = (pushed_p - pushed) - 1; i >= 0; i--) {
            p = &pushed[i];
            VectorCopy(p->origin, p->ent->s.origin);
            VectorCopy(p->angles, p->ent->s.angles);
            gi.linkentity(p->ent);
        }
        return false;
    }

//FIXME: is there a better way to handle this?
    // see if anything we moved has touched a trigger
    for (i = (pushed_p - pushed) - 1; i >= 0; i--)
        G_TouchTriggers(pushed[i].ent);

    return true;
}

/*
================
SV_Physics_Pusher

Bmodel objects don't interact with each other, but
push all box objects
================
*/
static void SV_Physics_Pusher(edict_t *ent)
{
    vec3_t      move, amove;
    edict_t     *part, *mv;

    // if not a team captain, so movement will be handled elsewhere
    if (ent->flags & FL_TEAMSLAVE)
        return;

    // make sure all team slaves can move before commiting
    // any moves or calling any think functions
    // if the move is blocked, all moved objects will be backed out
//retry:
    pushed_p = pushed;
    for (part = ent; part; part = part->teamchain) {
        if (part->velocity[0] || part->velocity[1] || part->velocity[2] ||
            part->avelocity[0] || part->avelocity[1] || part->avelocity[2]) {
            // object is moving
            VectorScale(part->velocity, FRAMETIME, move);
            VectorScale(part->avelocity, FRAMETIME, amove);

            if (!SV_Push(part, move, amove))
                break;  // move was blocked
        }
    }
    if (pushed_p > &pushed[MAX_EDICTS])
        gi.error("pushed_p > &pushed[MAX_EDICTS], memory corrupted");

    if (part) {
        // the move failed, bump all nextthink times and back out moves
        for (mv = ent; mv; mv = mv->teamchain) {
            if (mv->nextthink > 0)
                mv->nextthink++;
        }

        // if the pusher has a "blocked" function, call it
        // otherwise, just stay in place until the obstacle is gone
        if (part->blocked)
            part->blocked(part, obstacle);
#if 0
        // if the pushed entity went away and the pusher is still there
        if (!obstacle->inuse && part->inuse)
            goto retry;
#endif
    } else {
        // the move succeeded, so call all think functions
        for (part = ent; part; part = part->teamchain) {
            SV_RunThink(part);
        }
    }
}

//==================================================================

/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
static void SV_Physics_None(edict_t *ent)
{
// regular thinking
    SV_RunThink(ent);
}

/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
static void SV_Physics_Noclip(edict_t *ent)
{
// regular thinking
    if (!SV_RunThink(ent))
        return;
    if (!ent->inuse)
        return;

    VectorMA(ent->s.angles, FRAMETIME, ent->avelocity, ent->s.angles);
    VectorMA(ent->s.origin, FRAMETIME, ent->velocity, ent->s.origin);

    gi.linkentity(ent);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
static void SV_Physics_Toss(edict_t *ent)
{
    trace_t     trace;
    vec3_t      move;
    float       backoff;
    edict_t     *slave;
    bool        wasinwater;
    bool        isinwater;
    vec3_t      old_origin;

// regular thinking
    SV_RunThink(ent);
    if (!ent->inuse)
        return;

    // if not a team captain, so movement will be handled elsewhere
    if (ent->flags & FL_TEAMSLAVE)
        return;

    if (ent->velocity[2] > 0)
        ent->groundentity = NULL;

// check for the groundentity going away
    if (ent->groundentity)
        if (!ent->groundentity->inuse)
            ent->groundentity = NULL;

// if onground, return without moving
    if (ent->groundentity)
        return;

    VectorCopy(ent->s.origin, old_origin);

    SV_CheckVelocity(ent);

// add gravity
    if (ent->movetype != MOVETYPE_FLY
        && ent->movetype != MOVETYPE_FLYMISSILE)
        SV_AddGravity(ent);

// move angles
    VectorMA(ent->s.angles, FRAMETIME, ent->avelocity, ent->s.angles);

// move origin
    VectorScale(ent->velocity, FRAMETIME, move);
    trace = SV_PushEntity(ent, move);
    if (!ent->inuse)
        return;

    if (trace.fraction < 1) {
        if (ent->movetype == MOVETYPE_BOUNCE)
            backoff = 1.5f;
        else
            backoff = 1;

        ClipVelocity(ent->velocity, trace.plane.normal, ent->velocity, backoff);

        // stop if on ground
        if (trace.plane.normal[2] > 0.7f) {
            if (ent->velocity[2] < 60 || ent->movetype != MOVETYPE_BOUNCE) {
                ent->groundentity = trace.ent;
                ent->groundentity_linkcount = trace.ent->linkcount;
                VectorCopy(vec3_origin, ent->velocity);
                VectorCopy(vec3_origin, ent->avelocity);
            }
        }

//      if (ent->touch)
//          ent->touch (ent, trace.ent, &trace.plane, trace.surface);
    }

// check for water transition
    wasinwater = (ent->watertype & MASK_WATER);
    ent->watertype = gi.pointcontents(ent->s.origin);
    isinwater = (ent->watertype & MASK_WATER);
    ent->waterlevel = isinwater;

    if (!wasinwater && isinwater)
        gi.positioned_sound(old_origin, g_edicts, CHAN_AUTO, gi.soundindex("misc/h2ohit1.wav"), 1, 1, 0);
    else if (wasinwater && !isinwater)
        gi.positioned_sound(ent->s.origin, g_edicts, CHAN_AUTO, gi.soundindex("misc/h2ohit1.wav"), 1, 1, 0);

// move teamslaves
    for (slave = ent->teamchain; slave; slave = slave->teamchain) {
        VectorCopy(ent->s.origin, slave->s.origin);
        gi.linkentity(slave);
    }
}

//============================================================================
/*
================
G_RunEntity

================
*/
void G_RunEntity(edict_t *ent)
{
    if (ent->prethink)
        ent->prethink(ent);

    switch (ent->movetype) {
    case MOVETYPE_PUSH:
    case MOVETYPE_STOP:
        SV_Physics_Pusher(ent);
        break;
    case MOVETYPE_NONE:
        SV_Physics_None(ent);
        break;
    case MOVETYPE_NOCLIP:
        SV_Physics_Noclip(ent);
        break;
    case MOVETYPE_TOSS:
    case MOVETYPE_BOUNCE:
    case MOVETYPE_FLY:
    case MOVETYPE_FLYMISSILE:
        SV_Physics_Toss(ent);
        break;
    default:
        gi.error("%s: bad movetype %i", __func__, ent->movetype);
    }
}
