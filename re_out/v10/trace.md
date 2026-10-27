# V10 strategic trace

Runtime MD5: `649725c7263606abbc93fef0923f96be`
Main.exe MD5: `7e7e1b875aa3b07eeb28c2eb5950aac1`

## Mapping

43B3 labels use linear = 0x43B30 + IP.  The 0x226xx/0x228xx page's real CS is not stored in the dump; 2285:0006 from trace_v9 is linear/16 arithmetic, not a confirmed CS:IP.

## Function cluster

| name | linear | runtime label | static | insns | calls | global refs |
|---|---:|---|---|---:|---:|---:|
| `ctx_setup_226e0` | `226E0` | `linear 0x226E0` | `-` | 83 | 14 | 12 |
| `select_22856` | `22856` | `linear 0x22856` | `-` | 133 | 11 | 5 |
| `exec_22f60` | `22F60` | `linear 0x22F60` | `-` | 100 | 13 | 2 |
| `dispatch_2303c` | `2303C` | `linear 0x2303C` | `-` | 139 | 15 | 0 |
| `policy_morale_23196` | `23196` | `linear 0x23196` | `-` | 41 | 5 | 0 |
| `policy_troops_231ea` | `231EA` | `linear 0x231EA` | `-` | 48 | 6 | 0 |
| `policy_train_23250` | `23250` | `linear 0x23250` | `-` | 76 | 13 | 0 |
| `policy_train_gate_23304` | `23304` | `linear 0x23304` | `-` | 52 | 7 | 0 |
| `policy_recruit_2337e` | `2337E` | `linear 0x2337E` | `-` | 111 | 13 | 2 |
| `policy_resource_2348e` | `2348E` | `linear 0x2348E` | `-` | 54 | 5 | 0 |
| `route_enum_2f6ae` | `2F6AE` | `linear 0x2F6AE` | `-` | 35 | 1 | 0 |
| `value_71b8` | `30188` | `linear 0x30188` | `-` | 104 | 12 | 0 |
| `edge_cost_30280` | `30280` | `linear 0x30280` | `-` | 29 | 2 | 0 |
| `path_cost_a_302c4` | `302C4` | `linear 0x302C4` | `-` | 27 | 3 | 0 |
| `path_cost_b_302f2` | `302F2` | `linear 0x302F2` | `-` | 27 | 3 | 0 |
| `route_cost_7350` | `30320` | `linear 0x30320` | `-` | 45 | 3 | 0 |
| `two_step_752c` | `304FC` | `linear 0x304FC` | `-` | 63 | 3 | 2 |
| `distance_5f60` | `46B4C` | `43B3:301C` | `4000:5F60` | 68 | 2 | 0 |
| `matrix_6006` | `46BF2` | `43B3:30C2` | `4000:6006` | 27 | 1 | 0 |
| `nearest_own_6058` | `46C44` | `43B3:3114` | `4000:6058` | 32 | 2 | 0 |
| `nearest_enemy_609c` | `46C88` | `43B3:3158` | `4000:609C` | 38 | 2 | 0 |
| `adjacency_20c6` | `45BF6` | `43B3:20C6` | `4000:500A` | 21 | 1 | 0 |
| `owner_21a8` | `45CD8` | `43B3:21A8` | `4000:50EC` | 12 | 1 | 0 |
| `unowned_221e` | `45D4E` | `43B3:221E` | `4000:5162` | 14 | 1 | 0 |
| `officers_2ed6` | `46A06` | `43B3:2ED6` | `4000:5E1A` | 24 | 2 | 0 |
| `matrix_read_30fe` | `46C2E` | `43B3:30FE` | `4000:6042` | 8 | 0 | 0 |

## Call graph

