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

void ClientUserinfoChanged(edict_t *ent, char *userinfo);

void SP_misc_teleporter_dest(edict_t *ent);

/*QUAKED info_player_start (1 0 0) (-16 -16 -24) (16 16 32)
The normal starting point for a level.
*/
void SP_info_player_start(edict_t *self)
{
}

/*QUAKED info_player_deathmatch (1 0 1) (-16 -16 -24) (16 16 32)
potential spawning position for deathmatch games
*/
void SP_info_player_deathmatch(edict_t *self)
{
    SP_misc_teleporter_dest(self);
}

/*QUAKED info_player_coop (1 0 1) (-16 -16 -24) (16 16 32)
potential spawning position for coop games
*/

void SP_info_player_coop(edict_t *self)
{
    G_FreeEdict(self);
}


/*QUAKED info_player_intermission (1 0 1) (-16 -16 -24) (16 16 32)
The deathmatch intermission point will be at one of these
Use 'angles' instead of 'angle', so you can set pitch or roll as well as yaw.  'pitch yaw roll'
*/
void SP_info_player_intermission(void)
{
}


//=======================================================================


void player_pain(edict_t *self, edict_t *other, float kick, int damage)
{
    // player pain is handled at the end of the frame in P_DamageFeedback
}


int G_UpdateRanks(void)
{
    gclient_t   *ranks[MAX_CLIENTS];
    gclient_t   *c;
    char buffer[MAX_QPATH];
    int i, j, total, topscore;

    total = G_CalcRanks(ranks);
    if (!total) {
        return 0;
    }

    // top player
    c = ranks[0];
    topscore = c->resp.score;
    j = total > 1 ? ranks[1]->resp.score : 0;
    Q_snprintf(buffer, sizeof(buffer), "  +%2d", topscore - j);
    G_PrivateString(c->edict, PCS_DELTA, buffer);
    Q_snprintf(buffer, sizeof(buffer), "1/%d", total);
    G_PrivateString(c->edict, PCS_RANK, va("%5s", buffer));

    UpdateChaseTargets(CHASE_LEADER, c->edict);

    // other players
    for (i = 1; i < total; i++) {
        c = ranks[i];
        Q_snprintf(buffer, sizeof(buffer), "  -%2d",
                   topscore - c->resp.score);
        for (j = 0; buffer[j]; j++) {
            buffer[j] |= 128;
        }
        G_PrivateString(c->edict, PCS_DELTA, buffer);
        Q_snprintf(buffer, sizeof(buffer), "%d/%d", i + 1, total);
        G_PrivateString(c->edict, PCS_RANK, va("%5s", buffer));
    }

    return total;
}

void G_ScoreChanged(edict_t *ent)
{
    char buffer[MAX_QPATH];
    int total;

    total = (int)fraglimit->value;
    if (total > 0) {
        Q_snprintf(buffer, sizeof(buffer), "%2d/%-2d",
                   ent->client->resp.score, total);
    } else {
        Q_snprintf(buffer, sizeof(buffer), "%5d",
                   ent->client->resp.score);
    }

    G_PrivateString(ent, PCS_FRAGS, buffer);
}

bool G_IsSameView(edict_t *ent, edict_t *other)
{
    if (ent == other) {
        return true;
    }
    if (ent->client->chase_target == other) {
        return true;
    }
    return false;
}

static const byte mod_to_frag[MOD_TOTAL] = {
    FRAG_UNKNOWN,
    FRAG_BLASTER,
    FRAG_SHOTGUN,
    FRAG_SUPERSHOTGUN,
    FRAG_MACHINEGUN,
    FRAG_CHAINGUN,
    FRAG_GRENADELAUNCHER,
    FRAG_GRENADELAUNCHER,
    FRAG_ROCKETLAUNCHER,
    FRAG_ROCKETLAUNCHER,
    FRAG_HYPERBLASTER,
    FRAG_RAILGUN,
    FRAG_BFG,
    FRAG_BFG,
    FRAG_BFG,
    FRAG_GRENADES,
    FRAG_GRENADES,
    FRAG_WATER,
    FRAG_SLIME,
    FRAG_LAVA,
    FRAG_CRUSH,
    FRAG_TELEPORT,
    FRAG_FALLING,
    FRAG_SUICIDE,
    FRAG_GRENADES
};

static void AccountItemKills(edict_t *ent)
{
    int index;

    if (ent->flags & FL_MEGAHEALTH) {
        ent->client->resp.items[ITEM_HEALTH].kills++;
    }
    if (ent->client->quad_framenum > level.framenum) {
        // FIXME: should account based on inflictor?
        ent->client->resp.items[ITEM_QUAD].kills++;
    }
    if (ent->client->invincible_framenum > level.framenum) {
        ent->client->resp.items[ITEM_INVULNERABILITY].kills++;
    }

    index = ArmorIndex(ent);
    if (index) {
        ent->client->resp.items[index].kills++;
    }

    index = PowerArmorIndex(ent);
    if (index) {
        ent->client->resp.items[index].kills++;
    }
}

