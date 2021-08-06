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
#include <errno.h>

game_locals_t   game;
level_locals_t  level;
game_import_t   gi;
game_export_t   globals;
spawn_temp_t    st;

int             meansOfDeath;

edict_t         *g_edicts;

cvar_t  *dmflags;
cvar_t  *skill;
cvar_t  *fraglimit;
cvar_t  *timelimit;
cvar_t  *maxclients;
cvar_t  *maxentities;
cvar_t  *g_select_empty;
cvar_t  *g_idle_time;
cvar_t  *g_idle_kick;
cvar_t  *g_vote_mask;
cvar_t  *g_vote_time;
cvar_t  *g_vote_treshold;
cvar_t  *g_vote_limit;
cvar_t  *g_vote_flags;
cvar_t  *g_intermission_time;
cvar_t  *g_admin_password;
cvar_t  *g_maps_random;
cvar_t  *g_maps_file;
cvar_t  *g_defaults_file;
cvar_t  *g_item_ban;
cvar_t  *g_bugs;
cvar_t  *g_teleporter_nofreeze;
cvar_t  *g_spawn_mode;
cvar_t  *g_team_chat;
cvar_t  *g_mute_chat;
cvar_t  *g_protection_time;
cvar_t  *g_log_stats;
cvar_t  *g_skins_file;
cvar_t  *g_motd_file;
cvar_t  *g_highscore_path;
cvar_t  *dedicated;

cvar_t  *sv_maxvelocity;
cvar_t  *sv_gravity;

cvar_t  *sv_rollspeed;
cvar_t  *sv_rollangle;
cvar_t  *gun_x;
cvar_t  *gun_y;
cvar_t  *gun_z;

cvar_t  *run_pitch;
cvar_t  *run_roll;
cvar_t  *bob_up;
cvar_t  *bob_pitch;
cvar_t  *bob_roll;

cvar_t  *sv_cheats;
cvar_t  *sv_hostname;

cvar_t  *flood_msgs;
cvar_t  *flood_persecond;
cvar_t  *flood_waitdelay;

cvar_t  *flood_waves;
cvar_t  *flood_perwave;
cvar_t  *flood_wavedelay;

cvar_t  *flood_infos;
cvar_t  *flood_perinfo;
cvar_t  *flood_infodelay;

LIST_DECL(g_map_list);
LIST_DECL(g_map_queue);

//cvar_t  *sv_features;

void ClientThink(edict_t *ent, usercmd_t *cmd);
qboolean ClientConnect(edict_t *ent, char *userinfo);
void ClientUserinfoChanged(edict_t *ent, char *userinfo);
void ClientDisconnect(edict_t *ent);
void ClientBegin(edict_t *ent);
void ClientCommand(edict_t *ent);


//===================================================================

/*
=================
ClientEndServerFrames
=================
*/
static void ClientEndServerFrames(void)
{
    int         i;
    gclient_t   *c;

    if (level.intermission_framenum) {
        // if the end of unit layout is displayed, don't give
        // the player any normal movement attributes
        for (i = 0, c = game.clients; i < game.maxclients; i++, c++) {
            if (c->pers.connected <= CONN_CONNECTED) {
                continue;
            }
            IntermissionEndServerFrame(c->edict);
        }
        return;
    }

    // calc the player views now that all pushing
    // and damage has been added
    for (i = 0, c = game.clients; i < game.maxclients; i++, c++) {
        if (c->pers.connected <= CONN_CONNECTED) {
            continue;
        }

        if (c->pers.connected == CONN_PREGAME && game.motd[0]) {
            int delta = level.framenum - c->resp.enter_framenum;

            if (c->layout == LAYOUT_NONE && delta == 1.5f * HZ) {
                Cmd_Motd_f(c->edict);
            } else if (c->layout == LAYOUT_MOTD && delta == 15 * HZ) {
                c->layout = 0;
            }
        }

        if (!c->chase_target) {
            ClientEndServerFrame(c->edict);
        }

        // if the scoreboard is up, update it
        if (c->layout == LAYOUT_SCORES && !(level.framenum % (3 * HZ))) {
            DeathmatchScoreboardMessage(c->edict, false);
        }

        PMenu_Update(c->edict);
    }

    // update chase cam after all stats and positions are calculated
    for (i = 0, c = game.clients; i < game.maxclients; i++, c++) {
        if (c->pers.connected <= CONN_CONNECTED) {
            continue;
        }
        if (c->chase_target) {
            ChaseEndServerFrame(c->edict);
        }
    }
}

typedef struct {
    int nb_lines;
    char **lines;
    char path[1];
} load_file_t;

