# Antilag port: findings for the deeper pass

Written 2026-11-24, while porting the antilag feature from `latest` onto
upstream as the `antilag-upstream` series. Line numbers refer to
`antilag-upstream` at `a8748d42`.

The port carries the fork's logic over unchanged, so everything below is
present in the series and nothing has been fixed. Unless an item says
otherwise it comes from reading the code. Nothing was run on a server.
"Local mvdsv" is `~/Development/home/mvdsv` at `05880d2`; "upstream mvdsv" is
its `origin/master` at `dcc8355`.

## 1. Engine dependency

The engine half of the feature is not in upstream mvdsv. It lives in four
local mvdsv commits:

| Commit | What KTX uses from it |
|---|---|
| `eccdf50` | `precache_model` / `precache_sound` return the index |
| `5d2af5f` | EZCSQC negotiation: `ezcsqc`, `ezcsqc_ready`, message destination 6 |
| `09f935b` | `ping_current`, `antilag_rewind`, the `SetLastRuntime` extension |
| `05880d2` | keeps registered stats away from native EZCSQC clients |

Upstream mvdsv has none of `antilag_rewind`, `ping_current`, `SetLastRuntime`,
`MSG_ONE_NORECORD` or `ezcsqc_ready` (checked with `git grep`). The series does
not say what KTX should do on an engine without them, and the two pull
requests need an order.

1. **An unpatched mvdsv is taken down by the first CSQC client.** Checked in
   upstream mvdsv's source: `infokey(client, "ezcsqc")` is an alias of
   `csqcactive`, and `WriteDest` has no case for destination 6, so it falls to
   `default:` and `PR2_RunError`, which calls `SV_Error`.
   `WPredict_SendDefinitionsTo` (`weaponpred_defs.c`) writes to
   `MSG_ONE_NORECORD` for any player whose `ezcsqc` key is set. So once the
   server ships a `csprogs.dat` and a client runs it, the server stops with a
   program error. Without a `csprogs.dat` nobody has `csqcactive` and nothing
   happens. The EZCSQC path needs a gate on something the patched engine
   advertises, not on the info key alone.
2. **The rewind is client-chosen on engines without the new info keys.**
   `antilag_current_rewind` (`antilag.c:26`), `antilag_current_ping` and
   `CurrentClientPing` (`client.c`) read `antilag_rewind` and `ping_current`
   through `ezinfokey`. Local mvdsv computes both server-side. On any engine
   that lacks them the lookup falls through to the player's own userinfo, so
   `setinfo antilag_rewind 250` picks the hitscan rewind (clamped to 0-250 ms)
   and `setinfo ping_current 80` the projectile catch-up. mvdsv answers
   `infokey(world, "\\antilag_rewind")` with yes or no; KTX never asks.
3. **`qwm_ezcsqc 1` is set unconditionally** (`g_main.c`, `G_InitGame`), also
   when the engine has none of the extensions.
4. **`SetSendNeeded` missing.** On an engine without the extension, upstream's
   own `SetSendNeeded` wrapper broadcasts "SetSendNeeded needs support in
   server" for every projectile fired, and again for every live grenade every
   frame (see 2.8).
5. `resources/example-configs/ktx/mvdsv.cfg:13` still documents `sv_antilag`
   as "0 = off, 2 = on, 1 is unsupported by KTX".

## 2. Behaviour that changes on every server

These apply with `sv_antilag` 0 or 2 and with no CSQC client connected, which
is how upstream servers run today.

1. **The hook locks out firing.** `grapple.c:64-65,72` (`GrappleReset`) still
   sets `attack_finished` from `g_globalvars.time`. The fire gate in
   `W_WeaponFrame` now compares it with `client_time`, which starts at 0 when
   the player connects and is never seeded from server time. After a hook
   release the player cannot fire until `client_time` catches up with server
   time: minutes for someone who joined mid-map. The fork did not convert
   `grapple.c`, so the fork has the same bug.
   - `motd.c:34` has the same mix: firing no longer dismisses the MOTD.
   - `fb_globals.c:233` compares the bot's own `client_time` with the enemy's
     `attack_finished`. The other bot call sites use the enemy's clock.
2. **Weapon timing follows the client's reported frame time.**
   `PlayerPreThink` advances `client_time` by `g_globalvars.frametime`, the
   msec of each command (capped by the engine).
   - A client sending inflated msec refires faster, bounded only by the
     engine's speed-cheat checks.
   - When more commands are lost than the backup copies cover, cooldowns stand
     still instead of running out.
   - The animation chain now runs from `PlayerPostThink`, so held nailgun and
     lightning gun shots leave from the position after the move, not before.
   - Spectators never advance the clock. Bots advance it like players.