static void ClientObituary(edict_t *self, edict_t *inflictor, edict_t *attacker)
{
    int         mod;
    frag_t      frag;
    char        *message, *name;
    char        *message2, *name2;
    bool        ff;
    edict_t     *ent;
    char        buffer[MAX_NETNAME];
    int         i;
    int         level;

    ff = meansOfDeath & MOD_FRIENDLY_FIRE;
    mod = meansOfDeath & ~MOD_FRIENDLY_FIRE;
    message = NULL;
    message2 = "";

    switch (mod) {
    case MOD_SUICIDE:
        message = "suicides";
        break;
    case MOD_FALLING:
        message = "cratered";
        break;
    case MOD_CRUSH:
        message = "was squished";
        break;
    case MOD_WATER:
        message = "sank like a rock";
        break;
    case MOD_SLIME:
        message = "melted";
        break;
    case MOD_LAVA:
        message = "does a back flip into the lava";
        break;
    case MOD_EXPLOSIVE:
    case MOD_BARREL:
        message = "blew up";
        break;
    case MOD_EXIT:
        message = "found a way out";
        break;
    case MOD_TARGET_LASER:
        message = "saw the light";
        break;
    case MOD_TARGET_BLASTER:
        message = "got blasted";
        break;
    case MOD_BOMB:
    case MOD_SPLASH:
    case MOD_TRIGGER_HURT:
        message = "was in the wrong place";
        break;
    }
    if (attacker == self) {
        switch (mod) {
        case MOD_HELD_GRENADE:
            message = "tried to put the pin back in";
            break;
        case MOD_HG_SPLASH:
        case MOD_G_SPLASH:
            switch (self->client->pers.gender) {
            case GENDER_FEMALE:
                message = "tripped on her own grenade";
                break;
            case GENDER_MALE:
                message = "tripped on his own grenade";
                break;
            default:
                message = "tripped on its own grenade";
                break;
            }
            break;
        case MOD_R_SPLASH:
            switch (self->client->pers.gender) {
            case GENDER_FEMALE:
                message = "blew herself up";
                break;
            case GENDER_MALE:
                message = "blew himself up";
                break;
            default:
                message = "blew itself up";
                break;
            }
            break;
        case MOD_BFG_BLAST:
            message = "should have used a smaller gun";
            break;
        default:
            switch (self->client->pers.gender) {
            case GENDER_FEMALE:
                message = "killed herself";
                break;
            case GENDER_MALE:
                message = "killed himself";
                break;
            default:
                message = "killed itself";
                break;
            }
            break;
        }
    }
    if (message) {
        for (i = 0, ent = &g_edicts[1]; i < game.maxclients; i++, ent++) {
            if (!ent->inuse) {
                continue;
            }
            if (ent->client->pers.connected <= CONN_CONNECTED) {
                continue;
            }
            name = self->client->pers.netname;
            level = PRINT_MEDIUM;
            if (G_IsSameView(ent, self)) {
                G_HighlightStr(buffer, name, MAX_NETNAME);
                name = buffer;
                level = PRINT_HIGH;
            }
            gi.cprintf(ent, level, "%s %s.\n", name, message);
        }
        if ((int)dedicated->value) {
            gi.dprintf("%s %s.\n", self->client->pers.netname, message);
        }
        frag = mod_to_frag[mod];
        self->client->resp.score--;
        self->client->resp.frags[frag].suicides++;
        self->enemy = NULL;
        G_ScoreChanged(self);
        G_UpdateRanks();
        return;
    }

    self->enemy = attacker;
    if (attacker && attacker->client) {
        switch (mod) {
        case MOD_BLASTER:
            message = "was blasted by";
            break;
        case MOD_SHOTGUN:
            message = "was gunned down by";
            break;
        case MOD_SSHOTGUN:
            message = "was blown away by";
            message2 = "'s super shotgun";
            break;
        case MOD_MACHINEGUN:
            message = "was machinegunned by";
            break;
        case MOD_CHAINGUN:
            message = "was cut in half by";
            message2 = "'s chaingun";
            break;
        case MOD_GRENADE:
            message = "was popped by";
            message2 = "'s grenade";
            break;
        case MOD_G_SPLASH:
            message = "was shredded by";
            message2 = "'s shrapnel";
            break;
        case MOD_ROCKET:
            message = "ate";
            message2 = "'s rocket";
            break;
        case MOD_R_SPLASH:
            message = "almost dodged";
            message2 = "'s rocket";
            break;
        case MOD_HYPERBLASTER:
            message = "was melted by";
            message2 = "'s hyperblaster";
            break;
        case MOD_RAILGUN:
            message = "was railed by";
            break;
        case MOD_BFG_LASER:
            message = "saw the pretty lights from";
            message2 = "'s BFG";
            break;
        case MOD_BFG_BLAST:
            message = "was disintegrated by";
            message2 = "'s BFG blast";
            break;
        case MOD_BFG_EFFECT:
            message = "couldn't hide from";
            message2 = "'s BFG";
            break;
        case MOD_HANDGRENADE:
            message = "caught";
            message2 = "'s handgrenade";
            break;
        case MOD_HG_SPLASH:
            message = "didn't see";
            message2 = "'s handgrenade";
            break;
        case MOD_HELD_GRENADE:
            message = "feels";
            message2 = "'s pain";
            break;
        case MOD_TELEFRAG:
            message = "tried to invade";
            message2 = "'s personal space";
            break;
        }
        if (message) {
            for (i = 0, ent = &g_edicts[1]; i < game.maxclients; i++, ent++) {
                if (!ent->inuse) {
                    continue;
                }
                if (ent->client->pers.connected <= CONN_CONNECTED) {
                    continue;
                }
                name = self->client->pers.netname;
                name2 = attacker->client->pers.netname;
                level = PRINT_MEDIUM;
                if (G_IsSameView(ent, attacker)) {
                    G_HighlightStr(buffer, name, MAX_NETNAME);
                    name = buffer;
                    level = PRINT_HIGH;
                } else if (G_IsSameView(ent, self)) {
                    G_HighlightStr(buffer, name2, MAX_NETNAME);
                    name2 = buffer;
                    level = PRINT_HIGH;
                }
                gi.cprintf(ent, level, "%s %s %s%s\n",
                           name, message, name2, message2);
            }
            if ((int)dedicated->value) {
                gi.dprintf("%s %s %s%s\n", self->client->pers.netname,
                           message, attacker->client->pers.netname, message2);
            }

            if (ff) {
                attacker->client->resp.score--;
            } else {
                frag = mod_to_frag[mod];
                attacker->client->resp.score++;
                attacker->client->resp.frags[frag].kills++;
                self->client->resp.deaths++;
                self->client->resp.frags[frag].deaths++;
                AccountItemKills(attacker);
            }
            G_ScoreChanged(attacker);
            G_UpdateRanks();
            return;
        }
    }

    gi.bprintf(PRINT_MEDIUM, "%s died.\n", self->client->pers.netname);
    frag = mod_to_frag[mod];
    self->client->resp.score--;
    self->client->resp.frags[frag].suicides++;

    G_ScoreChanged(self);
    G_UpdateRanks();
}

static int damaging;

void G_BeginDamage(void)
{
    damaging = 1;
}

// called from T_Damage only when target is a living player
// and inflictor is a real entity (not world)
void G_AccountDamage(edict_t *targ, edict_t *inflictor, edict_t *attacker, int points)
{
    frag_t frag;

    if (!damaging) {
        return;
    }

    frag = mod_to_frag[meansOfDeath & ~MOD_FRIENDLY_FIRE];
    if (frag < FRAG_BLASTER || frag > FRAG_BFG) {
        return; // only care about weapons
    }

    targ->client->resp.damage_recvd += points;
    if (targ == attacker) {
        return; // no credit for shooting yourself
    }

    attacker->client->resp.damage_given += points;

    // don't count multiple damage as multiple hits (but railgun still counts)
    if (damaging == 1 || frag == FRAG_RAILGUN) {
        attacker->client->resp.frags[frag].hits++;
    }

    damaging++;
}

void G_EndDamage(void)
{
    damaging = 0;
}


void Touch_Item(edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf);

static void TossClientWeapon(edict_t *self)
{
    gitem_t     *item;
    edict_t     *drop;
    bool        quad;
    float       spread;

    item = self->client->weapon;
    if (!self->client->inventory[self->client->ammo_index])
        item = NULL;
    if (item == INDEX_ITEM(ITEM_BLASTER))
        item = NULL;

    if (!DF(QUAD_DROP))
        quad = false;
    else
        quad = (self->client->quad_framenum > (level.framenum + 1 * HZ));

    if (item && quad)
        spread = 22.5f;
    else
        spread = 0.0f;

    if (item) {
        self->client->v_angle[YAW] -= spread;
        drop = Drop_Item(self, item);
        self->client->v_angle[YAW] += spread;
        drop->spawnflags = DROPPED_PLAYER_ITEM;
    }

    if (quad) {
        self->client->v_angle[YAW] += spread;
        drop = Drop_Item(self, INDEX_ITEM(ITEM_QUAD));
        self->client->v_angle[YAW] -= spread;
        drop->spawnflags |= DROPPED_PLAYER_ITEM;

        drop->touch = Touch_Item;
        drop->nextthink = self->client->quad_framenum;
        drop->think = G_FreeEdict;
    }
}