| caller | site | kind | target |
|---|---|---|---|
| `ctx_setup_226e0` | `226EB` | far | `43B3:21A8` |
| `ctx_setup_226e0` | `22705` | far | `28FD:752C` |
| `ctx_setup_226e0` | `22716` | far | `43B3:3158` |
| `ctx_setup_226e0` | `22724` | far | `28FD:71B8` |
| `ctx_setup_226e0` | `22736` | far | `28FD:7350` |
| `ctx_setup_226e0` | `22746` | far | `3B1A:5BC8` |
| `ctx_setup_226e0` | `22756` | far | `18A6:3D86` |
| `ctx_setup_226e0` | `22762` | far | `18A6:3D54` |
| `ctx_setup_226e0` | `2276E` | far | `18A6:2DBA` |
| `ctx_setup_226e0` | `2277A` | far | `18A6:3856` |
| `ctx_setup_226e0` | `22786` | far | `43B3:1C18` |
| `ctx_setup_226e0` | `22793` | far | `43B3:1CA2` |
| `ctx_setup_226e0` | `227A2` | near | `linear 0x221D4` |
| `ctx_setup_226e0` | `227AC` | near | `linear 0x225AE` |
| `select_22856` | `22861` | far | `43B3:2ED6` |
| `select_22856` | `228A2` | far | `43B3:30FE` |
| `select_22856` | `228B4` | far | `43B3:30FE` |
| `select_22856` | `228F4` | far | `43B3:20C6` |
| `select_22856` | `22907` | far | `43B3:21A8` |
| `select_22856` | `22913` | far | `43B3:221E` |
| `select_22856` | `2291E` | far | `43B3:2ED6` |
| `select_22856` | `2292A` | far | `43B3:30FE` |
| `select_22856` | `2293E` | far | `28FD:7350` |
| `select_22856` | `22948` | far | `28FD:71B8` |
| `select_22856` | `2294F` | far | `3B1A:5BC8` |
| `exec_22f60` | `22F72` | far | `3B1A:5C02` |
| `exec_22f60` | `22F86` | far | `3B1A:5C1C` |
| `exec_22f60` | `22F90` | far | `43B3:2238` |
| `exec_22f60` | `22F97` | far | `3B1A:7F9A` |
| `exec_22f60` | `22FA5` | far | `43B3:2ED6` |
| `exec_22f60` | `22FB5` | far | `28FD:6DD0` |
| `exec_22f60` | `22FC8` | far | `3B1A:5C02` |
| `exec_22f60` | `22FD5` | near | `linear 0x22856` |
| `exec_22f60` | `22FE5` | far | `43B3:29F2` |
| `exec_22f60` | `22FF6` | far | `43B3:26D4` |
| `exec_22f60` | `23007` | far | `43B3:2774` |
| `exec_22f60` | `2301E` | near | `linear 0x22D82` |
| `exec_22f60` | `2302A` | near | `linear 0x227BA` |
| `dispatch_2303c` | `2304D` | near | `linear 0x227BA` |
| `dispatch_2303c` | `2305E` | far | `43B3:26D4` |
| `dispatch_2303c` | `2307E` | far | `28FD:6DD0` |
| `dispatch_2303c` | `23095` | far | `3B1A:596E` |
| `dispatch_2303c` | `230A1` | far | `3B1A:596E` |
| `dispatch_2303c` | `230CA` | far | `43B3:15C8` |
| `dispatch_2303c` | `230EB` | far | `43B3:15C8` |
| `dispatch_2303c` | `230F4` | far | `3B1A:5C1C` |
| `dispatch_2303c` | `23129` | far | `31CA:044A` |
| `dispatch_2303c` | `23139` | far | `31CA:0458` |
| `dispatch_2303c` | `23146` | far | `31CA:0474` |
| `dispatch_2303c` | `23157` | far | `31CA:0FFC` |
| `dispatch_2303c` | `23168` | far | `31CA:100A` |
| `dispatch_2303c` | `23179` | far | `31CA:145A` |
| `dispatch_2303c` | `23189` | near | `linear 0x227BA` |
| `policy_morale_23196` | `2319E` | far | `43B3:2630` |
| `policy_morale_23196` | `231A9` | far | `43B3:261A` |
| `policy_morale_23196` | `231B5` | far | `3B1A:5C1C` |
| `policy_morale_23196` | `231C4` | far | `3B1A:5C02` |
| `policy_morale_23196` | `231D9` | near | `linear 0x2303C` |
| `policy_troops_231ea` | `231F3` | far | `43B3:21A8` |
| `policy_troops_231ea` | `231FA` | far | `43B3:18BA` |
| `policy_troops_231ea` | `2320C` | far | `43B3:24DC` |
| `policy_troops_231ea` | `23218` | far | `3B1A:5C1C` |
| `policy_troops_231ea` | `2322A` | far | `3B1A:5C02` |
| `policy_troops_231ea` | `23240` | near | `linear 0x2303C` |
| `policy_train_23250` | `2325C` | far | `43B3:2238` |
| `policy_train_23250` | `23266` | far | `43B3:0FA2` |
| `policy_train_23250` | `23281` | far | `43B3:0A6C` |
| `policy_train_23250` | `23288` | far | `3B1A:597E` |
| `policy_train_23250` | `23293` | far | `43B3:257C` |
| `policy_train_23250` | `2329E` | far | `3B1A:5B0C` |
| `policy_train_23250` | `232AE` | far | `3B1A:5A0E` |
| `policy_train_23250` | `232B8` | far | `43B3:26D4` |
| `policy_train_23250` | `232BF` | far | `3B1A:596E` |
| `policy_train_23250` | `232CB` | far | `43B3:26D4` |
| `policy_train_23250` | `232DC` | far | `3B1A:596E` |
| `policy_train_23250` | `232E9` | far | `31CA:05A8` |
| `policy_train_23250` | `232F7` | near | `linear 0x227BA` |
| `policy_train_gate_23304` | `2330D` | far | `43B3:21A8` |
| `policy_train_gate_23304` | `23319` | far | `43B3:257C` |
| `policy_train_gate_23304` | `23326` | far | `3B1A:5C1C` |
| `policy_train_gate_23304` | `2333C` | far | `3B1A:5C02` |
| `policy_train_gate_23304` | `23349` | far | `43B3:18BA` |
| `policy_train_gate_23304` | `2335B` | near | `linear 0x23250` |
| `policy_train_gate_23304` | `2336F` | near | `linear 0x2303C` |
| `policy_recruit_2337e` | `23391` | far | `43B3:15C8` |
| `policy_recruit_2337e` | `233AF` | far | `43B3:26D4` |
| `policy_recruit_2337e` | `233BA` | far | `43B3:2774` |
| `policy_recruit_2337e` | `233E3` | far | `31CA:0016` |
| `policy_recruit_2337e` | `233FF` | far | `31CA:0016` |
| `policy_recruit_2337e` | `23412` | far | `3B1A:596E` |
| `policy_recruit_2337e` | `2341C` | far | `3B1A:596E` |
| `policy_recruit_2337e` | `2342D` | far | `31CA:0068` |
| `policy_recruit_2337e` | `23443` | far | `31CA:0016` |
| `policy_recruit_2337e` | `23456` | far | `3B1A:596E` |
| `policy_recruit_2337e` | `23460` | far | `3B1A:596E` |
| `policy_recruit_2337e` | `23471` | far | `31CA:0046` |
| `policy_recruit_2337e` | `23481` | near | `linear 0x227BA` |
| `policy_resource_2348e` | `23497` | far | `43B3:2920` |
| `policy_resource_2348e` | `234A3` | far | `3B1A:5C1C` |
| `policy_resource_2348e` | `234B5` | far | `3B1A:5C02` |
| `policy_resource_2348e` | `234D0` | far | `28FD:63C6` |
| `policy_resource_2348e` | `234E9` | near | `linear 0x2303C` |
| `route_enum_2f6ae` | `2F6C1` | near | `linear 0x2F5D2` |
| `value_71b8` | `301A3` | near | `linear 0x2F530` |
| `value_71b8` | `301C2` | far | `43B3:15C8` |
| `value_71b8` | `301CF` | far | `3B1A:596E` |
| `value_71b8` | `301F8` | far | `3B1A:596E` |
| `value_71b8` | `30209` | near | `linear 0x300C0` |
| `value_71b8` | `30217` | far | `43B3:2814` |
| `value_71b8` | `3021E` | far | `3B1A:596E` |
| `value_71b8` | `30230` | far | `43B3:2B2C` |
| `value_71b8` | `3023B` | far | `3B1A:5AF8` |
| `value_71b8` | `3024A` | far | `43B3:2920` |
| `value_71b8` | `30254` | far | `3B1A:5A0E` |
| `value_71b8` | `30272` | far | `3B1A:5AF8` |
| `edge_cost_30280` | `3029D` | far | `43B3:1CA2` |
| `edge_cost_30280` | `302AE` | far | `3B1A:5A38` |
| `path_cost_a_302c4` | `302CF` | near | `linear 0x30120` |
| `path_cost_a_302c4` | `302D7` | far | `43B3:34EC` |
| `path_cost_a_302c4` | `302E6` | near | `linear 0x30280` |
| `path_cost_b_302f2` | `302FD` | near | `linear 0x30188` |
| `path_cost_b_302f2` | `30305` | far | `43B3:21A8` |
| `path_cost_b_302f2` | `30314` | near | `linear 0x30280` |
| `route_cost_7350` | `3033A` | near | `linear 0x2F6AE` |
| `route_cost_7350` | `3035B` | near | `linear 0x302F2` |
| `route_cost_7350` | `30366` | far | `3B1A:5AF8` |
| `two_step_752c` | `30511` | near | `linear 0x2F6AE` |
| `two_step_752c` | `30543` | far | `43B3:30FE` |
| `two_step_752c` | `30564` | far | `3B1A:5AF8` |
| `distance_5f60` | `46B68` | far | `544F:04AC` |
| `distance_5f60` | `46BA4` | near | `linear 0x45BF6` |
| `matrix_6006` | `46C03` | near | `linear 0x46B4C` |
| `nearest_own_6058` | `46C5B` | near | `linear 0x45CD8` |
| `nearest_own_6058` | `46C69` | near | `linear 0x46C2E` |
| `nearest_enemy_609c` | `46CA7` | near | `linear 0x45CD8` |
| `nearest_enemy_609c` | `46CBD` | near | `linear 0x46C2E` |
| `adjacency_20c6` | `45C0C` | near | `linear 0x45A7A` |
| `owner_21a8` | `45CE0` | near | `linear 0x45A7A` |
| `unowned_221e` | `45D56` | near | `linear 0x45CD8` |
| `officers_2ed6` | `46A13` | near | `linear 0x45D68` |
| `officers_2ed6` | `46A20` | far | `3B1A:750A` |