static load_file_t *G_LoadFile(const char *dir, const char *name)
{
    int err = 0;

    if (!game.dir[0] || !*name)
        return NULL;

    char *path = dir ? va("%s/%s/%s.txt", game.dir, dir, name) :
                       va("%s/%s.txt",    game.dir,      name);
    size_t pathlen = strlen(path);
    if (pathlen >= MAX_OSPATH) {
        err = ENAMETOOLONG;
        goto fail0;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        err = errno;
        goto fail0;
    }

    Q_STATBUF st;
    if (os_fstat(os_fileno(fp), &st)) {
        err = errno;
        goto fail1;
    }

    if (st.st_size >= INT_MAX / sizeof(char *)) {
        err = EFBIG;
        goto fail1;
    }

    char *buf = G_Malloc(st.st_size + 1);

    if (!fread(buf, st.st_size, 1, fp)) {
        err = EIO;
        goto fail2;
    }

    buf[st.st_size] = 0;

    load_file_t *f = G_Malloc(sizeof(*f) + pathlen);
    f->nb_lines = 0;
    f->lines = NULL;
    memcpy(f->path, path, pathlen + 1);

    int max_lines = 0;
    while (1) {
        char *p = strchr(buf, '\n');
        if (p) {
            if (p > buf && *(p - 1) == '\r')
                *(p - 1) = 0;
            *p = 0;
        }

        if (f->nb_lines == max_lines) {
            void *tmp = f->lines;
            f->lines = G_Malloc(sizeof(char *) * (max_lines += 32));
            if (tmp) {
                memcpy(f->lines, tmp, sizeof(char *) * f->nb_lines);
                G_Free(tmp);
            }
        }
        f->lines[f->nb_lines++] = buf;

        if (!p)
            break;
        buf = p + 1;
    }

    fclose(fp);
    return f;

fail2:
    G_Free(buf);
fail1:
    fclose(fp);
fail0:
    gi.dprintf("Couldn't load '%s': %s\n", path, strerror(err));
    return NULL;
}

static void G_FreeFile(load_file_t *f)
{
    G_Free(f->lines[0]);
    G_Free(f->lines);
    G_Free(f);
}

static int ScoreCmp(const void *p1, const void *p2)
{
    score_t *a = (score_t *)p1;
    score_t *b = (score_t *)p2;

    if (a->score > b->score) {
        return -1;
    }
    if (a->score < b->score) {
        return 1;
    }
    if (a->time > b->time) {
        return -1;
    }
    if (a->time < b->time) {
        return 1;
    }
    return 0;
}

static void G_SaveScores(void)
{
    char path[MAX_OSPATH];
    score_t *s;
    FILE *fp;
    int i;
    size_t len;

    if (!game.dir[0]) {
        return;
    }

    len = Q_concat(path, sizeof(path), game.dir, "/", g_highscore_path->string, NULL);
    if (len >= sizeof(path)) {
        return;
    }
    os_mkdir(path);

    len = Q_concat(path, sizeof(path), game.dir, "/", g_highscore_path->string, "/",
                   level.mapname, ".txt", NULL);
    if (len >= sizeof(path)) {
        return;
    }
    fp = fopen(path, "w");
    if (!fp) {
        return;
    }

    for (i = 0; i < level.numscores; i++) {
        s = &level.scores[i];
        fprintf(fp, "\"%s\" %d %lu\n",
                s->name, s->score, (unsigned long)s->time);
    }

    fclose(fp);
}


static void G_RegisterScore(void)
{
    gclient_t    *ranks[MAX_CLIENTS];
    gclient_t    *c;
    score_t *s;
    int total;
    int sec, score;

    total = G_CalcRanks(ranks);
    if (!total) {
        return;
    }

    // grab our champion
    c = ranks[0];

    // calculate FPH
    sec = (level.framenum - c->resp.enter_framenum) / HZ;
    if (!sec) {
        sec = 1;
    }
    score = c->resp.score * 3600 / sec;

    if (score < 1) {
        return; // do not record bogus results
    }

    if (level.numscores < MAX_SCORES) {
        s = &level.scores[level.numscores++];
    } else {
        s = &level.scores[ level.numscores - 1 ];
        if (score < s->score) {
            return; // result not impressive enough
        }
    }

    strcpy(s->name, c->pers.netname);
    s->score = score;
    time(&s->time);

    level.record = s->time;

    qsort(level.scores, level.numscores, sizeof(score_t), ScoreCmp);

    gi.dprintf("Added highscore entry for %s with %d FPH\n",
               c->pers.netname, score);

    G_SaveScores();
}