/*
==================
LookAtKiller
==================
*/
static void LookAtKiller(edict_t *self, edict_t *inflictor, edict_t *attacker)
{
    vec3_t      dir;

    if (attacker && attacker != world && attacker != self) {
        VectorSubtract(attacker->s.origin, self->s.origin, dir);
    } else if (inflictor && inflictor != world && inflictor != self) {
        VectorSubtract(inflictor->s.origin, self->s.origin, dir);
    } else {
        self->client->killer_yaw = self->s.angles[YAW];
        return;
    }

    if (dir[0])
        self->client->killer_yaw = RAD2DEG(atan2(dir[1], dir[0]));
    else {
        self->client->killer_yaw = 0;
        if (dir[1] > 0)
            self->client->killer_yaw = 90;
        else if (dir[1] < 0)
            self->client->killer_yaw = -90;
    }
    if (self->client->killer_yaw < 0)
        self->client->killer_yaw += 360;


}

/*
==================
player_die
==================
*/
void player_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
    int     n;

    VectorClear(self->avelocity);

    self->takedamage = DAMAGE_YES;
    self->movetype = MOVETYPE_TOSS;

    self->s.modelindex2 = 0;    // remove linked weapon model

    self->s.angles[0] = 0;
    self->s.angles[2] = 0;

    self->s.sound = 0;
    self->client->weapon_sound = 0;

    self->maxs[2] = -8;

//  self->solid = SOLID_NOT;
    self->svflags |= SVF_DEADMONSTER;

    if (!self->deadflag) {
        self->client->respawn_framenum = level.framenum + 1 * HZ;
        LookAtKiller(self, inflictor, attacker);
        self->client->ps.pmove.pm_type = PM_DEAD;
        ClientObituary(self, inflictor, attacker);
        TossClientWeapon(self);

        // show scores
        if (!self->client->layout) {
            self->client->layout = LAYOUT_SCORES;
            DeathmatchScoreboardMessage(self, false);
        }

        // clear inventory
        // this is kind of ugly, but it's how we want to handle keys in coop
        for (n = 0; n < MAX_ITEMS; n++) {
            self->client->inventory[n] = 0;
        }
    }

    // remove powerups
    self->client->quad_framenum = 0;
    self->client->invincible_framenum = 0;
    self->client->breather_framenum = 0;
    self->client->enviro_framenum = 0;
    self->flags &= ~FL_POWER_ARMOR;

    if (self->health < -40) {
        // gib
        gi.sound(self, CHAN_BODY, level.sounds.udeath, 1, ATTN_NORM, 0);
        for (n = 0; n < 4; n++)
            ThrowGib(self, level.models.meat, damage, GIB_ORGANIC);
        ThrowClientHead(self, damage);

        self->takedamage = DAMAGE_NO;
    } else {
        // normal death
        if (!self->deadflag) {
            static int i;
            int r;

            i = (i + 1) % 3;
            // start a death animation
            self->client->anim_priority = ANIM_DEATH;
            if (self->client->ps.pmove.pm_flags & PMF_DUCKED) {
                self->client->anim_start = FRAME_crdeath1;
                self->client->anim_end = FRAME_crdeath5;
            } else {
                switch (i) {
                case 0:
                    self->client->anim_start = FRAME_death101;
                    self->client->anim_end = FRAME_death106;
                    break;
                case 1:
                    self->client->anim_start = FRAME_death201;
                    self->client->anim_end = FRAME_death206;
                    break;
                case 2:
                    self->client->anim_start = FRAME_death301;
                    self->client->anim_end = FRAME_death308;
                    break;
                }
            }
            r = Q_rand() & 3;
            gi.sound(self, CHAN_VOICE, level.sounds.death[r], 1, ATTN_NORM, 0);
        }
    }

    // no normal pain sound
    self->pain_debounce_framenum = KEYFRAME(FRAMEDIV);

    self->deadflag = DEAD_DEAD;

    gi.linkentity(self);
}

/*
=======================================================================

  SelectSpawnPoint

=======================================================================
*/

static edict_t *SelectRandomDeathmatchSpawnPoint(void)
{
    return level.spawns[Q_rand_uniform(level.numspawns)];
}

/*
================
PlayersRangeFromSpot

Returns the distance to the nearest player from the given spot
================
*/
static float PlayersRangeFromSpot(edict_t *spot)
{
    edict_t *player;
    float   bestplayerdistance;
    vec3_t  v;
    int     n;
    float   playerdistance;

    bestplayerdistance = 9999999;

    for (n = 1; n <= game.maxclients; n++) {
        player = &g_edicts[n];

        if (!player->inuse)
            continue;
        if (!PlayerSpawned(player))
            continue;

        if (player->health <= 0)
            continue;

        VectorSubtract(spot->s.origin, player->s.origin, v);
        playerdistance = VectorLength(v);

        if (playerdistance < bestplayerdistance)
            bestplayerdistance = playerdistance;
    }

    return bestplayerdistance;
}

static edict_t *SelectRandomDeathmatchSpawnPointAvoidingTelefrag(void)
{
    edict_t *spawns[MAX_SPAWNS];
    edict_t *spot;
    float range;
    int i;

    for (i = 0; i < level.numspawns; i++) {
        spawns[i] = level.spawns[i];
    }

    G_ShuffleArray(spawns, level.numspawns);

    for (i = 0; i < level.numspawns; i++) {
        spot = spawns[i];

        range = PlayersRangeFromSpot(spot);
        if (range > 64) {
            return spot;
        }
    }

    // all spots were taken
    return SelectRandomDeathmatchSpawnPoint();
}

static edict_t *SelectRandomDeathmatchSpawnPointAvoidingTwoClosest(void)
{
    edict_t *spot, *spot1, *spot2;
    float   range, range1, range2;
    int     i;

    range1 = range2 = 99999;
    spot1 = spot2 = NULL;

    for (i = 0; i < level.numspawns; i++) {
        spot = level.spawns[i];

        range = PlayersRangeFromSpot(spot);
        if (range < range1) {
            range1 = range;
            spot1 = spot;
        }
    }

    for (i = 0; i < level.numspawns; i++) {
        spot = level.spawns[i];

        if (spot == spot1) {
            continue; // already recorded this one
        }

        range = PlayersRangeFromSpot(spot);
        if (range < range2) {
            range2 = range;
            spot2 = spot;
        }
    }

    do {
        spot = SelectRandomDeathmatchSpawnPoint();
    } while (spot == spot1 || spot == spot2);

    return spot;
}

/*
================
SelectRandomDeathmatchSpawnPointAvoidingTwoClosestBugged

go to a random point, but NOT the two points closest
to other players

original bugged version: if points are in decreasing
range order, spot2 will never be set
================
*/
static edict_t *SelectRandomDeathmatchSpawnPointAvoidingTwoClosestBugged(void)
{
    edict_t *spot, *spot1, *spot2;
    float   range, range1, range2;
    int     i;

    range1 = range2 = 99999;
    spot1 = spot2 = NULL;

    for (i = 0; i < level.numspawns; i++) {
        spot = level.spawns[i];

        range = PlayersRangeFromSpot(spot);
        if (range < range1) {
            range1 = range;
            spot1 = spot;
        } else if (range < range2) {
            range2 = range;
            spot2 = spot;
        }
    }

    do {
        spot = SelectRandomDeathmatchSpawnPoint();
    } while (spot == spot1 || spot == spot2);

    return spot;
}