## Key selector instructions (0x22856)

| addr | instruction | role |
|---|---|---|
| `228DE` | `mov ax, word ptr [0x9216]` | initial ratio = word 0x9216 |
| `228E4` | `mov word ptr [bp - 0x12], 0xffff` | best castle = -1 (stay put) |
| `228FE` | `cmp si, -1` | candidate == -1 ends the 7-slot loop |
| `2292A` | `lcall 0x43b3, 0x30fe` | matrix[cand][best_ref] |
| `2293E` | `lcall 0x28fd, 0x7350` | cost = 7350(owner, 0x9212, cand) |
| `22948` | `lcall 0x28fd, 0x71b8` | value = 71B8(cand) |
| `2294F` | `lcall 0x3b1a, 0x5bc8` | ratio = 5BC8(value, cost) |
| `22957` | `sub ah, ah` | mask ratio to 8 bits |
| `2295C` | `cmp di, 0xf` | reject officers >= 15 |
| `22964` | `cmp word ptr [bp - 8], ax` | compare distance first |
| `2296D` | `cmp word ptr [bp - 0xc], di` | then officer count |
| `22975` | `cmp word ptr [bp - 0xa], ax` | then ratio |

## Shared words 0x920A..0x9220

| where | addr | global | access | instruction |
|---|---|---|---|---|
| `ctx_setup_226e0` | `226F7` | `0x920A` | write | `mov word ptr [0x920a], ax` |
| `ctx_setup_226e0` | `226FD` | `0x9210` | write | `mov word ptr [0x9210], cx` |
| `ctx_setup_226e0` | `2270D` | `0x920C` | write | `mov word ptr [0x920c], ax` |
| `ctx_setup_226e0` | `2271E` | `0x920E` | write | `mov word ptr [0x920e], ax` |
| `ctx_setup_226e0` | `2272F` | `0x9212` | read | `push word ptr [0x9212]` |
| `ctx_setup_226e0` | `22740` | `0x9214` | write | `mov word ptr [0x9214], si` |
| `ctx_setup_226e0` | `22750` | `0x9216` | write | `mov word ptr [0x9216], ax` |
| `ctx_setup_226e0` | `2275C` | `0x9218` | write | `mov word ptr [0x9218], ax` |
| `ctx_setup_226e0` | `22768` | `0x921A` | write | `mov word ptr [0x921a], ax` |
| `ctx_setup_226e0` | `22774` | `0x921C` | write | `mov word ptr [0x921c], ax` |
| `ctx_setup_226e0` | `22780` | `0x921E` | write | `mov word ptr [0x921e], ax` |
| `ctx_setup_226e0` | `227B0` | `0x9220` | write | `mov word ptr [0x9220], ax` |
| `select_22856` | `22874` | `0x920C` | read | `mov si, word ptr [0x920c]` |
| `select_22856` | `2287D` | `0x920E` | read | `mov ax, word ptr [0x920e]` |
| `select_22856` | `22888` | `0x9210` | read | `mov ax, word ptr [0x9210]` |
| `select_22856` | `22898` | `0x9212` | read | `mov ax, word ptr [0x9212]` |
| `select_22856` | `228DE` | `0x9216` | read | `mov ax, word ptr [0x9216]` |
| `exec_22f60` | `22F68` | `0x9220` | read | `cmp word ptr [0x9220], 4` |
| `exec_22f60` | `22FC4` | `0x9216` | read | `mov al, byte ptr [0x9216]` |
| `policy_recruit_2337e` | `233C2` | `0x9218` | read | `mov ax, word ptr [0x9218]` |
| `policy_recruit_2337e` | `233C8` | `0x921A` | read | `mov ax, word ptr [0x921a]` |
| `two_step_752c` | `30520` | `0x9210` | read | `cmp word ptr [0x9210], -1` |
| `two_step_752c` | `30527` | `0x9212` | read | `cmp word ptr [0x9212], -1` |