void G_LoadScores(void)
{
    char *token;
    const char *data;
    score_t *s;
    load_file_t *f;
    int i;

    f = G_LoadFile(g_highscore_path->string, level.mapname);
    if (!f) {
        return;
    }

    for (i = 0; i < f->nb_lines && level.numscores < MAX_SCORES; i++) {
        data = f->lines[i];

        if (data[0] == '#' || data[0] == '/') {
            continue;
        }

        token = COM_Parse(&data);
        if (!*token) {
            continue;
        }

        s = &level.scores[level.numscores++];
        Q_strlcpy(s->name, token, sizeof(s->name));

        token = COM_Parse(&data);
        s->score = strtoul(token, NULL, 10);

        token = COM_Parse(&data);
        s->time = strtoul(token, NULL, 10);
    }

    qsort(level.scores, level.numscores, sizeof(score_t), ScoreCmp);

    gi.dprintf("Loaded %d scores from '%s'\n", level.numscores, f->path);

    G_FreeFile(f);
}

map_entry_t *G_FindMap(const char *name)
{
    map_entry_t *map;

    LIST_FOR_EACH(map_entry_t, map, &g_map_list, list) {
        if (!Q_stricmp(map->name, name)) {
            return map;
        }
    }

    return NULL;
}

static int G_RebuildMapQueue(void)
{
    map_entry_t **pool, *map;
    int i, count;

    List_Init(&g_map_queue);

    count = List_Count(&g_map_list);
    if (!count)
        return 0;

    pool = G_Malloc(sizeof(map_entry_t *) * count);

    // build the queue from available map list
    count = 0;
    LIST_FOR_EACH(map_entry_t, map, &g_map_list, list) {
        if (map->flags & MAP_NOAUTO) {
            continue;
        }
        if (map->flags & MAP_EXCLUSIVE) {
            if (count && (pool[count - 1]->flags & MAP_WEIGHTED))
                continue;
        } else if (map->flags & MAP_WEIGHTED) {
            if (random() > map->weight)
                continue;
        }
        pool[count++] = map;
    }

    if (!count)
        goto done;

    gi.dprintf("Map queue: %d entries\n", count);

    // randomize it
    if ((int)g_maps_random->value > 0) {
        G_ShuffleArray(pool, count);
    }

    for (i = 0; i < count; i++) {
        List_Append(&g_map_queue, &pool[i]->queue);
    }

done:
    G_Free(pool);
    return count;
}

static map_entry_t *G_FindSuitableMap(void)
{
    int total = G_CalcRanks(NULL);
    map_entry_t *map;

    LIST_FOR_EACH(map_entry_t, map, &g_map_queue, queue) {
        if (total >= map->min_players && total <= map->max_players) {
            if ((int)g_maps_random->value < 2 || strcmp(map->name, level.mapname)) {
                return map;
            }
        }
    }

    return NULL;
}

static void G_PickNextMap(void)
{
    map_entry_t *map;

    // if map list is empty, stay on the same level
    if (LIST_EMPTY(&g_map_list)) {
        return;
    }

    // pick the suitable map
    map = G_FindSuitableMap();
    if (!map) {
        // if map queue is empty, rebuild it
        if (!G_RebuildMapQueue()) {
            return;
        }
        map = G_FindSuitableMap();
        if (!map) {
            gi.dprintf("Couldn't find next map!\n");
            return;
        }
    }

    List_Delete(&map->queue);
    map->num_hits++;

    gi.dprintf("Next map is %s.\n", map->name);
    strcpy(level.nextmap, map->name);
}

static void G_LoadMapList(void)
{
    char *token;
    const char *data;
    map_entry_t *map;
    load_file_t *f;
    size_t len;
    int i, nummaps;

    f = G_LoadFile("mapcfg", g_maps_file->string);
    if (!f) {
        return;
    }

    for (i = nummaps = 0; i < f->nb_lines; i++) {
        data = f->lines[i];

        if (data[0] == '#' || data[0] == '/') {
            continue;
        }

        token = COM_Parse(&data);
        if (!*token) {
            continue;
        }

        len = strlen(token);
        if (len >= MAX_QPATH) {
            gi.dprintf("Oversize mapname at line %d in %s\n", i, f->path);
            continue;
        }

        map = G_Malloc(sizeof(*map) + len);
        memcpy(map->name, token, len + 1);

        token = COM_Parse(&data);
        map->min_players = atoi(token);

        token = COM_Parse(&data);
        map->max_players = *token ? atoi(token) : game.maxclients;

        token = COM_Parse(&data);
        map->flags = atoi(token);

        token = COM_Parse(&data);
        if (*token == '@') {
            map->flags |= MAP_EXCLUSIVE;
            map->weight = 0;
        } else if (*token) {
            map->flags |= MAP_WEIGHTED;
            map->weight = atof(token);
        } else {
            map->weight = 1;
        }

        if (map->min_players < 0) {
            map->min_players = 0;
        }
        if (map->max_players > game.maxclients) {
            map->max_players = game.maxclients;
        }

        List_Append(&g_map_list, &map->list);
        nummaps++;
    }

    gi.dprintf("Loaded %d maps from '%s'\n", nummaps, f->path);

    G_FreeFile(f);
}