/*
================
SelectFarthestDeathmatchSpawnPoint

================
*/
static edict_t *SelectFarthestDeathmatchSpawnPoint(void)
{
    edict_t *bestspot;
    float   bestdistance, bestplayerdistance;
    edict_t *spot;
    int     i;

    bestspot = NULL;
    bestdistance = 0;
    for (i = 0; i < level.numspawns; i++) {
        spot = level.spawns[i];
        bestplayerdistance = PlayersRangeFromSpot(spot);
        if (bestplayerdistance > bestdistance) {
            bestspot = spot;
            bestdistance = bestplayerdistance;
        }
    }

    if (bestspot) {
        return bestspot;
    }

    // if there is a player just spawned on each and every start spot
    // we have no choice to turn one into a telefrag meltdown
    return level.spawns[0];
}

static edict_t *SelectDeathmatchSpawnPoint(void)
{
    // in the first 5 seconds of a level start,
    // avoid telefrags above all other conditions
    if (level.framenum < 5 * HZ) {
        return SelectRandomDeathmatchSpawnPointAvoidingTelefrag();
    }

    // spawn farthest overrides g_spawn_mode
    if (DF(SPAWN_FARTHEST)) {
        return SelectFarthestDeathmatchSpawnPoint();
    }

    if (level.numspawns > 2) {
        if ((int)g_spawn_mode->value == 0) {
            return SelectRandomDeathmatchSpawnPointAvoidingTwoClosestBugged();
        }
        if ((int)g_spawn_mode->value == 1) {
            return SelectRandomDeathmatchSpawnPointAvoidingTwoClosest();
        }
    }
    return SelectRandomDeathmatchSpawnPoint();
}

/*
===========
SelectSpawnPoint

Chooses a player start, deathmatch start, coop start, etc
============
*/
static void SelectSpawnPoint(edict_t *ent, vec3_t origin, vec3_t angles)
{
    edict_t *spot = NULL;

    if (level.numspawns && PlayerSpawned(ent)) {
        spot = SelectDeathmatchSpawnPoint();
    }

    // find a single player start spot
    if (!spot) {
        while ((spot = G_Find(spot, FOFS(classname), "info_player_start")) != NULL) {
            if (!spot->targetname)
                break;
        }

        if (!spot) {
            // there wasn't a spawnpoint without a target, so use any
            spot = G_Find(spot, FOFS(classname), "info_player_start");
            if (!spot) {
                gi.dprintf("Couldn't find spawn point!\n");
                spot = world;
            }
        }
    }

    VectorCopy(spot->s.origin, origin);
    VectorCopy(spot->s.angles, angles);
}

//======================================================================


void InitBodyQue(void)
{
    int     i;
    edict_t *ent;

    level.body_que = 0;
    for (i = 0; i < BODY_QUEUE_SIZE; i++) {
        ent = G_Spawn();
        ent->classname = "bodyque";
    }
}

static void body_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
    int n;

    if (self->health < -40) {
        gi.sound(self, CHAN_BODY, level.sounds.udeath, 1, ATTN_NORM, 0);
        for (n = 0; n < 4; n++)
            ThrowGib(self, level.models.meat, damage, GIB_ORGANIC);
        self->s.origin[2] -= 48;
        ThrowClientHead(self, damage);
        self->takedamage = DAMAGE_NO;
    }
}

static void CopyToBodyQue(edict_t *ent)
{
    edict_t     *body;

    gi.unlinkentity(ent);

    // grab a body que and cycle to the next one
    body = &g_edicts[game.maxclients + level.body_que + 1];
    level.body_que = (level.body_que + 1) % BODY_QUEUE_SIZE;

    // send an effect on the removed body
    if (body->s.modelindex) {
        gi.WriteByte(svc_temp_entity);
        gi.WriteByte(TE_BLOOD);
        gi.WritePosition(body->s.origin);
        gi.WriteDir(vec3_origin);
        gi.multicast(body->s.origin, MULTICAST_PVS);
    }

    gi.unlinkentity(body);

    body->s.number = body - g_edicts;
    VectorCopy(ent->s.origin, body->s.origin);
    VectorCopy(ent->s.origin, body->s.old_origin);
    VectorCopy(ent->s.origin, body->old_origin);
    VectorCopy(ent->s.angles, body->s.angles);
    body->s.modelindex = ent->s.modelindex;
    body->s.frame = ent->s.frame;
    body->s.skinnum = ent->s.skinnum;
    body->s.event = EV_OTHER_TELEPORT;

    // start with dead player frame
    if (ent->s.modelindex == 255 && ent->client->anim_priority == ANIM_DEATH)
        body->s.frame = ent->client->anim_end;

    body->svflags = ent->svflags;
    VectorCopy(ent->mins, body->mins);
    VectorCopy(ent->maxs, body->maxs);
    VectorCopy(ent->absmin, body->absmin);
    VectorCopy(ent->absmax, body->absmax);
    VectorCopy(ent->size, body->size);
    VectorCopy(ent->velocity, body->velocity);
    VectorCopy(ent->avelocity, body->avelocity);
    body->solid = ent->solid;
    body->clipmask = ent->clipmask;
    body->owner = ent->owner;
    body->movetype = ent->movetype;
    body->groundentity = ent->groundentity;

    body->die = body_die;
    body->takedamage = DAMAGE_YES;

    gi.linkentity(body);
}


void respawn(edict_t *self)
{
    // spectator's don't leave bodies
    if (self->movetype != MOVETYPE_NOCLIP)
        CopyToBodyQue(self);
    PutClientInServer(self);

    // add a teleportation effect
    self->s.event = EV_PLAYER_TELEPORT;

    // hold in place briefly
    self->client->ps.pmove.pm_flags = PMF_TIME_TELEPORT;
    self->client->ps.pmove.pm_time = 14;

    self->client->respawn_framenum = level.framenum;
}

/*
 * only called when pers.spectator changes
 */
void spectator_respawn(edict_t *ent, int connected)
{
    int total;

    ent->client->pers.connected = connected;
    total = G_UpdateRanks();

    // notify others
    if (connected == CONN_SPAWNED) {
        gi.bprintf(PRINT_HIGH, "%s entered the game (%d player%s)\n",
                   ent->client->pers.netname, total, total == 1 ? "" : "s");

        PutClientInServer(ent);

        G_ScoreChanged(ent);

        // add a teleportation effect
        ent->s.event = EV_PLAYER_TELEPORT;

        // hold in place briefly
        ent->client->ps.pmove.pm_flags = PMF_TIME_TELEPORT;
        ent->client->ps.pmove.pm_time = 14;
    } else {
        gi.bprintf(PRINT_HIGH, "%s moved to the sidelines (%d player%s)\n",
                   ent->client->pers.netname, total, total == 1 ? "" : "s");

        TossClientWeapon(ent);

        if (g_db)
            g_db->log(ent->client);

        // clear client on respawn
        memset(&ent->client->resp, 0, sizeof(ent->client->resp));

        // send effect on removed player
        gi.WriteByte(svc_temp_entity);
        gi.WriteByte(TE_BLOOD);
        gi.WritePosition(ent->s.origin);
        gi.WriteDir(vec3_origin);
        gi.multicast(ent->s.origin, MULTICAST_PVS);

        PutClientInServer(ent);
    }

    ent->client->resp.enter_framenum = level.framenum;
    ent->client->resp.activity_framenum = level.framenum;
    ent->client->respawn_framenum = level.framenum;

    G_CheckVote();
    G_CheckMatchStart();
}

