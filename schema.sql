BEGIN TRANSACTION;

CREATE TABLE players(
    netname TEXT PRIMARY KEY, -- player name (max 15 chars)
    created INT, -- UNIX time player was created
    updated INT -- UNIX time player was updated for the last time
);

-- generic player stats
CREATE TABLE records(
    player_id INT,  -- REFERENCES players(rowid)
    date INT,       -- UNIX time of this update
    time INT,       -- seconds in game
    score INT,
    deaths INT,
    damage_given INT,
    damage_recvd INT
);

CREATE INDEX records_idx ON records(player_id,date);

/*
typedef enum {
    FRAG_UNKNOWN,       // catches misc suicide types
    FRAG_BLASTER,
    FRAG_SHOTGUN,
    FRAG_SUPERSHOTGUN,
    FRAG_MACHINEGUN,
    FRAG_CHAINGUN,
    FRAG_GRENADES,
    FRAG_GRENADELAUNCHER,
    FRAG_ROCKETLAUNCHER,
    FRAG_HYPERBLASTER,
    FRAG_RAILGUN,
    FRAG_BFG,
    FRAG_TELEPORT,
    FRAG_WATER,
    FRAG_SLIME,
    FRAG_LAVA,
    FRAG_CRUSH,
    FRAG_FALLING,
    FRAG_SUICIDE,
    FRAG_TOTAL          // not used
} frag_t;
*/

-- weapons, suicides, etc
CREATE TABLE frags(
    player_id INT,  -- REFERENCES players(rowid)
    date INT,       -- UNIX time of this update
    frag INT,       -- frag type (one of frag_t constants above)
    kills INT,
    deaths INT,
    suicides INT,
    atts INT,       -- shots fired
    hits INT        -- shots hit
);

CREATE INDEX frags_idx ON frags(player_id,date,frag);

/*
typedef enum {
    ITEM_NULL,          // not used

    ITEM_ARMOR_BODY,
    ITEM_ARMOR_COMBAT,
    ITEM_ARMOR_JACKET,
    ITEM_ARMOR_SHARD,   // not used
    ITEM_POWER_SCREEN,
    ITEM_POWER_SHIELD,

    ITEM_BLASTER,       // not used
    
    // counted only when weapon stay is off
    ITEM_SHOTGUN,
    ITEM_SUPERSHOTGUN,
    ITEM_MACHINEGUN,
    ITEM_CHAINGUN,
    ITEM_GRENADES,
    ITEM_GRENADELAUNCHER,
    ITEM_ROCKETLAUNCHER,
    ITEM_HYPERBLASTER,
    ITEM_RAILGUN,
    ITEM_BFG,

    // not used
    ITEM_SHELLS,
    ITEM_BULLETS,
    ITEM_CELLS,
    ITEM_ROCKETS,
    ITEM_SLUGS,

    ITEM_QUAD,
    ITEM_INVULNERABILITY,
    ITEM_SILENCER,
    ITEM_BREATHER,
    ITEM_ENVIRO,
    ITEM_ANCIENT_HEAD,
    ITEM_ADRENALINE,
    ITEM_BANDOLIER,
    ITEM_PACK,
    ITEM_HEALTH,        // counts megahealth only

    ITEM_TOTAL          // not used
} item_t;
*/

-- picked up items
CREATE TABLE items(
    player_id INT,  -- REFERENCES players(rowid)
    date INT,       -- UNIX time of this update
    item INT,       -- item type (one of item_t constants above)
    pickups INT,    -- number of items of the given type this player picked up
    misses INT,     -- number of items of the given type other players picked up
                    -- while this player was in the game
    kills INT       -- counted only for quad, pent, megahealth and (power)armor
);

CREATE INDEX items_idx ON items(player_id,date,item);

COMMIT;