static void G_LoadSkinList(void)
{
    char *token;
    const char *data;
    skin_entry_t *skin;
    load_file_t *f;
    size_t len;
    int i, numskins, numdirs;

    f = G_LoadFile(NULL, g_skins_file->string);
    if (!f) {
        return;
    }

    for (i = numskins = numdirs = 0; i < f->nb_lines; i++) {
        data = f->lines[i];

        if (data[0] == '#' || data[0] == '/') {
            continue;
        }

        token = COM_Parse(&data);
        if (!*token) {
            continue;
        }

        len = strlen(token);
        if (len >= MAX_SKINNAME) {
            gi.dprintf("Oversize skinname at line %d in %s\n", i, f->path);
            continue;
        }

        skin = G_Malloc(sizeof(*skin) + len);
        memcpy(skin->name, token, len + 1);

        if (token[ len - 1 ] == '/') {
            skin->name[ len - 1 ] = 0;
            skin->next = game.skins;
            game.skins = skin;
            numdirs++;
        } else if (game.skins) {
            skin->next = game.skins->down;
            game.skins->down = skin;
            numskins++;
        } else {
            gi.dprintf("Skinname before directory at line %d in %s\n", i, f->path);
        }
    }

    gi.dprintf("Loaded %d skins in %d dirs from '%s'\n",
               numskins, numdirs, f->path);

    G_FreeFile(f);
}

static int seqlen(const char *s, int ch)
{
    int len = 0;
    while (*s++ == ch)
        len++;
    return len;
}

static void makeframe(load_file_t *f, int num, const char *seq, int ch)
{
    char *data = f->lines[num];
    char *p, *s, *scan = data;
    int i, j, pos, len;

    while (1) {
        p = strstr(scan, seq);
        if (!p)
            break;

        pos = p - data;
        len = seqlen(p, *seq);
        for (i = num + 1, j = -1; i < f->nb_lines; i++) {
            s = f->lines[i];
            if (strlen(s) > pos && (!pos || s[pos - 1] != *seq) && seqlen(s + pos, *seq) == len) {
                j = i;
                break;
            }
        }

        if (j > 0) {
            p[0] = 0x01 + ch;
            for (i = 1; i < len - 1; i++)
                p[i] = 0x02 + ch;
            p[i] = 0x03 + ch;

            for (i = num + 1; i < j; i++) {
                s = f->lines[i];
                if (strlen(s) > pos && s[pos] == '|')
                    s[pos] = 0x04 + ch;
                if (strlen(s) > pos + len - 1 && s[pos + len - 1] == '|')
                    s[pos + len - 1] = 0x06 + ch;
            }

            s = f->lines[j] + pos;
            s[0] = 0x07 + ch;
            for (i = 1; i < len - 1; i++)
                s[i] = 0x08 + ch;
            s[i] = 0x09 + ch;
        }

        scan = p + len;
    }
}

static void makebar(char *data, const char *seq, int ch)
{
    while (1) {
        char *p = strstr(data, seq);
        if (!p)
            break;

        int len = seqlen(p, *seq);
        int i;

        p[0] = ch;
        for (i = 1; i < len - 1; i++)
            p[i] = ch + 1;
        p[i] = ch + 2;

        data = p + len;
    }
}

static void makecolor(char *data)
{
    while (1) {
        char *p = strchr(data, '*');
        if (!p)
            break;
        char *q = strchr(p + 1, '*');
        if (!q)
            break;
        if (q > p + 1) {
            *p++ = *q = ' ';
            while (p < q)
                *p++ ^= 0x80;
        }
        data = q + 1;
    }
}

static char *transform(load_file_t *f, int num)
{
    char *data = f->lines[num];

    makeframe(f, num, "===", 0);
    makeframe(f, num, "~~~", 0x11);

    makebar(data, "---", 0x1d);
    makebar(data, "^^^", 0x80);

    makecolor(data);
    return data;
}

static void G_LoadMotd(void)
{
    char  *text = game.motd;
    size_t size = sizeof(game.motd);

    load_file_t *f = G_LoadFile("motd", g_motd_file->string);
    if (!f)
        return;

    size_t len = Q_strlcpy(text, "xl 8 ", size);
    text += len;
    size -= len;

    for (int i = 0; i < f->nb_lines; i++) {
        char *data = transform(f, i);
        if (!*data)
            continue;
        len = Q_snprintf(text, size, "yb %d string \"%s\"",
                         -32 - (f->nb_lines - i - 1) * 8, data);
        if (len >= size) {
            gi.dprintf("Oversize motd in %s\n", f->path);
            break;
        }
        text += len;
        size -= len;
    }

    *text = 0;
    if (len < size)
        gi.dprintf("Loaded motd from %s\n", f->path);

    G_FreeFile(f);
}