3. **Prewar spam.** The prediction-flags chain in `PlayerPostThink` calls
   `can_prewar(true)` for every player in every frame before a match
   (`client.c:4908`). `can_prewar` has side effects: a `bf` screen flash and a
   message, limited to once a second. They now repeat without anyone pressing
   fire with `k_prewar 0`, with `k_prewar 2` for a player who is not ready,
   and with `k_prewar 1` while captains pick.
4. **A weapon animation survives the respawn of a living player.** Only
   `player_run` and `PlayerDead` clear `client_think` / `client_nextthink`.
   `PutClientInServer` and `player_stand1` do not, where upstream's
   `player_stand1` overwrote `think` and ended the chain. `k_respawn` on a
   live player (`arena.c`, `clan_arena.c`, `race.c`, match start in `match.c`)
   keeps the previous life's chain firing. `player_nail1/2` and
   `player_light1/2` fire without checking the current weapon, and the
   dispatch runs before `W_WeaponFrame`'s 50 ms spawn guard.
5. **Pending impulses are dropped on respawn.** `k_respawn` clears every
   impulse and restores only a usable weapon selection (1-8 or 22). Weapon
   cycling (10, 12) and any other pending impulse is lost, also on first
   spawn. During the first 50 ms after a spawn an unusable weapon impulse is
   now cleared silently, where it used to print "no weapon" or "not enough
   ammo" afterwards.
6. **The knockback rule applies in every antilag mode, and its clock is wrong
   during physics.** `combat.c:885` skips knockback unless
   `targ->teleport_time < time_corrected`, with no `sv_antilag` condition.
   `world.c:1959` then does `time_corrected = time` with `StartFrame`'s `int`
   parameter. mvdsv passes `(int)(sv.time * 1000)` there (`pr2_exec.c:229`),
   so from `StartFrame` until the next `PlayerPreThink` the value is in
   milliseconds, and the comparison is seconds against milliseconds (always
   true). The same value feeds `local_explosion->gravity` in `T_MissileTouch`
   and the un-rewind test in `Killed` (`combat.c:259`).
7. **The axe animation pick** (`weapons.c:2394`).
   `(int)(client_time * 931.75) << 11` overflows a signed int once
   `client_time` passes about 1125 s (18.8 minutes), which is undefined
   behaviour in the native build. `fabs` on an int gives `-Wabsolute-value`,
   the one warning the series adds (confirmed by building). It also removes
   one `g_random()` draw per swing. The csprogs mirrors the formula at
   `qcsrc/weapon_state.qc:435`, so both sides have to change together.
8. **Projectiles become CSQC entities unconditionally.**
   `ScheduleProjectileSendIfLive` runs for every rocket, grenade and nail,
   whatever `sv_antilag` is, and `UpdateProjectileSendNeeded` scans all
   entities and resends every live grenade in every `StartFrame`.
   `SendEntity_Projectile` returns false for clients without `csqcactive`;
   whether those clients, MVD and QTV then still get the projectile as a
   normal entity depends on the engine and was not checked. It also does not
   mask its sendflags, so bits 5-7 can go out set.
9. **Always-on cost.** One extra edict per player and per bot for the
   weapon-info entity, two or three info-key lookups per client command in
   `WeaponPrediction_MarkSendFlags`, and `antilag_log` in every
   `PlayerPostThink`, also with `sv_antilag 0`.
10. **Antilag bookkeeping.**
    - `antilag_list_players`, `antilag_list_world` and
      `ANTILAG_MEMPOOL_WORLDSEEK` are never reset at map load. If the engine
      keeps the module loaded across maps, a reconnecting player pushes the
      same pool slot onto a stale list, which can form a cycle (the rewind
      loops would hang) or leave stale owners.
    - `antilag_create_world` (`antilag.c:282`) indexes the pool at
      `64 + ANTILAG_MEMPOOL_WORLDSEEK` with no bounds check. The pool has 256
      slots, so a map with more than 192 movers overflows it. Player slots
      assume an edict number below 64.
    - `antilag_delete_player` has no NULL check and does not clear the links
      or `antilag_data`. `antilag_delete_world` is never called, and
      `ANTILAG_TIMESTEP` is never used.
    - `WeaponPrediction_ResetBaseline` lacks the stale-pointer check that
      `WeaponPrediction_MarkSendFlags` has.
11. **Default and vote.** `common_um_init` now sets `sv_antilag 1` instead of
    2, and the vote cycles 0, 1, 2. Checked in mvdsv: traces are rewound by
    the engine only at `sv_antilag == 2` (`pr2_cmds.c:514`), while any
    non-zero value keeps the engine's lagged-entity bookkeeping running, so at
    1 KTX's rewind is not doubled. `sv_antilag_projectiles` is likewise tied
    to 2 (`sv_phys.c:754`).

## 3. Only under sv_antilag 1

