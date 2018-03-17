//
// Compile with: gcc -o udp_logger -O2 -Wall udp_logger.c -lsqlite3
// License: Public Domain
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <endian.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <sqlite3.h>

#ifndef le16toh
#define le16toh(x)  ((uint16_t)(x))
#define le32toh(x)  ((uint32_t)(x))
#define htole64(x)  ((uint64_t)(x))
#endif

#define MAX_PACKETLEN   4096
#define HEADER_LEN      16

static uint8_t buffer[MAX_PACKETLEN];
static int cursize;
static int readcount;

static unsigned recv_timestamp;
static unsigned norm_timestamp;

struct server {
    sqlite3 *db;
    uint32_t last_timestamp;
    uint32_t last_sequence;
    uint64_t cookie;
};

static struct server *servers;

static unsigned long long rowid;
static int numcols;
static int errors;

static int terminate;

static const char schema[] =
"BEGIN TRANSACTION;\n"

"CREATE TABLE IF NOT EXISTS players(\n"
    "netname TEXT PRIMARY KEY,\n"
    "created INT,\n"
    "updated INT\n"
");\n"

"CREATE TABLE IF NOT EXISTS records(\n"
    "player_id INT,\n"
    "date INT,\n"
    "time INT,\n"
    "score INT,\n"
    "deaths INT,\n"
    "damage_given INT,\n"
    "damage_recvd INT\n"
");\n"

"CREATE INDEX IF NOT EXISTS records_idx ON records(player_id,date);\n"

"CREATE TABLE IF NOT EXISTS frags(\n"
    "player_id INT,\n"
    "date INT,\n"
    "frag INT,\n"
    "kills INT,\n"
    "deaths INT,\n"
    "suicides INT,\n"
    "atts INT,\n"
    "hits INT\n"
");\n"

"CREATE INDEX IF NOT EXISTS frags_idx ON frags(player_id,date,frag);\n"

"CREATE TABLE IF NOT EXISTS items(\n"
    "player_id INT,\n"
    "date INT,\n"
    "item INT,\n"
    "pickups INT,\n"
    "misses INT,\n"
    "kills INT\n"
");\n"

"CREATE INDEX IF NOT EXISTS items_idx ON items(player_id,date,item);\n"

"COMMIT;\n";

static int db_query_callback(void *user, int argc, char **argv, char **names)
{
    if (argc > 0)
        rowid = strtoull(argv[0], NULL, 10);

    numcols = argc;
    return 0;
}

static int db_query_execute(sqlite3 *db, sqlite3_callback cb, const char *fmt, ...)
{
    char *sql, *err;
    va_list argptr;
    int ret;

    va_start(argptr, fmt);
    sql = sqlite3_vmprintf(fmt, argptr);
    va_end(argptr);

#ifdef VERBOSE
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned long long start = tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
#endif

    ret = sqlite3_exec(db, sql, cb, NULL, &err);
    if (ret) {
        printf("<3>%s\n", err);
        sqlite3_free(err);
        errors++;
    }

#ifdef VERBOSE
    gettimeofday(&tv, NULL);
    unsigned long long end = tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
    printf("<7>%llu msec: %s\n", end - start, sql);
#endif

    sqlite3_free(sql);
    return ret;
}

#define db_query(db, ...)   db_query_execute(db, db_query_callback, __VA_ARGS__)
#define db_execute(db, ...) db_query_execute(db, NULL, __VA_ARGS__)

static int read_u8(void)
{
    uint8_t v = 0;

    if (readcount <= cursize - 1)
        v = buffer[readcount];

    readcount++;
    return v;
}

static int read_u16(void)
{
    uint16_t v = 0;

    if (readcount <= cursize - 2) {
        memcpy(&v, buffer + readcount, sizeof(v));
        v = le16toh(v);
    }

    readcount += 2;
    return v;
}

static int read_s16(void)
{
    int16_t v = 0;

    if (readcount <= cursize - 2) {
        memcpy(&v, buffer + readcount, sizeof(v));
        v = le16toh(v);
    }

    readcount += 2;
    return v;
}