/*
=================
EndDMLevel

The timelimit or fraglimit has been exceeded
=================
*/
void EndDMLevel(void)
{
    G_RegisterScore();

    BeginIntermission();

    if (level.nextmap[0]) {
        return; // already set by operator or vote
    }

    strcpy(level.nextmap, level.mapname);

    // stay on same level flag
    if (DF(SAME_LEVEL)) {
        return;
    }

    G_PickNextMap();
}

void G_StartSound(int index)
{
    gi.sound(world, CHAN_RELIABLE, index, 1, ATTN_NONE, 0);
}

void G_StuffText(edict_t *ent, const char *text)
{
    gi.WriteByte(svc_stufftext);
    gi.WriteString(text);
    gi.unicast(ent, true);
}

static void G_SetTimeVar(int remaining)
{
    int sec = remaining % 60;
    int min = remaining / 60;

    gi.cvar_set("time_remaining", va("%d:%02d", min, sec));
}

/*
=================
CheckDMRules
=================
*/
static void CheckDMRules(void)
{
    int         i;
    gclient_t   *c;

    if (g_item_ban->modified) {
        G_UpdateItemBans();
    }

    if (g_vote_treshold->modified) {
        G_CheckVote();
        g_vote_treshold->modified = false;
    }

    if (g_vote_flags->modified) {
        G_CheckVote();
        g_vote_flags->modified = false;
    }

    if (timelimit->value > 0) {
        if (level.time >= timelimit->value * 60) {
            gi.bprintf(PRINT_HIGH, "Timelimit hit.\n");
            G_SetTimeVar(0);
            EndDMLevel();
            return;
        }
        if (timelimit->modified || (level.framenum % HZ) == 0) {
            int delta = level.framenum /*- level.match_framenum*/;
            int remaining = timelimit->value * 60 - delta / HZ;

            G_WriteTime(remaining);
            gi.multicast(NULL, MULTICAST_ALL);

            G_SetTimeVar(remaining);

            // notify
            switch (remaining) {
            case 10:
                gi.bprintf(PRINT_HIGH, "10 seconds remaining in match.\n");
                G_StartSound(level.sounds.count);
                break;
            case 60:
                gi.bprintf(PRINT_HIGH, "1 minute remaining in match.\n");
                G_StartSound(level.sounds.secret);
                break;
            case 300:
            case 600:
            case 900:
                gi.bprintf(PRINT_HIGH, "%d minutes remaining in match.\n",
                           remaining / 60);
                G_StartSound(level.sounds.secret);
                break;
            }
        }
    } else if (timelimit->modified) {
        gi.cvar_set("time_remaining", "");
    }

    if ((int)fraglimit->value > 0) {
        for (i = 0, c = game.clients; i < game.maxclients; i++, c++) {
            if (c->pers.connected != CONN_SPAWNED) {
                continue;
            }
            if (c->resp.score >= fraglimit->value) {
                gi.bprintf(PRINT_HIGH, "Fraglimit hit.\n");
                EndDMLevel();
                return;
            }
        }
    }

    if (fraglimit->modified) {
        for (i = 0, c = game.clients; i < game.maxclients; i++, c++) {
            if (c->pers.connected != CONN_SPAWNED) {
                continue;
            }
            G_ScoreChanged(c->edict);
        }
        G_UpdateRanks();
    }


    timelimit->modified = false;
    fraglimit->modified = false;
}

static void G_ResetSettings(void)
{
    char command[256];

    gi.bprintf(PRINT_HIGH, "No active players, restoring default game settings\n");

    if (g_defaults_file->string[0]) {
        Q_snprintf(command, sizeof(command), "exec \"%s\"\n", g_defaults_file->string);
        gi.AddCommandString(command);
    }

    game.settings_modified = 0;
}


/*
=============
ExitLevel
=============
*/
void G_ExitLevel(void)
{
    char command[256];
    map_entry_t *map;

    if (level.intermission_exit) {
        return; // already exited
    }

    // reset settings if no one was active on the previous map
    //if (game.settings_modified && !level.activity_framenum) {
    //    G_ResetSettings();
    //}

    map = G_FindMap(level.mapname);
    if (map) {
        map->num_in += level.players_in;
        map->num_out += level.players_out;
    }

    if (!level.nextmap[0] || !strcmp(level.nextmap, level.mapname)) {
        G_ResetLevel();
        return;
    }

    Q_snprintf(command, sizeof(command), "gamemap \"%s\"\n", level.nextmap);
    gi.AddCommandString(command);

    level.intermission_exit = level.framenum;
}