1. **`T_MissileTouch`** (`weapons.c:1160-1210`). The antilag branch needs
   `oldangles` and `rad_time`, which only `W_FireRocket` sets. A rocket
   already in flight when the mode is switched to 1, and boss rockets
   (`sp_boss.c` uses `T_MissileTouch`), get a very large delay, so the owner's
   own splash damage is postponed indefinitely. The division by
   `vlen(velocity)` has no zero check, and the owner's quad is evaluated when
   the delayed explosion fires, not when the rocket hit.
2. **Writes to a removed entity.** When the catch-up makes a rocket or grenade
   hit inside `antilag_lagmove_all_proj*`, the projectile is already removed,
   and `BotsRocketSpawned` (`weapons.c:1301`) and `BotsGrenadeSpawned`
   (`weapons.c:1675`) still write `think` and `nextthink` into it.
3. **`antilag.c` arithmetic.**
   - `1 / 32` (line 164) and `60 / 100` (line 183) are integer divisions and
     both 0. With `bouncestp` at 0 the grenade come-to-rest branch can never
     be taken during catch-up.
   - `antilag_lagmove` and `antilag_getorigin` divide by the difference of two
     sample times, which is zero for an empty history.
   - Line 700 checks `step_time * speed > 3` but then sets the step to
     `8 / speed`; the comment and the bounce variant (32 and 32) suggest the
     two numbers were meant to agree.
   - `FL_GODMODE` is set on the projectile and not cleared when it hits.
   - `self->touch` is called without a NULL check.
   - `owner->s.v.origin` is written directly, without a relink, and
     `owner->antilag_data` is dereferenced without a NULL check.
   - `antilag_unmove_all()` is called again right after
     `antilag_lagmove_all_proj*`, which already restores, and `e->oldangles`
     is copied twice. Both are harmless.
4. **Race gaps**, latent because race forces `sv_antilag 0`: the early return
   in `W_FireAxe` skips `antilag_unmove_all`, and `T_RadiusDamage_Ignore2` has
   no `isRACE()` branch.
5. `weaponpred_defs.c:266` reads `k_yawnmode` once at worldspawn, before the
   rules are fixed, so the definition table sent to native EZCSQC clients
   ignores later rule changes.

## 4. Prediction: where the csprogs and the server disagree

The wire layouts match on both sides: entity ids, sendflag bits, field order
and widths, the weapon-index and generation packing, pred flags, the gravity
stat, and the weapon table row by row. The differences are in behaviour.

1. **Lightning gun, first shot.** The client's state 0 shows frame 0 and sets
   refire to +0.1 (`weapon_table.qc:184-187`, `weapon_state.qc:379-386`). The
   server's `W_Attack` calls `player_light1` in the same frame, which shows
   frame 1 and sets +0.2. A tap shorter than 100 ms is predicted able to
   refire 0.1 s too early, and the predicted frame lags by one. The server's
   own definition table has the same state 0. `player_light2` also reports
   think index 1 where the table says 2, which is not visible.
2. **Super nailgun with one nail.** `W_FireSpikes` falls back to a single
   plain spike with the nailgun sound. The client always predicts a super
   spike: the sound plays twice, the hand-over fails because the models
   differ, and an orphan local spike flies on.
3. **Refire changes that no pred flag reports.** The CTF haste rune, and
   instagib without `k_instagib_custom_models` (server 1.2 or 0.7 s,
   predicted 0.5 s, because `PRDFL_COILGUN` is only set with custom models).
4. **Grenades.** The server clamps pitch and adds random spread outside
   yawnmode; the client does neither.
5. **Respawn guard.** The server's runs on `g_globalvars.time - spawn_time`
   and still runs the weapon think; the client's runs on `client_time` and
   skips it.
6. **`defs.qc`.** `ANIM_SOUNDAUTO`, `ANIM_LTIME` and `ANIM_SOUND2` are 0x80,
   0x100 and 0x200; the server's `WEPPREDANIM_*` are 0x180, 0x280 and 0x480.
   They never cross the wire to the csprogs, but the comment says they mirror
   the server's bits. The comment on `cl_predict_projectiles 2` says "force
   on", yet `Projectile_SpawnLocal` gives up at ping 0, which is what the
   server sends unless `sv_antilag` is 1.
7. **Sent but never read.** The timestamp in `PROJECTILE_ORIGIN`, and
   `ANIM_MUZZLEFLASH` on the client.

## 5. Build and packaging

1. **The CMake `csprogs` target** writes `build/csprogs.dat` in the source
   tree whatever the binary directory is, because the path comes from the
   first line of `qcsrc/progs.src`. fteqcc does not create that directory and
   exits 0 when it cannot write, so a build in any other directory silently
   produces no `csprogs.dat` (confirmed by running). `find_program` still
   lists `fteqcc64`, the binary that was dropped. Nothing installs
   `csprogs.dat` into a gamedir.
