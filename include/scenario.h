#ifndef YPHZ_SCENARIO_H
#define YPHZ_SCENARIO_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_OFFICERS 401
#define MAX_CASTLES  39
#define MAX_SCENARIOS 4
#define MAX_FACTIONS 15

// ---------------------------------------------------------------------------
// DOS / PC-98 16-bit compact memory layout: byte-level packing, no compiler
// padding. These records are parsed out of the original little-endian DOS
// images and must mirror the on-disk layout byte-for-byte.
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

// 15 Historical Daimyos / Factions in Genpei Kassen
typedef struct {
    int id;
    const char *name_sjis;
    const char *flag_sjis;
    uint8_t flag_color; // PC-98 16-color index
} FactionDef;

// Officer structure parsed from Sndata.gp (71-byte record)
typedef struct {
    int id;
    char family_sjis[16];
    char given_sjis[16];
    char full_name_sjis[32];
    char family_kana[16];
    char given_kana[16];
    int face_id;        // Unique portrait ID (0..89) in Kaodata.gp, or -1 if generic
    int age;            // East Asian reckoning age
    int lead;           // 統率 (Leadership / Combat command)
    int war;            // 武力 (Martial prowess)
    int intel;          // 知謀 (Intellect / Strategy)
    int politics;       // 政治 (Politics / Administration)
    int charm;          // 魅力 (Charisma)
    int loyalty;        // 忠誠 (Loyalty 0..100)
    int castle_id;      // Stationed castle (0..38, or -1)
    int faction_id;     // Affiliated clan (0..14)
    int troops;         // Personal troop count
    bool is_active;     // True if alive and active in scenario
} ScenarioOfficer;

// Castle structure parsed from Sndata.gp (76-byte record)
typedef struct {
    int id;
    char name_sjis[16];
    char kana[16];
    int map_x;          // X coordinate on 1696x360 strategic map
    int map_y;          // Y coordinate on 1696x360 strategic map
    int lord_officer_id;// Castle Governor / Lord officer ID
    int faction_id;     // Owning clan ID (0..14)
    int circuit_id;     // Historical circuit (0..7: 九州, 四国, 山陽, 畿内, 北陸, 東海, 関東, 奥羽)
    int rice;           // Kokudaka / Harvest income
    int gold;           // Treasury / Funds
    int troops;         // Garrison troop strength
    int defense;        // Castle defense / fortifications (0..1000)
    int morale;         // Garrison morale (0..100)
    int training;       // Garrison training level (0..100)
    int node_id;        // Node ID on highway graph
    int num_conns;      // Number of adjacent road/sea connections
    int conns[8];       // Adjacent castle IDs
} ScenarioCastle;

// Complete historical scenario
typedef struct {
    int scenario_idx;   // 0..3
    int year;           // 1180, 1183, 1184, 1185
    int month;          // 9, 6, 9, 9
    const char *title_sjis;
    ScenarioCastle castles[MAX_CASTLES];
    int num_castles;
    ScenarioOfficer officers[MAX_OFFICERS];
    int num_officers;
} Scenario;

#pragma pack(pop)

// Load scenario 0..3 from Sndata.gp
bool scenario_load(const char *sndata_path, int scenario_idx, Scenario *out_sc);

// Get faction definitions
const FactionDef* scenario_get_faction(int faction_id);

// Get circuit / province region name
const char* scenario_get_circuit_name(int circuit_id);

// Static metadata for the opening scenario-select screen.
const char* scenario_get_title(int scenario_idx);
int scenario_get_year(int scenario_idx);
int scenario_get_month(int scenario_idx);

// Faction leader (当主): the strongest castle lord of that faction, since the
// original scenario records carry no explicit "daimyo" field. Returns the
// officer id, or -1 when the faction holds no castles/officers.
int scenario_faction_leader(const Scenario *sc, int faction_id);

// ---------------------------------------------------------------------------
// 子任务 9: strategic road network.
//
// Sndata.gp already stores the 39-castle adjacency table inside each 76-byte
// castle record: eight little-endian u16 slots at offset 16, where slot 0 is a
// reserved flag and slots 1..7 hold neighbouring castle ids (0xFFFF = empty).
// The graph is symmetric, has 72 undirected edges and is fully connected.
// ---------------------------------------------------------------------------

// True when `a` and `b` are directly joined by a road/sea lane.
bool scenario_are_adjacent(const Scenario *sc, int a, int b);

// Number of undirected edges in the road network.
int scenario_road_edge_count(const Scenario *sc);

#endif // YPHZ_SCENARIO_H
