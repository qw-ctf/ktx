/*
 * Client-side weapon prediction for KTX.
 *
 * The server owns the weapon state machine and runs it on its own clock
 * (client_time, advanced per player frame). Every player frame it sends us an
 * owner-only snapshot of that machine. We replay the input frames the server
 * has not acknowledged yet on top of the snapshot, so the view weapon animates,
 * its sounds play and its projectiles appear at the moment the player fires
 * rather than a round trip later.
 *
 * Three clocks are in play and must not be conflated:
 *   client_time  the server's per-player weapon clock, advanced by replaying
 *                usercmd durations. Drives the state machine exclusively.
 *   time         interpolated server time. Drives projectile flight and the
 *                extrapolation of server-sent projectiles.
 *   cltime       real client time. Only used for effect-rate limiting.
 */

// ==========================================================================
// Wire protocol, mirrors ktx include/progs.h and include/g_consts.h
// ==========================================================================

#define EZCSQC_WEAPONINFO	1
#define EZCSQC_PROJECTILE	2

// weapon state payload
#define WEAPONINFO_INDEX		1
#define WEAPONINFO_AMMO_SHELLS	2
#define WEAPONINFO_AMMO_NAILS	4
#define WEAPONINFO_AMMO_ROCKETS	8
#define WEAPONINFO_AMMO_CELLS	16
#define WEAPONINFO_ATTACK		32
#define WEAPONINFO_TIMING		64
#define WEAPONINFO_PRED_PING	128

// the weapon byte carries the respawn generation above the weapon index
#define WEAPONINFO_WEAPON_MASK		0x0f
#define WEAPONINFO_GENERATION_SHIFT	4

// projectile payload
#define PROJECTILE_ORIGIN		1
#define PROJECTILE_MODEL		2
#define PROJECTILE_ANGLES		4
#define PROJECTILE_OWNER		8
#define PROJECTILE_SPAWN_ORIGIN	16

// server prediction flags
#define PRDFL_MIDAIR	1
#define PRDFL_COILGUN	2
#define PRDFL_YAWNMODE	4
#define PRDFL_FORCEOFF	255

// item bits, for the weapon a switch is allowed to select
#define IT_SHOTGUN				1
#define IT_SUPER_SHOTGUN		2
#define IT_NAILGUN				4
#define IT_SUPER_NAILGUN		8
#define IT_GRENADE_LAUNCHER		16
#define IT_ROCKET_LAUNCHER		32
#define IT_LIGHTNING			64
#define IT_AXE					4096
#define IT_INVISIBILITY			524288

// temp entity number we peek at
#define TE_LIGHTNING2	6

// ==========================================================================
// Weapon definition flags
//
// These mirror the server's WEPPREDANIM_* bits. The server also has a
// MOREBYTES marker that only exists to frame its byte stream; it has no
// meaning here and is deliberately absent.
// ==========================================================================

enumflags
{
	ANIM_SOUND,			// play .sound on entering this state
	ANIM_PROJECTILE,	// spawn a predicted projectile on entering
	ANIM_LGBEAM,		// draw the predicted lightning beam
	ANIM_MUZZLEFLASH,	// muzzle flash (server-side only, no csqc effect)
	ANIM_DEFAULT,		// the idle state; where a finished animation lands
	ANIM_ATTACK,		// firing state: sets attack_finished
	ANIM_BRANCH,		// re-tests +attack on entry, exits to .alt
	ANIM_SOUNDAUTO,		// .sound goes on CHAN_AUTO instead of CHAN_WEAPON
	ANIM_LTIME,			// throttled by the lightning sound window
	ANIM_SOUND2			// a second sound, always on CHAN_WEAPON
};

// ==========================================================================
// Tunables
// ==========================================================================

const float PRED_BACKUP			= 64;	// input frame ring, matches UPDATE_BACKUP
const float PRED_MASK			= 63;

const float LG_SOUND_WINDOW		= 0.6;	// lightning re-triggers its sound this often
const float LG_ATTACK_TIME		= 0.2;	// lightning refire, overrides the table
const float RESPAWN_GUARD		= 0.05;	// server discards attacks this long after spawning
const float NEWMIS_STEP			= 0.05;	// the server advances a new missile one frame

const float HANDOFF_GRENADE_DIST	= 128;	// grenade may be adopted within this
const float HANDOFF_DIST			= 64;	// other projectiles, generous case
const float HANDOFF_PERP			= 8;	// ... or this far off the flight line
const float HANDOFF_PHASE			= 256;	// ... and this far along it
const float HANDOFF_MIN_TIME		= 0.15;
const float HANDOFF_MAX_TIME		= 0.3;

const float TRAIL_PERIOD		= 0.013;	// spacing between trail emissions
const vector GRENADE_SPIN		= '300 300 300';
const float TOO_CLOSE_RADIUS	= 8;	// hide our own projectile inside this
const float BOUNCE_FACTOR		= 0.5;	// grenade restitution
const float BOUNCE_MAX_BUMPS	= 5;

const float SOUND_TOKEN_TTL		= 1.0;	// unmatched suppression tokens expire
const float MAX_SOUND_TOKENS	= 8;

const float BEAM_RANGE			= 600;
const float BEAM_HOLD			= 0.3;	// keep re-issuing a predicted beam this long
const float BEAM_SERVER_HOLD	= 0.15;	// treat the server's beam as live this long after one arrives

// ==========================================================================
// Cvars
//
// Named to match ezQuake so a player's existing settings carry over.
// The tri-state ones are: 0 off, 1 only when the server runs antilag,
// 2 force on regardless.
// ==========================================================================

var float autocvar_cl_nopred_weapon			= 0;
var float autocvar_cl_predict_weaponsound	= 1;
var float autocvar_cl_predict_projectiles	= 1;
var float autocvar_cl_predict_beam			= 1;
var float autocvar_cl_predict_buffer		= 1;
// 0 off, 1 the events worth noticing, 2 adds per-frame traces
var float autocvar_cl_predict_debug			= 0;

// ==========================================================================
// Shared state
// ==========================================================================

// Predicted player origin per input frame. The engine only exposes its
// prediction for the current frame, so we sample it each view frame and use
// the stored value when spawning a projectile for an earlier frame.
vector	origin_hist[PRED_BACKUP];
float	origin_hist_frame[PRED_BACKUP];

float	player_playable;	// alive, in play, not spectating
float	playable_pending;	// became playable; waiting for a fresh weapon state
float	sv_gravity;

entity	viewweapon;			// the entity carrying our predicted view model

// Effect high-water marks. Replaying repeats frames we have already seen, so
// one-shot effects record the newest frame they fired on.
float	last_sound_frame;
float	last_projectile_frame;

// Set for the duration of one replayed frame that is allowed to fire effects.
float	frame_may_sound;
float	frame_may_projectile;
float	replay_frame;

float Pred_Enabled(void);
float Pred_SoundEnabled(void);
float Pred_ProjectilesEnabled(void);
vector Pred_OriginForFrame(float f);
void Sound_PlayPredicted(string sample, float chan);
float Projectile_SpawnLocal(float widx, float a);
void Beam_Clear(void);
void Beam_Predict(void);
