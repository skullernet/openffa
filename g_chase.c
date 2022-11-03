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

static void SetChaseStats(gclient_t *client)
{
    edict_t *targ = client->chase_target;
    int playernum = (targ - g_edicts) - 1;

    memcpy(client->ps.stats, targ->client->ps.stats, sizeof(client->ps.stats));

    // layouts are independant in chasecam mode
    client->ps.stats[STAT_LAYOUTS] = 0;
    if (client->layout)
        client->ps.stats[STAT_LAYOUTS] |= 1;

    client->ps.stats[STAT_CHASE] = CS_PLAYERNAMES + playernum;
    client->ps.stats[STAT_SPECTATOR] = CS_SPECMODE;

    // STAT_FRAGS is no longer used for HUD,
    // but the server reports it in status responses
    client->ps.stats[STAT_FRAGS] = 0;

    // check view id settings
    if (client->pers.noviewid || client->layout == LAYOUT_MOTD) {
        client->ps.stats[STAT_VIEWID] = 0;
    } else if (targ->client->pers.noviewid) {
        client->ps.stats[STAT_VIEWID] = G_GetPlayerIdView(targ);
    }
}

static void UpdateChaseCamHack(gclient_t *client)
{
    vec3_t o, ownerv, goal;
    edict_t *ent = client->edict;
    edict_t *targ = client->chase_target;
    vec3_t forward, right;
    trace_t trace;
    vec3_t angles;

    VectorCopy(targ->s.origin, ownerv);

    ownerv[2] += targ->viewheight;

    VectorCopy(targ->client->v_angle, angles);
    if (angles[PITCH] > 56)
        angles[PITCH] = 56;
    AngleVectors(angles, forward, right, NULL);
    VectorNormalize(forward);
    VectorMA(ownerv, -30, forward, o);

    if (o[2] < targ->s.origin[2] + 20)
        o[2] = targ->s.origin[2] + 20;

    // jump animation lifts
    if (!targ->groundentity)
        o[2] += 16;

    trace = gi.trace(ownerv, vec3_origin, vec3_origin, o, targ, MASK_SOLID);

    VectorCopy(trace.endpos, goal);

    VectorMA(goal, 2, forward, goal);

    // pad for floors and ceilings
    VectorCopy(goal, o);
    o[2] += 6;
    trace = gi.trace(goal, vec3_origin, vec3_origin, o, targ, MASK_SOLID);
    if (trace.fraction < 1) {
        VectorCopy(trace.endpos, goal);
        goal[2] -= 6;
    }

    VectorCopy(goal, o);
    o[2] -= 6;
    trace = gi.trace(goal, vec3_origin, vec3_origin, o, targ, MASK_SOLID);
    if (trace.fraction < 1) {
        VectorCopy(trace.endpos, goal);
        goal[2] += 6;
    }

    if (targ->deadflag)
        client->ps.pmove.pm_type = PM_DEAD;
    else
        client->ps.pmove.pm_type = PM_FREEZE;

    client->ps.pmove.pm_flags |= PMF_NO_PREDICTION;

    VectorCopy(goal, ent->s.origin);
    VectorScale(goal, 8, client->ps.pmove.origin);

    if (targ->deadflag) {
        client->ps.viewangles[ROLL] = 40;
        client->ps.viewangles[PITCH] = -15;
        client->ps.viewangles[YAW] = targ->client->killer_yaw;
    } else {
        G_SetDeltaAngles(ent, targ->client->v_angle);
        VectorCopy(targ->client->v_angle, ent->client->ps.viewangles);
        VectorCopy(targ->client->v_angle, client->v_angle);
    }

    ent->viewheight = 0;
//  gi.linkentity(ent);
}

static void UpdateChaseCam(gclient_t *client)
{
    edict_t *ent = client->edict;
    edict_t *targ = client->chase_target;

    client->ps = targ->client->ps;
    if (client->pers.uf & UF_LOCALFOV) {
        client->ps.fov = client->pers.fov;
    }
    client->ps.pmove.pm_flags |= PMF_NO_PREDICTION;
    /*if (targ->deadflag) {
        client->ps.pmove.pm_type = PM_DEAD;
    } else*/ {
        client->ps.pmove.pm_type = PM_FREEZE;
    }

    VectorCopy(ent->client->ps.viewangles, ent->s.angles);
    VectorCopy(ent->client->ps.viewangles, ent->client->v_angle);
    VectorScale(ent->client->ps.pmove.origin, 0.125f, ent->s.origin);
    ent->viewheight = targ->viewheight;
}