//==============================================================

void G_SetDeltaAngles(edict_t *ent, vec3_t angles)
{
    int i;

    for (i = 0; i < 3; i++) {
        ent->client->ps.pmove.delta_angles[i] = ANGLE2SHORT(
                angles[i] - ent->client->level.cmd.angles[i]);
    }
}

static void InitClientInventory(edict_t *ent)
{
    gclient_t *client = ent->client;
    int weapon_initial = (int)g_weapon_initial->value;
    int weapon_have = (int)g_weapon_have->value | weapon_initial;
    int i;
    gitem_t *it;

    client->inventory[ITEM_BLASTER] = 1;

    for (i = WEAP_SHOTGUN; i < WEAP_TOTAL; i++) {
        if (weapon_have & (1 << (i - WEAP_SHOTGUN))) {
            it = FindItemByWeaponModel(i);
            client->inventory[ITEM_INDEX(it)] = 1;
        }
    }

    client->selected_item = ITEM_BLASTER;
    client->weapon = INDEX_ITEM(ITEM_BLASTER);

    for (i = WEAP_SHOTGUN; i < WEAP_TOTAL; i++) {
        if (weapon_initial & (1 << (i - WEAP_SHOTGUN))) {
            it = FindItemByWeaponModel(i);
            client->selected_item = ITEM_INDEX(it);
            client->weapon = it;
            break;
        }
    }

    client->max_shells      = G_ClampCvar(g_max_shells,   1, 999);
    client->max_bullets     = G_ClampCvar(g_max_bullets,  1, 999);
    client->max_cells       = G_ClampCvar(g_max_cells,    1, 999);
    client->max_grenades    = G_ClampCvar(g_max_grenades, 1, 999);
    client->max_rockets     = G_ClampCvar(g_max_rockets,  1, 999);
    client->max_slugs       = G_ClampCvar(g_max_slugs,    1, 999);

    client->inventory[ITEM_SHELLS]   = G_ClampCvar(g_start_shells,   0, 999);
    client->inventory[ITEM_BULLETS]  = G_ClampCvar(g_start_bullets,  0, 999);
    client->inventory[ITEM_CELLS]    = G_ClampCvar(g_start_cells,    0, 999);
    client->inventory[ITEM_GRENADES] = G_ClampCvar(g_start_grenades, 0, 999);
    client->inventory[ITEM_ROCKETS]  = G_ClampCvar(g_start_rockets,  0, 999);
    client->inventory[ITEM_SLUGS]    = G_ClampCvar(g_start_slugs,    0, 999);

    it = FindItemByArmorType(ARMOR_JACKET + G_ClampCvar(g_start_armortype, 0, 2));
    client->inventory[ITEM_INDEX(it)] = G_ClampCvar(g_start_armor, 0, ((gitem_armor_t *)it->info)->max_count);

    ent->max_health         = G_ClampCvar(g_max_health,   1, 999);
    ent->health             = G_ClampCvar(g_start_health, 1, 999);
}

/*
===========
PutClientInServer

Called when a player connects to a server or respawns in
a deathmatch.
============
*/
void PutClientInServer(edict_t *ent)
{
    int     index;
    vec3_t  spawn_origin, spawn_angles;
    gclient_t   *client;
    client_persistant_t pers;
    client_respawn_t    resp;
    client_level_t      lvl;
    vec3_t temp, temp2;
    trace_t tr;

    index = ent - g_edicts - 1;
    client = ent->client;

    // find a spawn point
    // do it before setting health back up, so farthest
    // ranging doesn't count this client
    if (client->pers.connected == CONN_SPECTATOR) {
        VectorScale(client->ps.pmove.origin, 0.125f, spawn_origin);
        VectorCopy(client->ps.viewangles, spawn_angles);
    } else {
        ent->health = 0;
        SelectSpawnPoint(ent, spawn_origin, spawn_angles);
    }

    PMenu_Close(ent);

    // deathmatch wipes most client data every spawn
    resp = client->resp;
    pers = client->pers;
    lvl = client->level;

    // clear everything but the persistant data
    memset(client, 0, sizeof(*client));
    client->pers = pers;
    client->resp = resp;
    client->level = lvl;
    client->edict = ent;
    client->clientNum = index;

    InitClientInventory(ent);

    // clear entity values
    ent->groundentity = NULL;
    ent->client = &game.clients[index];
    ent->takedamage = DAMAGE_AIM;
    ent->movetype = MOVETYPE_WALK;
    ent->viewheight = 22;
    ent->inuse = true;
    ent->classname = "player";
    ent->mass = 200;
    ent->solid = SOLID_BBOX;
    ent->deadflag = DEAD_NO;
    ent->air_finished_framenum = level.framenum + 12 * HZ;
    ent->clipmask = MASK_PLAYERSOLID;
    ent->model = "players/male/tris.md2";
    ent->pain = player_pain;
    ent->die = player_die;
    ent->waterlevel = 0;
    ent->watertype = 0;
    ent->flags &= ~(FL_NO_KNOCKBACK | FL_MEGAHEALTH);
    ent->svflags &= ~(SVF_DEADMONSTER | SVF_NOCLIENT);

    VectorSet(ent->mins, -16, -16, -24);
    VectorSet(ent->maxs, 16, 16, 32);
    VectorClear(ent->velocity);

    // clear playerstate values
    client->ps.fov = client->pers.fov;
    client->ps.gunindex = gi.modelindex(client->weapon->view_model);

    // clear entity state values
    ent->s.sound = 0;
    ent->s.effects = 0;
    ent->s.renderfx = 0;
    ent->s.modelindex = 255;        // will use the skin specified model
    ent->s.modelindex2 = 255;       // custom gun model
    // sknum is player num and weapon number
    // weapon number will be added in changeweapon
    ent->s.skinnum = ent - g_edicts - 1;
    ent->s.frame = 0;

    // try to properly clip to the floor / spawn
    VectorCopy(spawn_origin, temp);
    VectorCopy(spawn_origin, temp2);
    temp[2] -= 64;
    temp2[2] += 16;
    tr = gi.trace(temp2, ent->mins, ent->maxs, temp, ent, MASK_PLAYERSOLID);
    if (!tr.allsolid && !tr.startsolid) {
        VectorCopy(tr.endpos, ent->s.origin);
        ent->groundentity = tr.ent;
    } else {
        VectorCopy(spawn_origin, ent->s.origin);
        ent->s.origin[2] += 10; // make sure off ground
    }

    VectorCopy(ent->s.origin, ent->s.old_origin);
    VectorCopy(ent->s.origin, ent->old_origin);

    client->ps.pmove.origin[0] = COORD2SHORT(ent->s.origin[0]);
    client->ps.pmove.origin[1] = COORD2SHORT(ent->s.origin[1]);
    client->ps.pmove.origin[2] = COORD2SHORT(ent->s.origin[2]);

    spawn_angles[ROLL] = 0;

    // set the delta angle
    G_SetDeltaAngles(ent, spawn_angles);

    VectorCopy(spawn_angles, ent->s.angles);
    VectorCopy(spawn_angles, client->ps.viewangles);
    VectorCopy(spawn_angles, client->v_angle);

    // spawn a spectator
    if (client->pers.connected != CONN_SPAWNED || level.intermission_framenum) {
        ent->movetype = MOVETYPE_NOCLIP;
        ent->solid = SOLID_NOT;
        ent->svflags |= SVF_NOCLIENT;
        client->ps.gunindex = 0;
        client->ps.pmove.pm_type = PM_SPECTATOR;
        //gi.linkentity (ent);
        return;
    }

    // we must link before killbox since it uses absmin/absmax
    gi.linkentity(ent);

    if (!G_KillBox(ent)) {
        // could't spawn in?
    }

    // force the current weapon up
    client->newweapon = client->weapon;
    ChangeWeapon(ent);

    if (g_protection_time->value > 0) {
        client->invincible_framenum = level.framenum +
                                      g_protection_time->value * HZ;
    }
}