2. **`qcsrc/fteextensions.qc`** was generated by FTE build 5982, dated
   2021-07-18, while the local fteqcc is from 2026. The refresh recipe in
   `qcsrc/Makefile` differs from the one in the file's own header. It covers
   server, client and menu QuakeC behind `#ifdef`s.
3. **No licence headers** on the `.qc` files or the Makefile. FTE's generated
   file is GPL.
4. **`qcsrc/Makefile`**: `OUT` is only used by `clean`.
5. **MSVC** (`/W4`) was not built. Upstream CI has a `verify-msvc` job.

## 6. Style

Not addressed in the port, by design.

- The fork's code does not follow upstream's brace style: single-statement
  `if` bodies without braces, in `antilag.c`, `weapons.c`, `client.c` and
  `vote.c`.
- `antilag.c` keeps commented-out code and the `///*` / `//*/` toggles.
- `// hacky check` comments on `do_antilag` in `weapons.c`.
- `player_dummyanim` (`player.c`) is an empty think the engine still
  schedules every 0.1 s.
- `qcsrc/defs.qc:16` is a blank line inside the header block comment, and
  `qcsrc/projectiles.qc:105-106` is an orphaned comment that belongs to
  `Projectile_EmitTrail`.

## 7. Found during the pruning review

Added later on 2026-11-24, while checking what can be removed once only real
CSQC is supported. Same tree, `a8748d42`.

1. **Released mvdsv stops at the first projectile.** Checked at the mvdsv
   tags `v1.10` and `1.11`: with the engine's CSQC extension compiled in,
   `setsendneeded` is mapped to a stub that calls
   `PR2_RunError("SetSendNeeded not implemented yet.")` (`pr2_cmds.c:2004`).
   KTX's `SetSendNeeded` wrapper only checks that the extension is mapped, and
   `ScheduleProjectileSendIfLive` (`weapons.c:140`) runs for every rocket,
   grenade and nail whatever `sv_antilag` is. The working implementation is
   mvdsv `26d5325` (2026-10-20), on master and untagged. This replaces the
   milder description in 1.4, which only holds for engines that do not map
   the extension at all.
2. **Edict leak when the engine cannot set `SendEntity`.**
   `ExtFieldSetSendEntity` leaves `ed->SendEntity` at 0 when the engine has
   neither `MapExtFieldPtr` + `SetExtFieldPtr` nor `SetExtField` (mvdsv
   `v1.00` has no `MapExtFieldPtr`). `WeaponPrediction_MarkSendFlags`
   (`client.c:1745`) treats that as a stale weapon-info entity, drops the
   pointer without freeing it and spawns a new one, in every `PlayerPostThink`
   for every player and bot, until the server runs out of edicts.
3. **Unguarded trap.** `client.c:1810` calls `trap_SetSendNeeded` directly,
   without the `HAVEEXTENSION` check the wrapper has. Only the `csqcactive`
   info key stands in front of it, and on an engine without that key the
   lookup falls through to the player's own userinfo.
4. **The csprogs picks the wrong axe swing about half the time.**
   `qcsrc/weapon_state.qc:435` computes `(...int...) % 1000`, which is int %
   float. Because `fteextensions.qc:2283` declares the `mod` builtin, fteqcc
   compiles that as a float-to-int conversion applied to the int value and a
   call to `mod()`, so the argument collapses to about 0 and
   `WeaponState_AxeEntry` returns chain 1 almost always. Found by
   disassembling the built `csprogs.dat` and emulating both sides: agreement
   with the server's C expression is about 50%. Writing `% (int)1000` gives
   exact integer code and 99.4-100% agreement; the rest is float against
   double. `WeaponState_RecoverAxeChain` hides the miss once the server's
   snapshot arrives. The comment at `weapon_state.qc:420-429` blames precision
   alone.
5. **Repeated rewinds compound.** Each step of the projectile catch-up loop
   calls `antilag_lagmove_all_playeronly` again, and `antilag_lagmove`'s
   extrapolation and `frac > 1` branches start from the player's current
   origin, which the previous step already rewound. That can add up to
   20 ms x velocity per step.
6. **Quad rockets lose their glow for CSQC clients.** In midair mode the
   server sets `EF_BLUE` on quad rockets (`weapons.c:1266`) and sends the
   effects in the projectile entity, but the csprogs never reads `.effects`,
   and FTE applies only a handful of effect bits to CSQC entities (read in
   FTE's `pr_csqc.c`), not this one.
7. `PRDFL_MIDAIR` in the instagib branch (`client.c:4892-4893`) is always sent
   together with `PRDFL_COILGUN`, which makes the csprogs stand aside
   entirely, so it has no effect.