/*
================
G_RunFrame

Advances the world by 0.1 seconds
================
*/
void G_RunFrame(void)
{
    int     i, delta;
    edict_t *ent;

    //
    // treat each object in turn
    // even the world gets a chance to think
    //
    for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++) {
        if (!ent->inuse)
            continue;

        level.current_entity = ent;

        VectorCopy(ent->old_origin, ent->s.old_origin);

        // if the ground entity moved, make sure we are still on it
        if ((ent->groundentity) && (ent->groundentity->linkcount != ent->groundentity_linkcount)) {
            ent->groundentity = NULL;
        }

        if (i > 0 && i <= game.maxclients) {
            ClientBeginServerFrame(ent);
            continue;
        }

        G_RunEntity(ent);
    }

    if (level.intermission_exit) {
        if (level.framenum > level.intermission_exit + 5) {
            G_ResetLevel(); // in case gamemap failed, reset the level
        }
    } else if (level.intermission_framenum) {
        int exit_delta = g_intermission_time->value * HZ;

        clamp(exit_delta, 5 * HZ, 120 * HZ);

        delta = level.framenum - level.intermission_framenum;
        if (delta == 1 * HZ) {
            if (Q_rand() & 1) {
                G_StartSound(level.sounds.xian);
            } else {
                G_StartSound(level.sounds.makron);
            }
        } else if (delta == exit_delta) {
            G_ExitLevel();
        } else if (delta % (5 * HZ) == 0) {
            delta /= 5 * HZ;
            if (level.numscores && (delta & 1)) {
                HighScoresMessage();
            } else {
                for (i = 0, ent = &g_edicts[1]; i < game.maxclients; i++, ent++) {
                    if (ent->client->pers.connected > CONN_CONNECTED) {
                        DeathmatchScoreboardMessage(ent, true);
                    }
                }
            }
        }
    } else {
#if 0
        if (level.warmup_framenum) {
            delta = level.framenum - level.warmup_framenum;
        } else if (level.countdown_framenum) {
            delta = level.framenum - level.countdown_framenum;
            if ((level.framenum % HZ) == 0) {
                int remaining = 15 * HZ - delta;

                if (remaining) {
                    G_WriteTime(remaining);
                    if (remaining == 10 * HZ) {
                        G_StartSound(level.sounds.count);
                    }
                } else {
                    gi.bprintf(PRINT_HIGH, "Match has started!\n");
                    level.countdown_framenum = 0;
                    level.match_framenum = level.framenum;
                }
            }
        } else
#endif
        {
            // see if it is time to end a deathmatch
            CheckDMRules();
        }

        // check vote timeout
        if (level.vote.proposal) {
            G_UpdateVote();
        }
    }

    // build the playerstate_t structures for all players
    ClientEndServerFrames();

    // reset settings if no one was active for the last 5 minutes
    if (game.settings_modified && level.framenum - level.activity_framenum > 5 * 60 * HZ) {
        G_ResetSettings();
    }

    // save old_origins for next frame
    for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++) {
        if (ent->inuse)
            VectorCopy(ent->s.origin, ent->old_origin);
    }

    G_RunDatabase();

    // advance for next frame
    level.framenum++;
    level.time = level.framenum * FRAMETIME;
}


static void G_Shutdown(void)
{
    gi.dprintf("==== ShutdownGame ====\n");

    G_CloseDatabase();

    gi.FreeTags(TAG_LEVEL);
    gi.FreeTags(TAG_GAME);

    memset(&game, 0, sizeof(game));

    // reset our features
    gi.cvar_forceset("g_features", "0");

    List_Init(&g_map_list);
    List_Init(&g_map_queue);
}

void G_CheckFilenameVariable(cvar_t *cv)
{
    if (strchr(cv->string, '/') || strstr(cv->string, "..")) {
        gi.dprintf("'%s' should be a single filename, not a path.\n", cv->name);
        gi.cvar_forceset(cv->name, "");
    }
}