void G_WriteTime(int remaining)
{
    char buffer[16];
    int sec = remaining % 60;
    int min = remaining / 60;
    int i;

    sprintf(buffer, "%2d:%02d", min, sec);
    if (remaining <= 30 && (sec & 1) == 0) {
        for (i = 0; buffer[i]; i++) {
            buffer[i] |= 128;
        }
    }

    gi.WriteByte(svc_configstring);
    gi.WriteShort(CS_TIME);
    gi.WriteString(buffer);
}


/*
===========
ClientBegin

called when a client has finished connecting, and is ready
to be placed into the game.  This will happen every level load.
============
*/
void ClientBegin(edict_t *ent)
{
    ent->client = game.clients + (ent - g_edicts - 1);
    ent->client->edict = ent;

    G_InitEdict(ent);

    memset(&ent->client->resp, 0, sizeof(ent->client->resp));

    ent->client->resp.enter_framenum = level.framenum;
    ent->client->pers.connected = CONN_PREGAME;

    // locate ent at a spawn point
    PutClientInServer(ent);

    if (ent->client->pers.mvdspec || level.intermission_framenum || (int)g_warmup->value)
        ent->client->pers.connected = CONN_SPECTATOR;

    if (level.intermission_framenum) {
        MoveClientToIntermission(ent);
    } else {
        int remaining = 0;

        if (timelimit->value > 0 && level.match_state == MS_PLAYING) {
            remaining = timelimit->value * 60 - level.time;
        } else if (level.match_state == MS_COUNTDOWN) {
            int delta = level.framenum - level.countdown_framenum;
            remaining = (int)g_countdown_time->value - delta / HZ;
        }

        if (remaining > 0) {
            G_WriteTime(remaining);
            gi.unicast(ent, true);
        }
    }

    if (ent->client->level.first_time) {
        gi.bprintf(PRINT_HIGH, "%s connected\n", ent->client->pers.netname);

        // track map stats
        level.players_in++;

        // send login effect only to this client
        gi.WriteByte(svc_muzzleflash);
        gi.WriteShort(ent - g_edicts);
        gi.WriteByte(MZ_LOGIN);
        gi.unicast(ent, false);

        ent->client->level.first_time = false;
    }

    if (!ent->client->pers.mvdspec && !level.intermission_framenum && (int)g_warmup->value) {
        Cmd_Menu_f(ent);
        ent->client->resp.enter_framenum -= 5 * HZ; // hack for G_SpecRateLimited()
    }

    // make sure all view stuff is valid
    ClientEndServerFrame(ent);
}

static skin_entry_t *find_skin(skin_entry_t *head, const char *name)
{
    skin_entry_t *e;

    for (e = head; e; e = e->next) {
        if (!Q_stricmp(e->name, name)) {
            return e;
        }
    }

    return head; // use default
}

static bool parse_skin(char *out, const char *in)
{
    char *p;
    skin_entry_t *m, *s;
    size_t len;

    // copy it off
    len = Q_strlcpy(out, in, MAX_SKINNAME);
    if (len >= MAX_SKINNAME) {
        goto bad;
    }

    // isolate the model name
    p = strchr(out, '/');
    if (!p) {
        goto bad;
    }
    *p = 0;

    // validate the model part
    if (!COM_IsPath(out)) {
        goto bad;
    }

    if (!game.skins) {
        // validate the skin part
        if (!COM_IsPath(p + 1)) {
            goto bad;
        }

        // fix slash and return original skin
        *p = '/';
        return false;
    }

    m = find_skin(game.skins, out);
    if (!m->down) {
        // validate the skin part
        if (!COM_IsPath(p + 1)) {
            goto bad;
        }

        // empty directory = wildcard
        len = Q_concat(out, MAX_SKINNAME, m->name, "/", p + 1);
    } else {
        s = find_skin(m->down, p + 1);
        len = Q_concat(out, MAX_SKINNAME, m->name, "/", s->name);
    }

    if (len >= MAX_SKINNAME) {
        goto bad;
    }
    return true;

bad:
    strcpy(out, "male/grunt");
    return true;
}

static bool forbid_name_change(edict_t *ent)
{
    if (!ent->client->pers.skin[0]) {
        return false; // allow the very first one
    }

    return G_FloodProtect(ent, &ent->client->level.info_flood,
                          "change name or skin", (int)flood_infos->value,
                          flood_perinfo->value, flood_infodelay->value);
}

/*
===========
ClientUserInfoChanged

called whenever the player updates a userinfo variable.

The game can override any of the settings in place
(forcing skins or names, etc) before copying it off.
============
*/
void ClientUserinfoChanged(edict_t *ent, char *userinfo)
{
    char    *s;
    int     playernum;
    gclient_t *client = ent->client;
    char    name[MAX_NETNAME], skin[MAX_SKINNAME];
    char    playerskin[MAX_QPATH];
    bool    changed;

    // check for malformed or illegal info strings
    if (!Info_Validate(userinfo)) {
        strcpy(userinfo, "\\name\\badinfo\\skin\\male/grunt");
    }

    // set name
    s = Info_ValueForKey(userinfo, "name");
    Q_strlcpy(name, s, sizeof(name));
    if (COM_IsWhite(name)) {
        strcpy(name, "unnamed");
    }

    // set skin
    s = Info_ValueForKey(userinfo, "skin");
    parse_skin(skin, s);

    changed = strcmp(name, client->pers.netname) ||
              strcmp(skin, client->pers.skin);

    if (!client->pers.mvdspec && changed) {
        if (forbid_name_change(ent)) {
            Info_SetValueForKey(userinfo, "name", client->pers.netname);
            //Info_SetValueForKey( userinfo, "skin", client->pers.skin );
            G_StuffText(ent, va("set name \"%s\"\n", client->pers.netname));
        } else {
            // combine name and skin into a configstring
            Q_concat(playerskin, sizeof(playerskin), name, "\\", skin);
            playernum = (ent - g_edicts) - 1;
            gi.configstring(CS_PLAYERSKINS + playernum, playerskin);
            gi.configstring(CS_PLAYERNAMES + playernum, name);
            strcpy(client->pers.skin, skin);
            strcpy(client->pers.netname, name);
        }
    }

    // set fov
    if (DF(FIXED_FOV)) {
        client->pers.fov = 90;
    } else {
        client->pers.fov = atoi(Info_ValueForKey(userinfo, "fov"));
        if (client->pers.fov < 1)
            client->pers.fov = 90;
        else if (client->pers.fov > 160)
            client->pers.fov = 160;
    }

    client->ps.fov = client->pers.fov;

    // handedness
    s = Info_ValueForKey(userinfo, "hand");
    client->pers.hand = *s ? atoi(s) : 0;

    // gender
    s = Info_ValueForKey(userinfo, "gender");
    if (s[0] == 'f' || s[0] == 'F') {
        client->pers.gender = GENDER_FEMALE;
    } else if (s[0] == 'm' || s[0] == 'M') {
        client->pers.gender = GENDER_MALE;
    } else {
        client->pers.gender = GENDER_NEUTRAL;
    }

    // flags
    s = Info_ValueForKey(userinfo, "uf");
    client->pers.uf = *s ? atoi(s) : UF_LOCALFOV;
}