## Helper semantics (disassembly-derived)

| helper | address | formula | confidence | evidence |
|---|---|---|---|---|
| min | `3B1A:596E` | `min(a, b)` | B | 43B3:7350 tail, 43B3:30188 tail |
| max | `3B1A:597E` | `max(a, b)` | B | wrapper 23250 (0x23288) |
| add_capped | `3B1A:5AF8` | `v = min(v + x, cap)` | B | 0x3023B, 0x30272, 0x304CE |
| mul_div | `3B1A:5A0E` | `(a * b) / c; 0xFFFF when c == 0 or overflow` | B | 0x30254, 0x40D99 |
| percent | `3B1A:5A38` | `(a * b) / 100` | B | 0x302AE, 0x302B7 branch |
| ratio_5bc8 | `3B1A:5BC8` | `(a + b) * 100 / a; 50 when a == 0 and b == 0` | B | 0x40D68..0x40DA1: 5AF8(&tmp=a, b, -1) then 5A0E(tmp, 100, a) |
| rand_mod | `3B1A:5C1C` | `rand() % n` | B | 0x40DA2; used by 5C02 |
| rand_percent | `3B1A:5C02` | `rand() % 100 < x` | B | 0x40DA2..: calls 5C1C(100), compares AL with the argument |

## Castle adjacency evidence (Sndata.gp)