static char *read_str(void)
{
    static char str[16];
    int i;

    for (i = 0;; i++) {
        if (!(str[i] = read_u8()))
            break;
        if (i == sizeof(str) - 1) {
            str[i] = 0;
            break;
        }
    }

    return str;
}

static void parse(sqlite3 *db)
{
    char *netname    = read_str();
    int time         = read_u16();
    int score        = read_s16();
    int deaths       = read_u16();
    int damage_given = read_u16();
    int damage_recvd = read_u16();

    if (readcount > cursize)
        return;

    numcols = 0;
    if (db_query(db, "SELECT rowid FROM players WHERE netname=%Q", netname))
        return;

    if (!numcols) {
        if (db_execute(db, "INSERT INTO players VALUES(%Q,%u,%u)",
                       netname, recv_timestamp, recv_timestamp))
            return;
        rowid = sqlite3_last_insert_rowid(db);
    }

    db_execute(db, "UPDATE records SET "
               "time=time+%d,"
               "score=score+%d,"
               "deaths=deaths+%d,"
               "damage_given=damage_given+%d,"
               "damage_recvd=damage_recvd+%d "
               "WHERE player_id=%llu AND date=%u",
               time, score, deaths, damage_given, damage_recvd,
               rowid, norm_timestamp);

    if (!sqlite3_changes(db)) {
        db_execute(db, "INSERT INTO records VALUES(%llu,%u,%d,%d,%d,%d,%d)",
                   rowid, norm_timestamp,
                   time, score, deaths, damage_given, damage_recvd);
    }

    while (readcount < cursize) {
        int i = read_u8();
        if (i == 0xff)
            break;

        int v = read_u8();
        if (!v)
            continue;

        int kills    = (v &  1) ? (v & 32) ? read_u16() : read_u8() : 0;
        int deaths   = (v &  2) ? (v & 32) ? read_u16() : read_u8() : 0;
        int suicides = (v &  4) ? (v & 32) ? read_u16() : read_u8() : 0;
        int atts     = (v &  8) ? (v & 64) ? read_u16() : read_u8() : 0;
        int hits     = (v & 16) ? (v & 64) ? read_u16() : read_u8() : 0;

        db_execute(db, "UPDATE frags SET "
                   "kills=kills+%d,"
                   "deaths=deaths+%d,"
                   "suicides=suicides+%d,"
                   "atts=atts+%d,"
                   "hits=hits+%d "
                   "WHERE player_id=%llu AND date=%u AND frag=%d",
                   kills, deaths, suicides, atts, hits,
                   rowid, norm_timestamp, i);

        if (!sqlite3_changes(db)) {
            db_execute(db, "INSERT INTO frags VALUES(%llu,%u,%d,%d,%d,%d,%d,%d)",
                        rowid, norm_timestamp, i,
                        kills, deaths, suicides, atts, hits);
        }
    }

    while (readcount < cursize) {
        int i = read_u8();
        if (i == 0xff)
            break;

        int v = read_u8();
        if (!v)
            continue;

        int pickups = (v & 1) ? (v & 8) ? read_u16() : read_u8() : 0;
        int misses  = (v & 2) ? (v & 8) ? read_u16() : read_u8() : 0;
        int kills   = (v & 4) ? (v & 8) ? read_u16() : read_u8() : 0;

        db_execute(db, "UPDATE items SET "
                   "pickups=pickups+%d,"
                   "misses=misses+%d,"
                   "kills=kills+%d "
                   "WHERE player_id=%llu AND date=%u AND item=%d",
                   pickups, misses, kills,
                   rowid, norm_timestamp, i);

        if (!sqlite3_changes(db)) {
            db_execute(db, "INSERT INTO items VALUES(%llu,%u,%d,%d,%d,%d)",
                        rowid, norm_timestamp, i,
                        pickups, misses, kills);
        }
    }

    db_execute(db, "UPDATE players SET updated=%u WHERE rowid=%llu", recv_timestamp, rowid);
}

static void signal_handler(int sig)
{
    terminate = 1;
}

static time_t normalize_timestamp(time_t t)
{
    struct tm   tm;

    if (!localtime_r(&t, &tm))
        return -1;

    tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
    return mktime(&tm);
}

