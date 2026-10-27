#ifndef YPHZ_SCENARIO_H
#define YPHZ_SCENARIO_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_OFFICERS 401
#define MAX_CASTLES  39
#define MAX_SCENARIOS 4
#define MAX_FACTIONS 15
// Original castle +0x34 morale setter clamps to 999 (see
// docs/MAIN_EXE_ARCHITECTURE.md §3.2), not 100.
#define MAX_CASTLE_MORALE 999

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
    int morale;         // Garrison morale (0..MAX_CASTLE_MORALE)
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


// ---------------------------------------------------------------------------
// V9: original binary accessors and clamp rules.
//
// Runtime addresses are the real-mode 43B3 group (static 4000:xxxx maps with
// L_run = static + 0x40BEC).  Only rules re-verified against the runtime image
// are exposed here; names retain the function offsets so the evidence can be
// audited in docs/MAIN_EXE_ARCHITECTURE.md and re_out/v9/trace.json.
// ---------------------------------------------------------------------------

// 43B3 group / static 4000:5362: gold = min(rice, value), floor 0.
void castle_set_gold_bounded(ScenarioCastle *c, int value);
// static 4000:5396: gold = min(rice, max(0, gold - amount)).
void castle_sub_gold(ScenarioCastle *c, int amount);
// static 4000:53c4: only when rice/5 <= gold:
//   gold = min(rice, max(rice/5, (gold * param) / 100)).
// The 597e helper is max (596e is min), so this is the exact branch order.
void castle_calc_53c4(ScenarioCastle *c, int param);

// static 4000:5436 / 5460 / 5492: troops clamped to 0..999.
void castle_set_troops(ScenarioCastle *c, int value);
void castle_add_troops(ScenarioCastle *c, int delta);
void castle_sub_troops(ScenarioCastle *c, int amount);
// static 4000:54d6 / 5500 / 5530: training clamped to 0..100.
void castle_set_training(ScenarioCastle *c, int value);
void castle_add_training(ScenarioCastle *c, int delta);
void castle_sub_training(ScenarioCastle *c, int amount);
// static 4000:558a / 55b4 / 55ea: morale 0..999; 55b4 additionally caps the
// increase at min(defense, 999).
void castle_set_morale(ScenarioCastle *c, int value);
void castle_add_morale_capped_by_def(ScenarioCastle *c, int delta);
void castle_sub_morale(ScenarioCastle *c, int amount);

// 4000:1887 / 185b / 1871.  The original compares two raw u16 words at
// officer record +0x1a and +0x1c crosswise:
//   b_1a == a_1c || a_1a == b_1c.
// Those two words are runtime fields, not part of the Sndata.gp save record,
// so the API takes them explicitly instead of inventing struct fields.
bool officer_same_location_1887(uint16_t a_1a, uint16_t a_1c,
                                uint16_t b_1a, uint16_t b_1c);

// 4000:1957 / 1977 / 1999: raw officer +0x21 status bits.
uint16_t officer_status_bits_set_1957(uint16_t bits, uint16_t mask);
uint16_t officer_status_bits_clear_1977(uint16_t bits, uint16_t mask);
bool officer_status_bits_test_1999(uint16_t bits, uint16_t mask);

// Semantic engine-side ports used by the turn/AI code.
int  officer_count_at_castle_5e1a(const Scenario *sc, int castle_id);
bool officer_same_castle(const ScenarioOfficer *a, const ScenarioOfficer *b);
bool castle_adjacent_to_faction(const Scenario *sc, int castle_id,
                                int faction_id);

// ---------------------------------------------------------------------------
// V10: strategic distance / nearest-castle helpers (43B3 group).
//
// These are static ports of the original routines below.  The runtime image
// addresses are the real-mode CS:IP values; the Ghidra/static address is the
// same code with IP + 0x2F44 (e.g. 43B3:301C == 4000:5F60).
//
//   scenario_castle_distance          43B3:301C / 4000:5F60
//   scenario_castle_distance_matrix   43B3:30C2 / 4000:6006
//   scenario_nearest_friendly_castle  43B3:3114 / 4000:6058
//   scenario_nearest_enemy_castle     43B3:3158 / 4000:609C
//
// The original graph edges are castle record +0x12..+0x1e (seven u16 slots);
// Sndata.gp slot 0 at +0x10 is a reserved flag, not a neighbour.  The parsed
// ScenarioCastle.conns[] already holds exactly those seven slots.
//
// Confidence: B (runtime disassembly verified, no DOSBox-X dynamic trace).
// The BFS frontier order and the 200-level limit follow the original; see
// report_summary_v10.md for the instruction-level evidence.
// ---------------------------------------------------------------------------

#define CASTLE_DIST_UNREACHABLE 0xFF

// Exact port of 4000:5F60.  Returns 0 for the same castle, the BFS depth for
// a reachable castle, and CASTLE_DIST_UNREACHABLE when the graph is exhausted
// (the original also returns 0xFF past depth 200).  Invalid ids return
// CASTLE_DIST_UNREACHABLE; the original has no unified bounds check.
int scenario_castle_distance(const Scenario *sc, int from, int to);

// Exact port of 4000:6006.  Fills out[row * out_cols + col] with the byte
// distance matrix.  out_rows/out_cols must both be >= sc->num_castles; the
// original always writes a 39x39 byte matrix.
bool scenario_castle_distance_matrix(const Scenario *sc, uint8_t *out,
                                     int out_rows, int out_cols);

// Exact port of 4000:6058.  Argument order follows the original stack frame:
// [bp+6] = faction id, [bp+8] = source castle.  Scans castles 0..38, keeps the
// first castle of `faction` with the smallest matrix[castle][source] byte and
// skips unreachable (0xFF) entries.  Returns -1 when no castle matches.
int scenario_nearest_friendly_castle(const Scenario *sc, int faction,
                                     int source_castle);

// Exact port of 4000:609C.  [bp+6] = faction id, [bp+8] = source castle.
// Skips castles with faction_id == -1 (the original's 0xFFFF) and castles of
// the given faction; keeps the first minimum matrix[castle][source] byte.
int scenario_nearest_enemy_castle(const Scenario *sc, int faction,
                                  int source_castle);

#endif // YPHZ_SCENARIO_H