/*
============
InitGame

This will be called when the dll is first loaded, which
only happens when a new game is started or a save game
is loaded.
============
*/
static void G_Init(void)
{
    cvar_t *cv;
    size_t len;

    gi.dprintf("==== InitGame ====\n");

    Q_srand(time(NULL));

    gun_x = gi.cvar("gun_x", "0", 0);
    gun_y = gi.cvar("gun_y", "0", 0);
    gun_z = gi.cvar("gun_z", "0", 0);

    //FIXME: sv_ prefix is wrong for these
    sv_rollspeed = gi.cvar("sv_rollspeed", "200", 0);
    sv_rollangle = gi.cvar("sv_rollangle", "2", 0);
    sv_maxvelocity = gi.cvar("sv_maxvelocity", "2000", 0);
    sv_gravity = gi.cvar("sv_gravity", "800", 0);

    // noset vars
    dedicated = gi.cvar("dedicated", "0", CVAR_NOSET);

    // latched vars
    sv_cheats = gi.cvar("cheats", "0", CVAR_SERVERINFO | CVAR_LATCH);
    sv_hostname = gi.cvar("hostname", NULL, 0);
    gi.cvar("gamename", GAMEVERSION, CVAR_SERVERINFO);
    gi.cvar_set("gamename", GAMEVERSION);
    gi.cvar("gamedate", __DATE__, CVAR_SERVERINFO);
    gi.cvar_set("gamedate", __DATE__);

    maxclients = gi.cvar("maxclients", "4", CVAR_SERVERINFO | CVAR_LATCH);
    maxentities = gi.cvar("maxentities", "1024", CVAR_LATCH);

    // change anytime vars
    dmflags = gi.cvar("dmflags", "0", CVAR_SERVERINFO);
    fraglimit = gi.cvar("fraglimit", "0", CVAR_SERVERINFO);
    timelimit = gi.cvar("timelimit", "0", CVAR_SERVERINFO);

    gi.cvar("time_remaining", "", CVAR_SERVERINFO);
    gi.cvar_set("time_remaining", "");

    gi.cvar("revision", va("%d", OPENFFA_REVISION), CVAR_SERVERINFO);
    gi.cvar_set("revision", va("%d", OPENFFA_REVISION));

    g_select_empty = gi.cvar("g_select_empty", "0", CVAR_ARCHIVE);
    g_idle_time = gi.cvar("g_idle_time", "0", 0);
    g_idle_kick = gi.cvar("g_idle_kick", "0", 0);
    g_vote_mask = gi.cvar("g_vote_mask", "0", 0);
    g_vote_time = gi.cvar("g_vote_time", "60", 0);
    g_vote_treshold = gi.cvar("g_vote_treshold", "50", 0);
    g_vote_limit = gi.cvar("g_vote_limit", "3", 0);
    g_vote_flags = gi.cvar("g_vote_flags", "11", 0);
    g_intermission_time = gi.cvar("g_intermission_time", "10", 0);
    g_admin_password = gi.cvar("g_admin_password", "", 0);
    g_maps_random = gi.cvar("g_maps_random", "2", 0);
    g_maps_file = gi.cvar("g_maps_file", "", CVAR_LATCH);
    g_defaults_file = gi.cvar("g_defaults_file", "", CVAR_LATCH);
    g_item_ban = gi.cvar("g_item_ban", "0", 0);
    g_bugs = gi.cvar("g_bugs", "0", 0);
    g_teleporter_nofreeze = gi.cvar("g_teleporter_nofreeze", "0", 0);
    g_spawn_mode = gi.cvar("g_spawn_mode", "1", 0);
    g_team_chat = gi.cvar("g_team_chat", "0", 0);
    g_mute_chat = gi.cvar("g_mute_chat", "0", 0);
    g_protection_time = gi.cvar("g_protection_time", "0", 0);
    g_skins_file = gi.cvar("g_skins_file", "", CVAR_LATCH);
    g_motd_file = gi.cvar("g_motd_file", "", CVAR_LATCH);
    g_highscore_path = gi.cvar("g_highscore_path", "highscores", CVAR_LATCH);

    run_pitch = gi.cvar("run_pitch", "0.002", 0);
    run_roll = gi.cvar("run_roll", "0.005", 0);
    bob_up  = gi.cvar("bob_up", "0.005", 0);
    bob_pitch = gi.cvar("bob_pitch", "0.002", 0);
    bob_roll = gi.cvar("bob_roll", "0.002", 0);

    // chat flood control
    flood_msgs = gi.cvar("flood_msgs", "4", 0);
    flood_persecond = gi.cvar("flood_persecond", "4", 0);
    flood_waitdelay = gi.cvar("flood_waitdelay", "10", 0);

    // wave flood control
    flood_waves = gi.cvar("flood_waves", "4", 0);
    flood_perwave = gi.cvar("flood_perwave", "30", 0);
    flood_wavedelay = gi.cvar("flood_wavedelay", "60", 0);

    // userinfo flood control
    flood_infos = gi.cvar("flood_infos", "4", 0);
    flood_perinfo = gi.cvar("flood_perinfo", "30", 0);
    flood_infodelay = gi.cvar("flood_infodelay", "60", 0);

    // force deathmatch
    //gi.cvar_set( "coop", "0" ); //atu
    //gi.cvar_set( "deathmatch", "1" );

    // initialize all entities for this game
    game.maxentities = maxentities->value;
    clamp(game.maxentities, (int)maxclients->value + 1, MAX_EDICTS);
    g_edicts = G_Malloc(game.maxentities * sizeof(g_edicts[0]));
    globals.edicts = g_edicts;
    globals.max_edicts = game.maxentities;

    // initialize all clients for this game
    game.maxclients = maxclients->value;
    game.clients = G_Malloc(game.maxclients * sizeof(game.clients[0]));
    globals.num_edicts = game.maxclients + 1;

    // obtain game path
    cv = gi.cvar("fs_gamedir", NULL, 0);
    if (cv && cv->string[0]) {
        len = Q_strlcpy(game.dir, cv->string, sizeof(game.dir));
    } else {
        cvar_t *basedir = gi.cvar("basedir", NULL, 0);
        cvar_t *gamedir = gi.cvar("game", NULL, 0);
        if (basedir && gamedir) {
            len = Q_concat(game.dir, sizeof(game.dir),
                           basedir->string, "/", gamedir->string, NULL);
        } else {
            len = 0;
        }
    }

    if (!len) {
        gi.dprintf("Failed to determine game directory.\n");
    } else if (len >= sizeof(game.dir)) {
        gi.dprintf("Oversize game directory.\n");
        game.dir[0] = 0;
    }

    G_CheckFilenameVariable(g_maps_file);
    G_CheckFilenameVariable(g_defaults_file);
    G_CheckFilenameVariable(g_skins_file);
    G_CheckFilenameVariable(g_motd_file);

    G_LoadMapList();
    G_LoadSkinList();
    G_LoadMotd();
    G_OpenDatabase();

    // obtain server features
    cv = gi.cvar("sv_features", NULL, 0);
    if (cv) {
        game.serverFeatures = (int)cv->value;
    }

#if USE_FPS
    // setup framerate parameters
    if (game.serverFeatures & GMF_VARIABLE_FPS) {
        int framediv;

        cv = gi.cvar("sv_fps", NULL, 0);
        if (!cv)
            gi.error("GMF_VARIABLE_FPS exported but no 'sv_fps' cvar");

        framediv = (int)cv->value / BASE_FRAMERATE;
        if (framediv < 1 || framediv > MAX_FRAMEDIV
            || framediv * BASE_FRAMERATE != (int)cv->value)
            gi.error("Invalid value '%s' for 'sv_fps' cvar", cv->string);

        game.framerate = framediv * BASE_FRAMERATE;
        game.frametime = BASE_FRAMETIME_1000 / framediv;
        game.framediv = framediv;
    } else {
        game.framerate = BASE_FRAMERATE;
        game.frametime = BASE_FRAMETIME_1000;
        game.framediv = 1;
    }
#endif

    // export our own features
    gi.cvar_forceset("g_features", va("%d", G_FEATURES));
}