int main(int argc, char **argv)
{
    char *err;
    int i, nb_servers, sock_fd = -1;
    struct sockaddr_in addr;
    socklen_t addrlen;
    struct server *s;

    if (argc < 3 || !(argc & 1)) {
        printf("<0>Usage: %s <database> <cookie> [...]\n", argv[0]);
        return 1;
    }

    nb_servers = (argc - 1) / 2;
    servers = calloc(nb_servers, sizeof(*servers));
    if (!servers) {
        printf("<0>Couldn't allocate memory\n");
        return 1;
    }

    for (i = 0, s = servers; i < nb_servers; i++, s++) {
        if (sqlite3_open(argv[1+i*2], &s->db)) {
            printf("<0>Couldn't open database: %s", sqlite3_errmsg(s->db));
            goto fail;
        }

        if (sqlite3_exec(s->db, schema, NULL, NULL, &err)) {
            printf("<0>Couldn't create database schema: %s\n", err);
            sqlite3_free(err);
            goto fail;
        }

        s->cookie = htole64(strtoull(argv[2+i*2], NULL, 0));
    }

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd == -1) {
        printf("<0>Couldn't open socket: %s\n", strerror(errno));
        goto fail;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(27999);

    if (bind(sock_fd, (const struct sockaddr *)&addr, sizeof(addr))) {
        printf("<0>Couldn't bind socket: %s\n", strerror(errno));
        goto fail;
    }

    struct sigaction act = { .sa_handler = signal_handler };
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    setlinebuf(stdout);
    printf("<6>Successfully opened %d database%s\n", nb_servers, nb_servers == 1 ? "" : "s");

    while (!terminate) {
        uint32_t sequence, timestamp;
        uint64_t cookie;
        ssize_t ret;

        addrlen = sizeof(addr);
        ret = recvfrom(sock_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, &addrlen);
        if (ret < HEADER_LEN)
            continue;

        memcpy(&sequence,  buffer + 0, 4);
        memcpy(&timestamp, buffer + 4, 4);
        memcpy(&cookie,    buffer + 8, 8);

        for (i = 0, s = servers; i < nb_servers; i++, s++)
            if (cookie == s->cookie)
                break;
        if (i == nb_servers)
            continue;

        sequence  = le32toh(sequence);
        timestamp = le32toh(timestamp);

#ifdef VERBOSE
        char temp[INET_ADDRSTRLEN];
        printf("<7>Packet from %s:%d size %zd seq %u ts %u\n",
               inet_ntop(AF_INET, &addr.sin_addr, temp, sizeof(temp)),
               ntohs(addr.sin_port), ret, sequence, timestamp);
#endif
        if (sequence == s->last_sequence && timestamp == s->last_timestamp) {
            printf("<6>Resending lost acknowledgement\n");
            goto echo;
        }

        if (sequence <= s->last_sequence) {
            if (s->last_timestamp < timestamp) {
                printf("<5>Suspected server restart\n");
            } else if (s->last_sequence - sequence > 1000) {
                printf("<5>Too big sequence delta\n");
            } else {
                printf("<7>Ignoring out-of-order packet\n");
                continue;
            }
        } else if (s->last_sequence && sequence > s->last_sequence + 1) {
            printf("<4>Lost %u packets\n", sequence - s->last_sequence - 1);
        }

        recv_timestamp = timestamp;
        norm_timestamp = normalize_timestamp(timestamp);

        cursize   = ret;
        readcount = HEADER_LEN;

        errors = 0;
        if (db_execute(s->db, "BEGIN TRANSACTION"))
            continue;

        while (readcount < cursize && !errors)
            parse(s->db);

        if (db_execute(s->db, errors ? "ROLLBACK" : "COMMIT") || errors)
            continue;

        s->last_sequence  = sequence;
        s->last_timestamp = timestamp;
echo:
        ret = sendto(sock_fd, buffer, HEADER_LEN, 0, (const struct sockaddr *)&addr, sizeof(addr));
        if (ret < 0)
            printf("<3>Error sending packet: %s\n", strerror(errno));
    }

fail:
    if (sock_fd != -1)
        close(sock_fd);
    for (i = 0, s = servers; i < nb_servers; i++, s++)
        sqlite3_close(s->db);
    free(servers);
    return !terminate;
}