void SetChaseTarget(edict_t *ent, edict_t *targ)
{
    int i;

    ent->client->chase_target = targ;

    // stop chasecam
    if (!targ) {
        ent->client->clientNum = (ent - g_edicts) - 1;
        ent->client->ps.pmove.pm_flags = 0;
        ent->client->ps.pmove.pm_type = PM_SPECTATOR;
        ent->client->ps.viewangles[ROLL] = 0;
        G_SetDeltaAngles(ent, ent->client->ps.viewangles);
        VectorCopy(ent->client->ps.viewangles, ent->s.angles);
        VectorCopy(ent->client->ps.viewangles, ent->client->v_angle);
        VectorScale(ent->client->ps.pmove.origin, 0.125f, ent->s.origin);
        ent->client->chase_mode = CHASE_NONE;
        ClientEndServerFrame(ent);
    } else {
        ent->client->clientNum = (targ - g_edicts) - 1;
        for (i = 0; i < PCS_TOTAL; i++) {
            G_PrivateString(ent, i, targ->client->level.strings[i]);
        }
        ChaseEndServerFrame(ent);
    }
}

void UpdateChaseTargets(chase_mode_t mode, edict_t *targ)
{
    edict_t *other;
    int i;

    for (i = 1; i <= game.maxclients; i++) {
        other = &g_edicts[i];
        if (!other->inuse) {
            continue;
        }
        if (other->client->pers.connected != CONN_SPECTATOR) {
            continue;
        }
        if (other->client->chase_mode != mode) {
            continue;
        }
        if (other->client->chase_target != targ) {
            SetChaseTarget(other, targ);
        }
    }
}

bool ChaseNext(edict_t *ent)
{
    int i;
    edict_t *e, *targ = ent->client->chase_target;

    if (!targ)
        return false;

    i = targ - g_edicts;
    do {
        i++;
        if (i > game.maxclients)
            i = 1;
        e = g_edicts + i;
        if (e == targ)
            return false;
    } while (!PlayerSpawned(e));

    SetChaseTarget(ent, e);
    return true;
}

bool ChasePrev(edict_t *ent)
{
    int i;
    edict_t *e, *targ = ent->client->chase_target;

    if (!targ)
        return false;

    i = targ - g_edicts;
    do {
        i--;
        if (i < 1)
            i = game.maxclients;
        e = g_edicts + i;
        if (e == targ)
            return false;
    } while (!PlayerSpawned(e));

    SetChaseTarget(ent, e);
    return true;
}

bool GetChaseTarget(edict_t *ent, chase_mode_t mode)
{
    gclient_t *ranks[MAX_CLIENTS];
    edict_t *other, *found = NULL;
    int i;

    if (mode == CHASE_LEADER) {
        if (G_CalcRanks(ranks)) {
            found = ranks[0]->edict;
        }
    } else for (i = 1; i <= game.maxclients; i++) {
        other = &g_edicts[i];
        if (!other->inuse) {
            continue;
        }
        if (!PlayerSpawned(other)) {
            continue;
        }
        if (mode == CHASE_NONE
            || (mode == CHASE_QUAD && other->client->quad_framenum > level.framenum)
            || (mode == CHASE_INVU && other->client->invincible_framenum > level.framenum)) {
            found = other;
            break;
        }
    }

    ent->client->chase_mode = mode;
    if (!found) {
        gi.cprintf(ent, PRINT_HIGH, "No players to chase.\n");
        return false;
    }

    SetChaseTarget(ent, found);
    return true;
}

void ChaseEndServerFrame(edict_t *ent)
{
    gclient_t *c = ent->client;

    if (!c->chase_target) {
        return;
    }

    // is our chase target gone?
    if (!PlayerSpawned(c->chase_target)) {
        if (!ChaseNext(ent)) {
            SetChaseTarget(ent, NULL);
            return;
        }
    }

    // camera
    if (game.serverFeatures & GMF_CLIENTNUM) {
        UpdateChaseCam(c);
    } else {
        UpdateChaseCamHack(c);
    }

    // stats
    SetChaseStats(c);
}