static void G_WriteGame(const char *filename, qboolean autosave)
{
}

static void G_ReadGame(const char *filename)
{
}

static void G_WriteLevel(const char *filename)
{
}

static void G_ReadLevel(const char *filename)
{
}

//======================================================================

#ifndef GAME_HARD_LINKED

// this is only here so the functions in q_shared.c can link
void Com_LPrintf(print_type_t type, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    gi.dprintf("%s", text);
}

void Com_Error(error_type_t code, const char *error, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];

    va_start(argptr, error);
    Q_vsnprintf(text, sizeof(text), error, argptr);
    va_end(argptr);

    gi.error("%s", text);
}

#endif

/*
=================
GetGameAPI

Returns a pointer to the structure with all entry points
and global variables
=================
*/
q_exported game_export_t *GetGameAPI(game_import_t *import)
{
    gi = *import;

    globals.apiversion = GAME_API_VERSION;
    globals.Init = G_Init;
    globals.Shutdown = G_Shutdown;
    globals.SpawnEntities = G_SpawnEntities;

    globals.WriteGame = G_WriteGame;
    globals.ReadGame = G_ReadGame;
    globals.WriteLevel = G_WriteLevel;
    globals.ReadLevel = G_ReadLevel;

    globals.ClientThink = ClientThink;
    globals.ClientConnect = ClientConnect;
    globals.ClientUserinfoChanged = ClientUserinfoChanged;
    globals.ClientDisconnect = ClientDisconnect;
    globals.ClientBegin = ClientBegin;
    globals.ClientCommand = ClientCommand;

    globals.RunFrame = G_RunFrame;

    globals.ServerCommand = G_ServerCommand;

    globals.edict_size = sizeof(edict_t);

    return &globals;
}
