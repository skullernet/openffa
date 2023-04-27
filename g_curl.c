/*
Copyright (C) 2013 Andrey Nazarov

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
#include <curl/curl.h>

#define TAG_HTTP    767

#define FRAG_CHUNK  1024

typedef struct {
    list_t      entry;
    unsigned    maxsize;
    unsigned    cursize;
    unsigned    readpos;
    char        *data;
} fragment_t;

static const char *const frag_names[FRAG_TOTAL] = {
    "UNKNOWN",
    "BLASTER",
    "SHOTGUN",
    "SUPERSHOTGUN",
    "MACHINEGUN",
    "CHAINGUN",
    "GRENADES",
    "GRENADELAUNCHER",
    "ROCKETLAUNCHER",
    "HYPERBLASTER",
    "RAILGUN",
    "BFG",
    "TELEPORT",
    "WATER",
    "SLIME",
    "LAVA",
    "CRUSH",
    "FALLING",
    "SUICIDE",
};

static const char *const item_names[ITEM_TOTAL] = {
    "NULL",
    "ARMOR_BODY",
    "ARMOR_COMBAT",
    "ARMOR_JACKET",
    "ARMOR_SHARD",
    "POWER_SCREEN",
    "POWER_SHIELD",
    "BLASTER",
    "SHOTGUN",
    "SUPERSHOTGUN",
    "MACHINEGUN",
    "CHAINGUN",
    "GRENADES",
    "GRENADELAUNCHER",
    "ROCKETLAUNCHER",
    "HYPERBLASTER",
    "RAILGUN",
    "BFG",
    "SHELLS",
    "BULLETS",
    "CELLS",
    "ROCKETS",
    "SLUGS",
    "QUAD",
    "INVULNERABILITY",
    "SILENCER",
    "BREATHER",
    "ENVIRO",
    "ANCIENT_HEAD",
    "ADRENALINE",
    "BANDOLIER",
    "PACK",
    "HEALTH",
};

static cvar_t   *g_http_url;
static cvar_t   *g_http_interval;
static cvar_t   *g_http_debug;

static list_t       frag_list;
static list_t       *frag_cursor;
static unsigned     frag_total;
static unsigned     frag_remaining;

static char         recv_buffer[MAX_QPATH];
static unsigned     recv_size;

static fragment_t   *frag_current;

static CURLM                *curl_multi;
static CURL                 *curl_easy;
static struct curl_slist    *curl_headers;
static int                  curl_handles;

static unsigned     upload_backoff;
static unsigned     upload_framenum;
static unsigned     current_framenum;

static void         http_close(void);

static size_t recv_func(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t bytes;

    if (!nmemb)
        return 0;

    if (size > SIZE_MAX / nmemb)
        return 0;

    if (recv_size > sizeof(recv_buffer) - 1)
        return 0;

    bytes = size * nmemb;
    if (bytes > sizeof(recv_buffer) - recv_size - 1)
        bytes = sizeof(recv_buffer) - recv_size - 1;

    memcpy(recv_buffer + recv_size, ptr, bytes);
    recv_size += bytes;

    recv_buffer[recv_size] = 0;
    return bytes;
}

static size_t send_func(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    fragment_t *frag;
    size_t bytes, bytes_written, bytes_available;

    if (!nmemb)
        return 0;

    if (size > SIZE_MAX / nmemb)
        return CURL_READFUNC_ABORT;

    bytes_available = size * nmemb;
    bytes_written = 0;
    while (frag_remaining && bytes_available) {
        // got to the end of list with bytes still remaining?
        if (frag_cursor == &frag_list)
            return CURL_READFUNC_ABORT;

        frag = LIST_ENTRY(fragment_t, frag_cursor, entry);
        if (frag->readpos > frag->cursize)
            return CURL_READFUNC_ABORT;

        bytes = bytes_available;
        if (bytes > frag->cursize - frag->readpos)
            bytes = frag->cursize - frag->readpos;

        // trying to send more than remaining bytes?
        if (bytes > frag_remaining)
            return CURL_READFUNC_ABORT;

        memcpy((byte *)ptr + bytes_written, frag->data + frag->readpos, bytes);
        frag->readpos += bytes;

        // if done with this fragment, fetch next
        if (frag->readpos == frag->cursize)
            frag_cursor = frag->entry.next;

        frag_remaining -= bytes;
        bytes_available -= bytes;
        bytes_written += bytes;
    }

    return bytes_written;
}

static void start_upload(void)
{
    CURLMcode ret;

    if (curl_handles)
        return;

    if (LIST_EMPTY(&frag_list))
        return;

    if (current_framenum - upload_framenum < upload_backoff)
        return;

    frag_cursor = frag_list.next;
    frag_remaining = frag_total;

    recv_size = 0;
    recv_buffer[0] = 0;

    if (!curl_easy)
        curl_easy = curl_easy_init();

    curl_easy_setopt(curl_easy, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl_easy, CURLOPT_VERBOSE, (bool)(int)g_http_debug->value);
    curl_easy_setopt(curl_easy, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(curl_easy, CURLOPT_WRITEFUNCTION, recv_func);
    curl_easy_setopt(curl_easy, CURLOPT_READDATA, NULL);
    curl_easy_setopt(curl_easy, CURLOPT_READFUNCTION, send_func);
    curl_easy_setopt(curl_easy, CURLOPT_FAILONERROR, 1);
    curl_easy_setopt(curl_easy, CURLOPT_USERAGENT, GAMEVERSION " (" OPENFFA_VERSION ")");
    curl_easy_setopt(curl_easy, CURLOPT_REFERER, sv_hostname->string);
    curl_easy_setopt(curl_easy, CURLOPT_POST, 1);
    curl_easy_setopt(curl_easy, CURLOPT_POSTFIELDSIZE, frag_remaining);
    curl_easy_setopt(curl_easy, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl_easy, CURLOPT_URL, g_http_url->string);
    curl_easy_setopt(curl_easy, CURLOPT_DNS_CACHE_TIMEOUT, 24 * 60 * 60);
    curl_easy_setopt(curl_easy, CURLOPT_FORBID_REUSE, 1);

    ret = curl_multi_add_handle(curl_multi, curl_easy);
    if (ret != CURLM_OK) {
        gi.dprintf("[HTTP] Failed to add download handle: %s\n", curl_multi_strerror(ret));
        http_close();
        return;
    }

    gi.dprintf("[HTTP] Going to POST %u bytes\n", frag_remaining);
    curl_handles++;
}

static void free_fragment(fragment_t *frag)
{
    List_Remove(&frag->entry);
    frag_total -= frag->cursize;
    if (frag->data)
        gi.TagFree(frag->data);
    gi.TagFree(frag);
}

static void free_done_fragments(void)
{
    fragment_t *frag, *next;

    frag = LIST_FIRST(fragment_t, &frag_list, entry);
    while (&frag->entry != frag_cursor) {
        next = LIST_NEXT(fragment_t, frag, entry);
        free_fragment(frag);
        frag = next;
    }
}

static void finish_upload(void)
{
    int         msgs_in_queue;
    CURLMsg     *msg;
    long        response;

    unsigned minimum_interval = g_http_interval->value * 60 * HZ;

    do {
        msg = curl_multi_info_read(curl_multi, &msgs_in_queue);
        if (!msg)
            break;

        if (msg->msg != CURLMSG_DONE)
            continue;

        if (msg->easy_handle != curl_easy)
            continue;

        if (!curl_handles)
            continue;

        if (msg->data.result == CURLE_OK) {
            if (Q_strcasestr(recv_buffer, "success")) {
                gi.dprintf("[HTTP] Upload completed successfully\n");
                free_done_fragments();
            } else {
                gi.dprintf("[HTTP] Upload completed with invalid response body. Assuming failure.\n");
            }
            upload_backoff = minimum_interval;
        } else if (msg->data.result == CURLE_HTTP_RETURNED_ERROR) {
            curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &response);
            gi.dprintf("[HTTP] Upload completed with HTTP error code: %lu\n", response);
            upload_backoff += 120 * HZ;
        } else {
            gi.dprintf("[HTTP] Failed to perform upload: %s\n", curl_easy_strerror(msg->data.result));
            upload_backoff += 30 * HZ;
        }

        curl_handles--;
        curl_multi_remove_handle(curl_multi, msg->easy_handle);

        if (upload_backoff < minimum_interval)
            upload_backoff = minimum_interval;
        if (upload_backoff > 4 * 60 * 60 * HZ)
            upload_backoff = 4 * 60 * 60 * HZ;

        upload_framenum = current_framenum;
    } while (msgs_in_queue > 0);
}

static void append_fmt(const char *fmt, ...)
{
    fragment_t *f = frag_current;
    va_list argptr;
    size_t  len;
    char    *data;
    int     i;

    for (i = 0; i < 2; i++) {
        va_start(argptr, fmt);
        len = Q_vsnprintf(f->data + f->cursize, f->maxsize - f->cursize, fmt, argptr);
        va_end(argptr);

        if (len == (size_t)-1)
            break;

        if (len < f->maxsize - f->cursize) {
            f->cursize += len;
            break;
        }

        // grow buffer in FRAG_CHUNK chunks. +1 for NUL.
        f->maxsize = (f->cursize + len + FRAG_CHUNK) & ~(FRAG_CHUNK - 1);
        data = gi.TagMalloc(f->maxsize, TAG_HTTP);
        if (f->data) {
            memcpy(data, f->data, f->cursize);
            gi.TagFree(f->data);
        }
        f->data = data;
    }
}

static void append_str(const char *name, const char *string)
{
    append_fmt("\"%s\":\"%s\",", name, string);
}

static void append_num(const char *name, int number)
{
    append_fmt("\"%s\":%d,", name, number);
}

static void remove_comma(void)
{
    fragment_t *f = frag_current;

    if (f->cursize && f->data[f->cursize - 1] == ',') {
        f->data[f->cursize - 1] = 0;
        f->cursize--;
    }
}

static void begin_clients(void)
{
    // begin new fragment
    frag_current = gi.TagMalloc(sizeof(fragment_t), TAG_HTTP);

    append_fmt("{");
    append_fmt("\"timestamp\":%llu,", (unsigned long long)time(NULL));
    append_fmt("\"clients\":[");
}

static void end_clients(void)
{
    remove_comma();
    append_fmt("]},");

    List_Append(&frag_list, &frag_current->entry);
    frag_total += frag_current->cursize;

    // done building this fragment
    frag_current = NULL;
}

static void log_client(gclient_t *c)
{
    fragstat_t *fs;
    itemstat_t *is;
    int i;

    append_fmt("{");
    append_str("name", c->pers.netname);
    append_num("time", (level.framenum - c->resp.enter_framenum) / HZ);
    if (c->resp.score)
        append_num("score", c->resp.score);
    if (c->resp.deaths)
        append_num("deaths", c->resp.deaths);
    if (c->resp.damage_given)
        append_num("damage_given", c->resp.damage_given);
    if (c->resp.damage_recvd)
        append_num("damage_recvd", c->resp.damage_recvd);

    for (i = 0, fs = c->resp.frags; i < FRAG_TOTAL; i++, fs++)
        if (fs->kills || fs->deaths || fs->suicides || fs->atts || fs->hits)
            break;

    if (i < FRAG_TOTAL) {
        append_fmt("\"frags\":[");
        for (i = 0, fs = c->resp.frags; i < FRAG_TOTAL; i++, fs++) {
            if (fs->kills || fs->deaths || fs->suicides || fs->atts || fs->hits) {
                append_fmt("{");
                append_str("name", frag_names[i]);
                if (fs->kills)
                    append_num("kills", fs->kills);
                if (fs->deaths)
                    append_num("deaths", fs->deaths);
                if (fs->suicides)
                    append_num("suicides", fs->suicides);
                if (fs->atts)
                    append_num("atts", fs->atts);
                if (fs->hits)
                    append_num("hits", fs->hits);
                remove_comma();
                append_fmt("},");
            }
        }
        remove_comma();
        append_fmt("],");
    }

    for (i = 0, is = c->resp.items; i < ITEM_TOTAL; i++, is++)
        if (is->pickups || is->misses || is->kills)
            break;

    if (i < ITEM_TOTAL) {
        append_fmt("\"items\":[");
        for (i = 0, is = c->resp.items; i < ITEM_TOTAL; i++, is++) {
            if (is->pickups || is->misses || is->kills) {
                append_fmt("{");
                append_str("name", item_names[i]);
                if (is->pickups)
                    append_num("pickups", is->pickups);
                if (is->misses)
                    append_num("misses", is->misses);
                if (is->kills)
                    append_num("kills", is->kills);
                remove_comma();
                append_fmt("},");
            }
        }
        remove_comma();
        append_fmt("],");
    }

    remove_comma();
    append_fmt("},");
}

static void http_log(gclient_t *c)
{
    int i;

    if (!curl_multi)
        return;

    if (frag_total > 2 * 1024 * 1024)
        return;

    if (!game.clients)
        return;

    if (c) {
        begin_clients();
        log_client(c);
        end_clients();
        return;
    }

    for (i = 0, c = game.clients; i < game.maxclients; i++, c++)
        if (c->pers.connected == CONN_SPAWNED)
            break;
    if (i == game.maxclients)
        return;

    begin_clients();
    for (i = 0, c = game.clients; i < game.maxclients; i++, c++)
        if (c->pers.connected == CONN_SPAWNED)
            log_client(c);
    end_clients();
}

static void http_open(void)
{
    g_http_url = gi.cvar("g_http_url", "", CVAR_LATCH);
    g_http_interval = gi.cvar("g_http_interval", "15", 0);
    g_http_debug = gi.cvar("g_http_debug", "0", 0);
    if (!g_http_url->string[0])
        return;

    curl_global_init(CURL_GLOBAL_NOTHING);

    if (!curl_multi)
        curl_multi = curl_multi_init();

    if (!curl_headers)
        curl_headers = curl_slist_append(NULL, "Content-Type: application/x-json-fragment");

    upload_backoff = 0;
    upload_framenum = 0;
    current_framenum = 0;

    List_Init(&frag_list);
}

static void http_close(void)
{
    List_Init(&frag_list);
    frag_cursor = &frag_list;
    frag_total = 0;
    frag_remaining = 0;

    if (curl_easy) {
        if (curl_multi)
            curl_multi_remove_handle(curl_multi, curl_easy);
        curl_easy_cleanup(curl_easy);
        curl_easy = NULL;
    }

    if (curl_multi) {
        curl_multi_cleanup(curl_multi);
        curl_multi = NULL;
    }

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    }

    curl_handles = 0;
    curl_global_cleanup();

    gi.FreeTags(TAG_HTTP);
}

static void http_run(void)
{
    CURLMcode   ret;
    int         new_count;

    if (!curl_multi)
        return;

    start_upload();

    if (!curl_multi)
        return;

    do {
        ret = curl_multi_perform(curl_multi, &new_count);
        if (new_count < curl_handles)
            finish_upload();
    } while (ret == CURLM_CALL_MULTI_PERFORM);

    if (ret != CURLM_OK) {
        gi.dprintf("[HTTP] Error running uploads: %s\n", curl_multi_strerror(ret));
        http_close();
        return;
    }

    current_framenum++;
}

const database_t g_db_http = {
    .name = "http",
    .open = http_open,
    .close = http_close,
    .run = http_run,
    .log = http_log,
};