/*
===========
ClientConnect

Called when a player begins connecting to the server.
The game can refuse entrance to a client by returning false.
If the client is allowed, the connection process will continue
and eventually get to ClientBegin()
Changing levels will NOT cause this to be called again, but
loadgames will.
============
*/
qboolean ClientConnect(edict_t *ent, char *userinfo)
{
    char *s, *userinfo_base = userinfo;
    ipaction_t action;

    // skip to extra userinfo if supported
    if (game.serverFeatures & GMF_EXTRA_USERINFO) {
        userinfo += strlen(userinfo) + 1;
    }

    if (!Info_Validate(userinfo)) {
        return false;
    }

    s = Info_ValueForKey(userinfo, "ip");
    action = G_CheckFilters(s);
    if (action == IPA_BAN) {
        strcpy(userinfo_base, "\\rejmsg\\You are banned from this server.");
        return false;
    }

    // they can connect
    ent->client = game.clients + (ent - g_edicts - 1);

    memset(ent->client, 0, sizeof(gclient_t));
    ent->client->edict = ent;
    ent->client->pers.connected = CONN_CONNECTED;
    ent->client->level.first_time = true;
    ent->client->pers.loopback = !strcmp(s, "loopback");
    ent->client->pers.muted = action == IPA_MUTE;

    // save ip
    Q_strlcpy(ent->client->pers.ip, s, sizeof(ent->client->pers.ip));

    if (game.serverFeatures & GMF_MVDSPEC) {
        s = Info_ValueForKey(userinfo, "mvdspec");
        if (*s) {
            ent->client->pers.mvdspec = true;
            ent->client->level.first_time = false;
        }
    }

    if (game.serverFeatures & GMF_EXTRA_USERINFO) {
        int major = atoi(Info_ValueForKey(userinfo, "major"));
        int minor = atoi(Info_ValueForKey(userinfo, "minor"));
        int zlib  = atoi(Info_ValueForKey(userinfo, "zlib"));
        ent->client->pers.extended_layout = major == 36 && minor >= 1020 && zlib > 0;
    }

    return true;
}

/*
===========
ClientDisconnect

Called when a player drops from the server.
Will not be called between levels.
============
*/
void ClientDisconnect(edict_t *ent)
{
    int     total;
    conn_t  connected;

    if (!ent->client)
        return;

    connected = ent->client->pers.connected;
    ent->client->pers.connected = CONN_DISCONNECTED;
    ent->client->ps.stats[STAT_FRAGS] = 0;

    if (g_db && connected == CONN_SPAWNED) {
        g_db->log(ent->client);
    }

    if (connected == CONN_SPAWNED && !level.intermission_framenum) {
        // send effect
        gi.WriteByte(svc_muzzleflash);
        gi.WriteShort(ent - g_edicts);
        gi.WriteByte(MZ_LOGOUT);
        gi.multicast(ent->s.origin, MULTICAST_PVS);

        // update ranks
        total = G_UpdateRanks();
        gi.bprintf(PRINT_HIGH, "%s disconnected (%d player%s)\n",
                   ent->client->pers.netname, total, total == 1 ? "" : "s");
    } else if (connected > CONN_CONNECTED) {
        gi.bprintf(PRINT_HIGH, "%s disconnected\n",
                   ent->client->pers.netname);
    }

    PMenu_Close(ent);

    if (connected > CONN_CONNECTED) {
        // track map stats
        level.players_out++;
    }

    gi.unlinkentity(ent);

    ent->s.modelindex = 0;
    ent->s.modelindex2 = 0;
    ent->s.sound = 0;
    ent->s.event = 0;
    ent->s.effects = 0;
    ent->s.renderfx = 0;
    ent->s.solid = 0;
    ent->solid = SOLID_NOT;
    ent->inuse = false;
    ent->classname = "disconnected";
    ent->svflags = SVF_NOCLIENT;

    // FIXME: don't break skins on corpses, etc
    //playernum = ent-g_edicts-1;
    //gi.configstring (CS_PLAYERSKINS+playernum, "");

    // check vote after this client has been completely disconnected
    G_CheckVote();
    G_CheckMatchStart();
}


//==============================================================


// pmove doesn't need to know about passent and contentmask
static edict_t  *pm_passent;
static int      pm_mask;

static trace_t q_gameabi PM_trace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end)
{
    return gi.trace(start, mins, maxs, end, pm_passent, pm_mask);
}

/*
==============
G_TouchProjectiles

An ugly hack that runs a trace against any FL_NOCLIP_PROJECTILE entities
for clipping purposes against players. This assumes that the ent
will be freed on touch or bad things will happen.
==============
*/
static void G_TouchProjectiles(edict_t *ent, vec3_t start)
{
    edict_t *ignore;
    trace_t tr;
    int i;

    ignore = ent;
    for (i = 0; i < 10; i++) {
        tr = gi.trace(start, ent->mins, ent->maxs, ent->s.origin,
                      ignore, CONTENTS_MONSTER | CONTENTS_DEADMONSTER);
        if (!tr.ent || tr.ent == world)
            continue;
        VectorCopy(tr.endpos, start);
        ignore = tr.ent;

        if (!(tr.ent->flags & FL_NOCLIP_PROJECTILE))
            continue;

        //gi.bprintf (PRINT_HIGH, "%s: ent %d touching ent %d\n",
        //    __func__, ent->s.number, tr.ent->s.number);
        tr.ent->touch(tr.ent, ent, NULL, NULL);
    }
}

