/*
Copyright (C) 2009 Andrey Nazarov

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
#include <sqlite3.h>

static sqlite3 *db;

static unsigned long long rowid;
static unsigned long updated;
static int numcols;

static int db_query_callback( void *user, int argc, char **argv, char **names ) {
    if( argc > 0 ) {
        rowid = strtoull( argv[0], NULL, 10 );
        if( argc > 1 ) {
            updated = strtoul( argv[1], NULL, 10 );
        }
    }
    numcols = argc;

    return 0;
}

static int db_query( const char *fmt, ... ) {
    char *sql, *err;
    va_list argptr;
    int ret;

    va_start( argptr, fmt );
    sql = sqlite3_vmprintf( fmt, argptr );
    va_end( argptr );

    ret = sqlite3_exec( db, sql, db_query_callback, NULL, &err );
    if( ret ) {
        Com_EPrintf( "%s: %s\n", __func__, err );
        sqlite3_free( err );
    }
    sqlite3_free( sql );
    return ret;
}

static int db_execute( const char *fmt, ... ) {
    char *sql, *err;
    va_list argptr;
    int ret;

    va_start( argptr, fmt );
    sql = sqlite3_vmprintf( fmt, argptr );
    va_end( argptr );

    ret = sqlite3_exec( db, sql, NULL, NULL, &err );
    if( ret ) {
        Com_EPrintf( "%s: %s\n", __func__, err );
        sqlite3_free( err );
    }
    sqlite3_free( sql );
    return ret;
}

void G_BeginLogging( void ) {
    if( db ) {
        db_execute( "BEGIN TRANSACTION" );
    }
}

void G_EndLogging( void ) {
    if( db ) {
        db_execute( "COMMIT" );
    }
}

void G_LogClient( gclient_t *c ) {
    unsigned long clock;
    fragstat_t *fs;
    itemstat_t *is;
    int i, ret;

    if( !db ) {
        return;
    }

    clock = time( NULL );

    numcols = 0;
    ret = db_query( "SELECT rowid,updated FROM players WHERE netname=%Q", c->pers.netname );
    if( ret ) {
        return;
    }

    if( numcols > 1 ) {
        if( clock <= updated ) {
            return;
        }
        //gi.dprintf( "found player_id=%llu\n", rowid );
    } else {
        ret = db_execute( "INSERT INTO players VALUES(%Q,%lu,%lu)",
            c->pers.netname, clock, clock );
        if( ret ) {
            return;
        }
        rowid = sqlite3_last_insert_rowid( db );
        //gi.dprintf( "created player_id=%llu\n", rowid );
    }

    // MapChange \current\%s\next\%s\players\%d
    // PlayerBegin \name\%s\id\%s
    // PlayerEnd \name\%s\id\%s
    // PlayerStats \name\%s\id\%s\time\%d\frg\%d\dth\%d\dmg\%d\dmr\%d\w%d\%d,%d,%d,%d,%d\i%d\%d,%d,%d
    db_execute( "INSERT INTO records VALUES(%llu,%lu,%d,%d,%d,%d,%d)",
        rowid, clock, ( level.framenum - c->resp.enter_framenum ) / HZ,
        c->resp.score, c->resp.deaths, c->resp.damage_given, c->resp.damage_recvd );

    for( i = 0; i < FRAG_TOTAL; i++ ) {
        fs = &c->resp.frags[i];
        if( fs->kills || fs->deaths || fs->suicides || fs->atts || fs->hits ) {
            db_execute( "INSERT INTO frags VALUES(%llu,%lu,%d,%d,%d,%d,%d,%d)",
                rowid, clock, i, fs->kills, fs->deaths, fs->suicides, fs->atts, fs->hits );
        }
    }

    for( i = 0; i < ITEM_TOTAL; i++ ) {
        is = &c->resp.items[i];
        if( is->pickups || is->misses || is->kills ) {
            db_execute( "INSERT INTO items VALUES(%llu,%lu,%d,%d,%d,%d)",
                rowid, clock, i, is->pickups, is->misses, is->kills );
        }
    }

    db_execute( "UPDATE players SET updated=%lu WHERE rowid=%llu", clock, rowid );
}

void G_LogClients( void ) {
    gclient_t *c;
    int i;

    G_BeginLogging();
    for( i = 0, c = game.clients; i < game.maxclients; i++, c++ ) {
        if( c->pers.connected == CONN_SPAWNED ) {
            G_LogClient( c );
        }
    }
    G_EndLogging();
}

static const char schema[] =
"BEGIN TRANSACTION;\n"

"CREATE TABLE IF NOT EXISTS players(\n"
    "netname TEXT PRIMARY KEY,\n"
    "created INT,\n"
    "updated INT\n"
");\n"

"CREATE TABLE IF NOT EXISTS records(\n"
    "player_id INT,\n"
    "clock INT,\n"
    "time INT,\n"
    "score INT,\n"
    "deaths INT,\n"
    "damage_given INT,\n"
    "damage_recvd INT\n"
");\n"

"CREATE INDEX IF NOT EXISTS records_idx ON records(player_id,clock);\n"

"CREATE TABLE IF NOT EXISTS frags(\n"
    "player_id INT,\n"
    "clock INT,\n"
    "frag INT,\n"
    "kills INT,\n"
    "deaths INT,\n"
    "suicides INT,\n"
    "atts INT,\n"
    "hits INT\n"
");\n"

"CREATE INDEX IF NOT EXISTS frags_idx ON frags(player_id,clock);\n"

"CREATE TABLE IF NOT EXISTS items(\n"
    "player_id INT,\n"
    "clock INT,\n"
    "item INT,\n"
    "pickups INT,\n"
    "misses INT,\n"
    "kills INT\n"
");\n"

"CREATE INDEX IF NOT EXISTS items_idx ON items(player_id,clock);\n"

"COMMIT;\n";

qboolean G_OpenDatabase( void ) {
    char buffer[MAX_OSPATH];
    size_t len;
    char *err;
    int ret;

    if( db ) {
        return qtrue;
    }

    if( !game.dir[0] ) {
        return qfalse;
    }

    if( !g_sql_database->string[0] ) {
        return qfalse;
    }

    len = Q_snprintf( buffer, sizeof( buffer ), "%s/%s.db",
        game.dir, g_sql_database->string  );
    if( len >= sizeof( buffer ) ) {
        return qfalse;
    }

    ret = sqlite3_open( buffer, &db );
    if( ret ) {
        Com_EPrintf( "Couldn't open SQLite database: %s", sqlite3_errmsg( db ) );
        goto fail;
    }

    if( (int)g_sql_async->value ) {
        ret = db_execute( "PRAGMA synchronous=OFF" );
        if( ret ) {
            goto fail;
        }
    }

    ret = sqlite3_exec( db, schema, NULL, NULL, &err );
    if( ret ) {
        Com_EPrintf( "Couldn't create SQLite database schema: %s\n", err );
        sqlite3_free( err );
        goto fail;
    }

    gi.dprintf( "Logging to SQLite database '%s'\n", buffer );

    return qtrue;

fail:
    sqlite3_close( db );
    db = NULL;
    return qfalse;
}

void G_CloseDatabase( void ) {
    if( db ) {
        gi.dprintf( "Closing SQLite database\n" );
        sqlite3_close( db );
        db = NULL;
    }
}

