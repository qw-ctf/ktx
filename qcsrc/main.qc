/*
 * Entry points and per-frame bookkeeping.
 */

float Pred_Enabled(void)
{
	if (autocvar_cl_nopred_weapon)
		return FALSE;

	// A demo has no input frames to replay and no local player to predict for.
	if (isdemo())
		return FALSE;

	if (intermission)
		return FALSE;

	return TRUE;
}

/*
 * Prediction is only honest when the server rewinds to meet the client. Without
 * antilag a predicted shot would not agree with what the server does with it,
 * so these default to following the server's setting.
 */
static float Pred_AntilagActive(void)
{
	return serverkey("sv_antilag") == "1";
}

float Pred_SoundEnabled(void)
{
	if (autocvar_cl_predict_weaponsound >= 2)
		return TRUE;
	if (!autocvar_cl_predict_weaponsound)
		return FALSE;

	return Pred_AntilagActive();
}

float Pred_ProjectilesEnabled(void)
{
	if (autocvar_cl_predict_projectiles >= 2)
		return TRUE;
	if (!autocvar_cl_predict_projectiles)
		return FALSE;

	return Pred_AntilagActive();
}

// ==========================================================================
// Predicted origin history
//
// A projectile has to leave from where the player was on the frame that fired
// it, which may be a few frames back. The engine only ever exposes its
// prediction for the present, so we keep a short history of it.
// ==========================================================================

static void Pred_RecordOrigin(void)
{
	float slot = clientcommandframe & PRED_MASK;

	origin_hist[slot] = pmove_org;
	origin_hist_frame[slot] = clientcommandframe;
}

vector Pred_OriginForFrame(float f)
{
	float slot = f & PRED_MASK;

	// At low frame rates some input frames never got sampled; the present
	// position is the closest thing we have.
	if (origin_hist_frame[slot] != f)
		return pmove_org;

	return origin_hist[slot];
}

// ==========================================================================
// Player status
// ==========================================================================

static void Pred_UpdatePlayerState(void)
{
	float was_playable = player_playable;

	player_playable = TRUE;

	if (getstatf(STAT_HEALTH) <= 0)
		player_playable = FALSE;
	if (intermission)
		player_playable = FALSE;
	if (getplayerkeyfloat(player_localnum, "*spectator"))
		player_playable = FALSE;

	/*
	 * The player just became playable, by respawning or by stopping spectating.
	 * The weapon snapshot we hold predates that, so hold off until a fresh one
	 * arrives rather than animating from stale state.
	 */
	if (player_playable && !was_playable)
	{
		playable_pending = TRUE;
		Beam_Clear();
		lg_twidth = 0;
	}
}

// ==========================================================================
// Entry points
// ==========================================================================

void CSQC_Init(float apilevel, string enginename, float engineversion)
{
	// All motion here is integrated per frame; the engine's own entity physics
	// runs on a coarse tick that would visibly stutter a close-up rocket.
	physics_mode = 0;

	Weapon_PrecacheAll();
	Sound_Reset();
	WeaponState_Reset();
	Beam_Clear();

	viewweapon = spawn();
	viewweapon.renderflags = RF_VIEWMODEL;
	// It doubles as the entity the predicted bolt traces with; see Beam_Draw.
	viewweapon.dimension_hit = 254;

	sv_gravity = 800;
}

void CSQC_WorldLoaded(void)
{
	// KTX publishes this because some maps change it and csqc cannot otherwise
	// see the server's movevars.
	sv_gravity = serverkeyfloat("sv_gravity", 800);

	if (autocvar_cl_predict_debug)
		print(sprintf("csqc: gravity %g\n", sv_gravity));
}

void CSQC_Shutdown(void)
{
	// The progs is torn down wholesale on map change; nothing to release.
}

void CSQC_Ent_Update(float isnew)
{
	float kind = ReadByte();

	switch (kind)
	{
	case EZCSQC_WEAPONINFO:
		WeaponState_Parse(isnew);
		break;

	case EZCSQC_PROJECTILE:
		Projectile_Parse(isnew);
		break;

	default:
		// Anything else means our idea of the stream has diverged from the
		// server's, and every following byte is garbage.
		error(sprintf("csqc: unknown entity type %g\n", kind));
	}
}

void CSQC_Ent_Remove(void)
{
	if (autocvar_cl_predict_debug)
		print(sprintf("csqc: server removed #%g\n", self.entnum));

	remove(self);
}

/*
 * Peek at temp entities to notice the server's lightning beam. Everything is
 * passed straight back to the engine: returning false rewinds the read, so the
 * engine parses and draws it exactly as it normally would.
 */
float CSQC_Parse_TempEntity(void)
{
	float kind = ReadByte();

	if (kind == TE_LIGHTNING2)
	{
		// The server's beam for us has caught up with the predicted one.
		if (ReadShort() == player_localentnum)
			Beam_ServerArrived();

		return FALSE;
	}

	return FALSE;
}

void CSQC_UpdateView(float vwidth, float vheight, float notmenu)
{
	clearscene();

	setproperty(VF_DRAWENGINESBAR, TRUE);
	setproperty(VF_DRAWCROSSHAIR, TRUE);

	Pred_RecordOrigin();
	Pred_UpdatePlayerState();

	/*
	 * Replay first. addentities() runs the projectile predraws and turns beams
	 * into render entities as it goes, so anything spawned or drawn after it
	 * would not appear until the next frame.
	 */
	WeaponState_Replay();
	Beam_Draw();

	addentities(MASK_ENGINE);

	// Fall back to the engine's view model whenever we decline to draw one, so
	// the player is never left without a gun.
	if (!WeaponState_Present())
		addentities(MASK_VIEWMODEL);

	renderscene();
}

// Declared __used by the engine definitions, so it must exist.
void CSQC_Input_Frame(void)
{
}