/*
==============
ClientThink

This will be called once for each client frame, which will
usually be a couple times for each server frame.
==============
*/
void ClientThink(edict_t *ent, usercmd_t *ucmd)
{
    gclient_t   *client;
    edict_t *other;
    int     i, j;
    pmove_t pm;
    vec3_t start;

    level.current_entity = ent;
    client = ent->client;

    client->level.cmd.angles[0] = SHORT2ANGLE(ucmd->angles[0]);
    client->level.cmd.angles[1] = SHORT2ANGLE(ucmd->angles[1]);
    client->level.cmd.angles[2] = SHORT2ANGLE(ucmd->angles[2]);

    if (abs(ucmd->forwardmove - client->level.cmd.forwardmove) >= 10 ||
        abs(ucmd->upmove - client->level.cmd.upmove) >= 10 ||
        abs(ucmd->sidemove - client->level.cmd.sidemove) >= 10 ||
        (ucmd->buttons ^ client->level.cmd.buttons) & BUTTON_ATTACK) {
        client->resp.activity_framenum = level.framenum;
        if (client->pers.connected == CONN_SPAWNED) {
            level.activity_framenum = level.framenum;
        }
    }

    client->level.cmd.framenum = level.framenum;
    client->level.cmd.forwardmove = ucmd->forwardmove;
    client->level.cmd.upmove = ucmd->upmove;
    client->level.cmd.sidemove = ucmd->sidemove;
    client->level.cmd.buttons = ucmd->buttons;

    if (level.intermission_framenum) {
        client->ps.pmove.pm_type = PM_FREEZE;
        return;
    }

    if (!ent->client->chase_target) {
        // set up for pmove
        memset(&pm, 0, sizeof(pm));

        if (ent->movetype == MOVETYPE_NOCLIP)
            client->ps.pmove.pm_type = PM_SPECTATOR;
        else if (ent->s.modelindex != 255)
            client->ps.pmove.pm_type = PM_GIB;
        else if (ent->deadflag)
            client->ps.pmove.pm_type = PM_DEAD;
        else
            client->ps.pmove.pm_type = PM_NORMAL;

        pm_passent = ent;
        pm_mask = ent->health > 0 ? MASK_PLAYERSOLID : MASK_DEADSOLID;

        client->ps.pmove.gravity = sv_gravity->value;
        pm.s = client->ps.pmove;

        VectorCopy(ent->s.origin, start);

        for (i = 0; i < 3; i++) {
            pm.s.origin[i] = COORD2SHORT(ent->s.origin[i]);
            pm.s.velocity[i] = COORD2SHORT(ent->velocity[i]);
        }

        if (memcmp(&client->old_pmove, &pm.s, sizeof(pm.s))) {
            pm.snapinitial = true;
            //gi.dprintf("pmove changed!\n");
        }

        pm.cmd = *ucmd;

        pm.trace = PM_trace;  // adds default parms
        pm.pointcontents = gi.pointcontents;

        // perform a pmove
        gi.Pmove(&pm);

        for (i = 0; i < 3; i++) {
            ent->s.origin[i] = SHORT2COORD(pm.s.origin[i]);
            ent->velocity[i] = SHORT2COORD(pm.s.velocity[i]);
        }

        VectorCopy(pm.mins, ent->mins);
        VectorCopy(pm.maxs, ent->maxs);

        if (~client->ps.pmove.pm_flags & pm.s.pm_flags & PMF_JUMP_HELD && pm.waterlevel == 0)
            gi.sound(ent, CHAN_VOICE, level.sounds.jump, 1, ATTN_NORM, 0);

        // save results of pmove
        client->ps.pmove = pm.s;
        client->old_pmove = pm.s;

        ent->viewheight = pm.viewheight;
        ent->waterlevel = pm.waterlevel;
        ent->watertype = pm.watertype;
        ent->groundentity = pm.groundentity;
        if (pm.groundentity)
            ent->groundentity_linkcount = pm.groundentity->linkcount;

        if (ent->deadflag) {
            client->ps.viewangles[ROLL] = 40;
            client->ps.viewangles[PITCH] = -15;
            client->ps.viewangles[YAW] = client->killer_yaw;
        } else {
            VectorCopy(pm.viewangles, client->v_angle);
            VectorCopy(pm.viewangles, client->ps.viewangles);
        }

        if (ent->movetype != MOVETYPE_NOCLIP) {
            gi.linkentity(ent);

            // touch trigger objects
            G_TouchTriggers(ent);

            // touch solid objects
            for (i = 0; i < pm.numtouch; i++) {
                other = pm.touchents[i];
                for (j = 0; j < i; j++)
                    if (pm.touchents[j] == other)
                        break;
                if (j != i)
                    continue;   // duplicated
                if (!other->touch)
                    continue;
                other->touch(other, ent, NULL, NULL);
            }

            // touch non-solid projectiles
            G_TouchProjectiles(ent, start);
        }
    }

    client->oldbuttons = client->buttons;
    client->buttons = ucmd->buttons;
    client->latched_buttons |= client->buttons & ~client->oldbuttons;

    // save light level the player is standing on for
    // monster sighting AI
    ent->light_level = ucmd->lightlevel;

    // fire weapon from final position if needed
    if (client->latched_buttons & BUTTON_ATTACK) {
        if (client->pers.connected == CONN_PREGAME) {
            spectator_respawn(ent, CONN_SPAWNED);
        } else if (client->pers.connected == CONN_SPECTATOR) {
            client->latched_buttons = 0;
            if (client->chase_target) {
                SetChaseTarget(ent, NULL);
            } else {
                GetChaseTarget(ent, CHASE_NONE);
            }
        } else if (!client->weapon_thunk) {
            client->weapon_thunk = true;
            Think_Weapon(ent);
        }
    }

    if (client->pers.connected == CONN_SPECTATOR) {
        if (abs(ucmd->upmove) >= 10) {
            if (!client->level.jump_held) {
                client->level.jump_held = true;
                if (client->chase_target) {
                    if (ucmd->upmove > 0) {
                        ChaseNext(ent);
                    } else {
                        ChasePrev(ent);
                    }
                    client->chase_mode = CHASE_NONE;
                }
            }
        } else {
            client->level.jump_held = false;
        }
    }

}


/*
==============
ClientBeginServerFrame

This will be called once for each server frame, before running
any other entities in the world.
==============
*/
void ClientBeginServerFrame(edict_t *ent)
{
    gclient_t   *client;

    if (level.intermission_framenum)
        return;

    client = ent->client;

    if (client->pers.connected == CONN_SPAWNED) {
        if (FRAMESYNC) {
            // run weapon animations if it hasn't been done by a ucmd_t
            if (!client->weapon_thunk)
                Think_Weapon(ent);
            else
                client->weapon_thunk = false;
        }

        if (g_idle_time->value > 0) {
            int timeout = g_idle_time->value * HZ;
            int penalty = 0;

            if (level.framenum - client->level.cmd.framenum < 1 * HZ) {
                if (abs(client->level.cmd.forwardmove) >= 10 ||
                    abs(client->level.cmd.sidemove) >= 10)
                    penalty++;

                if (client->level.cmd.buttons & BUTTON_ATTACK)
                    penalty++;
            }

            if (level.framenum - client->resp.activity_framenum > timeout >> penalty) {
                int kick = (int)g_idle_kick->value;

                gi.bprintf(PRINT_HIGH, "Removing %s from the game due to inactivity.\n",
                           client->pers.netname);

                if ((kick > 0 && penalty) || kick > 1)
                    gi.AddCommandString(va("kick %d\n", (int)(client - game.clients)));
                else
                    spectator_respawn(ent, CONN_SPECTATOR);
                return;
            }
        }

        if (ent->deadflag) {
            // wait for any button just going down
            if (level.framenum > client->respawn_framenum) {
                // in deathmatch, only wait for attack button
                if ((client->latched_buttons & BUTTON_ATTACK) ||
                    (DF(FORCE_RESPAWN) && level.framenum - client->respawn_framenum > 2 * HZ)) {
                    respawn(ent);
                    client->latched_buttons = 0;
                }
            }
            return;
        }
    }

    client->latched_buttons = 0;
}