Undirected edges found in slots +0x12..+0x1e: **72**.

Slot 0 (+0x10) is nonzero only for:
- castle 19: 179

43B3:20C6 reads `+0x12 + 2*slot` for slot 0..6, so +0x10 is never part of the neighbour list; +0x12 is the first real neighbour.  The engine parser already skips +0x10.

## Independent distance cross-check (scenario 0)

- castle 30 faction = 0, nearest friendly = 30, nearest enemy = 27
- The C port prints the same values in `--v10-selftest` (`real d30_29=1 d30_0=13 friend30=30 enemy30=27`).

## Open questions

- Writer and exact business meaning of the shared word 0x9212.  It is used as the route-cost source in 7350(owner, 0x9212, castle) and is rejected when -1, so a castle id is likely but not proven.
- The six action routines 31CA:044A/0458/0474/0FFC/100A/145A are not decoded; only the dispatcher and the policy gates are established.
- 22D82 (move executor) is not decoded; its state writes are unknown.
- 18A6:3D86/3D54/2DBA/3856 and 43B3:1C18/1CA2 return semantics are not confirmed; 0x9218..0x9220 stay unnamed.
- The top-level monthly caller of 0x226E0/0x22F60 was not found in this image; it is probably in an overlay page that was not resident.
- 30280's third argument is compared against the source castle id and the intermediate owner; the exact 'from/to/owner' naming is provisional.
- No DOSBox-X session was available, so every conclusion here is static (confidence B), never dynamic (A).

## Confidence

- **B (static evidence):** adjacency slots, BFS distance matrix, nearest friendly/enemy, 0x22856 control flow, helper formulas, 0x2303C dispatcher shape.
- **C (engine-side approximation):** the 7350 route-cost / 71B8 value score is isolated behind a callback; no original score model is claimed in C.
- **A (dynamic):** none.  DOSBox-X is not installed in this workspace.
