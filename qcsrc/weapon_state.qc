/*
 * The weapon state machine: parse the server's snapshot, replay unacknowledged
 * input on top of it, present the result as a view model.
 */

typedef struct
{
	float	impulse;
	float	weapon_index;
	float	ammo_shells;
	float	ammo_nails;
	float	ammo_rockets;
	float	ammo_cells;
	float	attack_finished;
	float	client_nextthink;
	float	client_thinkindex;
	float	client_time;
	float	frame;
	float	predflags;
	float	ping;
} wstate_t;

wstate_t	ws_server;		// the last snapshot the server sent
wstate_t	ws_predicted;	// scratch, rebuilt from ws_server every view frame
wstate_t	ws_presented;	// what the view model actually shows

float	ws_have_state;		// a snapshot has arrived
float	ws_state_frame;		// input frame the snapshot already accounts for
float	weapon_generation;	// respawn counter, -1 until the first snapshot
float	respawn_guard_until;// server ignores attacks until this client_time
float	lg_twidth;			// lightning sound may retrigger from this client_time

// The view model interpolates between two poses; these track the swap.
float	vw_lerptime;

void Weapon_EnterState(float idx, float buttons);

void WeaponState_Reset(void)
{
	ws_have_state = FALSE;
	ws_state_frame = 0;
	weapon_generation = -1;
	respawn_guard_until = 0;
	lg_twidth = 0;
	last_sound_frame = 0;
	last_projectile_frame = 0;
	vw_lerptime = 0;
}

// ==========================================================================
// Parsing
// ==========================================================================

void WeaponState_Parse(float isnew)
{
	float sendflags = ReadByte();
	float enc, generation = weapon_generation;

	if (sendflags & WEAPONINFO_INDEX)
	{
		ws_server.impulse = ReadByte();

		enc = ReadByte();
		generation = enc >> WEAPONINFO_GENERATION_SHIFT;
		ws_server.weapon_index = enc & WEAPONINFO_WEAPON_MASK;
	}

	if (sendflags & WEAPONINFO_AMMO_SHELLS)
		ws_server.ammo_shells = ReadByte();
	if (sendflags & WEAPONINFO_AMMO_NAILS)
		ws_server.ammo_nails = ReadByte();
	if (sendflags & WEAPONINFO_AMMO_ROCKETS)
		ws_server.ammo_rockets = ReadByte();
	if (sendflags & WEAPONINFO_AMMO_CELLS)
		ws_server.ammo_cells = ReadByte();

	if (sendflags & WEAPONINFO_ATTACK)
	{
		ws_server.attack_finished = ReadFloat();
		ws_server.client_nextthink = ReadFloat();
		ws_server.client_thinkindex = ReadByte();
	}

	if (sendflags & WEAPONINFO_TIMING)
	{
		ws_server.client_time = ReadFloat();
		ws_server.frame = ReadByte();
	}

	if (sendflags & WEAPONINFO_PRED_PING)
	{
		ws_server.predflags = ReadByte();
		ws_server.ping = ReadByte() / 1000;
	}

	/*
	 * The generation changes when the player respawns. Everything we predicted
	 * before that belongs to the previous life, and the server throws away
	 * attacks for a moment after spawning, so do the same. This has to come
	 * after the timing fields are read: the guard runs off the clock in this
	 * snapshot, not the one it replaces.
	 */
	if (weapon_generation >= 0 && generation != weapon_generation)
	{
		respawn_guard_until = ws_server.client_time + RESPAWN_GUARD;
		lg_twidth = 0;
		Beam_Clear();
	}
	weapon_generation = generation;

	/*
	 * The axe runs one of two interchangeable swing chains. The server reports
	 * both as the same think index, so recover the second chain from the frame
	 * it is showing.
	 */
	if (ws_server.weapon_index == 1 && ws_server.frame >= 5
		&& ws_server.client_thinkindex >= 2 && ws_server.client_thinkindex <= 4)
	{
		ws_server.client_thinkindex += 4;
	}

	/*
	 * Replay has to start from the input frame this snapshot already includes.
	 * Capture it now: by the time we draw, the global has moved on, and any
	 * frame the snapshot did not see would be skipped.
	 */
	ws_state_frame = servercommandframe;
	ws_have_state = TRUE;
	playable_pending = FALSE;

	if (isnew)
		vw_lerptime = 0;
}

// ==========================================================================
// State machine
//
// A state is entered, sets the view frame and any effects, and schedules the
// next one. client_thinkindex names the state that is scheduled, not the one
// being shown.
// ==========================================================================

static float Weapon_HasItem(float idx)
{
	return (getstatbits(STAT_ITEMS, 0, 23) & weapons[idx].itemflag) != 0;
}

// Does the player have enough ammo for the weapon this impulse selects?
static float Weapon_HasAmmo(float impulse)
{
	switch (impulse)
	{
	case 2:  return ws_predicted.ammo_shells >= 1;
	case 3:  return ws_predicted.ammo_shells >= 2;
	case 4:  return ws_predicted.ammo_nails >= 1;
	case 5:  return ws_predicted.ammo_nails >= 2;
	case 6:  return ws_predicted.ammo_rockets >= 1;
	case 7:  return ws_predicted.ammo_rockets >= 1;
	case 8:  return ws_predicted.ammo_cells >= 1;
	}

	return TRUE;
}

enum
{
	SWITCH_APPLIED,
	SWITCH_BLOCKED,		// mid-animation; the impulse stays queued
	SWITCH_UNRESOLVED	// cannot select it; drop the impulse
};

static float Weapon_Switch(float impulse)
{
	// The server refuses to switch until the current animation finishes, and
	// remembers the impulse until then.
	if (ws_predicted.client_time < ws_predicted.attack_finished)
		return SWITCH_BLOCKED;

	for (float i = 1; i < weapons.length; i++)
	{
		if (!Weapon_Valid(i) || weapons[i].impulse != impulse)
			continue;

		if (!Weapon_HasItem(i) || !Weapon_HasAmmo(impulse))
			return SWITCH_UNRESOLVED;

		if (ws_predicted.weapon_index == i)
			return SWITCH_APPLIED;

		if (weapons[ws_predicted.weapon_index].itemflag == IT_LIGHTNING)
		{
			lg_twidth = 0;
			Beam_Clear();
		}

		ws_predicted.weapon_index = i;
		ws_predicted.client_thinkindex = 0;
		ws_predicted.client_nextthink = 0;
		ws_predicted.frame = max(0, weapons[i].anim[0].frame);
		return SWITCH_APPLIED;
	}

	return SWITCH_UNRESOLVED;
}

// Effects fire on entering a state, but only on a frame we have not already
// played them for: replay walks the same frames again every time we draw.
static void Weapon_PlayEffects(float idx, float a, float buttons)
{
	anim_t *anim = &weapons[idx].anim[a];
	float flags = anim->flags;
	float chan, throttled;

	if (autocvar_cl_nopred_weapon)
		return;

	if (frame_may_sound && (flags & ANIM_SOUND))
	{
		chan = (flags & ANIM_SOUNDAUTO) ? CHAN_AUTO : CHAN_WEAPON;

		// The lightning gun ticks every 100ms but its sound repeats far more
		// slowly; the window keeps the two apart.
		throttled = (flags & ANIM_LTIME) && ws_predicted.client_time < lg_twidth;

		if (!throttled)
		{
			Sound_PlayPredicted(anim->sound, chan);

			if (flags & ANIM_SOUND2)
			{
				/*
				 * Lightning restarts its loop sound freely but only re-strikes
				 * on the same slow cadence as the beam.
				 */
				if ((flags & ANIM_LGBEAM) && !(flags & ANIM_LTIME))
				{
					if (ws_predicted.client_time >= lg_twidth)
					{
						Sound_PlayPredicted(anim->sound2, CHAN_WEAPON);
						lg_twidth = ws_predicted.client_time + LG_SOUND_WINDOW;
					}
				}
				else
				{
					Sound_PlayPredicted(anim->sound2, CHAN_WEAPON);
				}
			}

			if (flags & ANIM_LTIME)
				lg_twidth = ws_predicted.client_time + LG_SOUND_WINDOW;
		}

		last_sound_frame = max(last_sound_frame, replay_frame);
	}

	if (frame_may_projectile && (flags & ANIM_PROJECTILE))
	{
		if (Projectile_SpawnLocal(idx, a))
			last_projectile_frame = max(last_projectile_frame, replay_frame);
	}

	if (frame_may_projectile && (flags & ANIM_LGBEAM))
	{
		Beam_Predict();
		last_projectile_frame = max(last_projectile_frame, replay_frame);
	}
}

void Weapon_EnterState(float a, float buttons)
{
	float idx = ws_predicted.weapon_index;

	if (a < 0 || a >= weapons[idx].anim_count)
	{
		ws_predicted.client_thinkindex = 0;
		ws_predicted.client_nextthink = 0;
		return;
	}

	anim_t *anim = &weapons[idx].anim[a];
	float flags = anim->flags;

	// The idle state ends the animation.
	if (a == 0 && (flags & ANIM_DEFAULT))
	{
		ws_predicted.client_thinkindex = 0;
		ws_predicted.client_nextthink = 0;
		if (anim->frame >= 0)
			ws_predicted.frame = anim->frame;
		return;
	}

	/*
	 * A looping fire state re-checks the trigger when it comes round. Letting
	 * go, or queueing a weapon switch, drops out to the idle state.
	 */
	if ((flags & ANIM_ATTACK) && (flags & ANIM_BRANCH)
		&& (!(buttons & 1) || ws_predicted.impulse))
	{
		float alt = anim->alt;

		if (alt >= 0 && alt < weapons[idx].anim_count
			&& (weapons[idx].anim[alt].flags & ANIM_DEFAULT))
		{
			Weapon_EnterState(alt, buttons);
		}
		else
		{
			// Lightning has no idle state; it simply stops.
			ws_predicted.client_thinkindex = 0;
			ws_predicted.client_nextthink = 0;
		}
		return;
	}

	Weapon_PlayEffects(idx, a, buttons);

	if (anim->frame >= 0)
	{
		ws_predicted.frame = anim->frame;
	}
	else
	{
		// A negative frame means cycle 1..|frame|.
		ws_predicted.frame++;
		if (ws_predicted.frame > -anim->frame)
			ws_predicted.frame = 1;
	}

	if (flags & ANIM_ATTACK)
	{
		if ((flags & ANIM_BRANCH) && weapons[idx].itemflag == IT_LIGHTNING)
			ws_predicted.attack_finished = ws_predicted.client_time + LG_ATTACK_TIME;
		else
			ws_predicted.attack_finished = ws_predicted.client_time
				+ Weapon_AttackTime(idx, ws_predicted.predflags);
	}

	ws_predicted.client_thinkindex = anim->next;

	if (anim->len)
		ws_predicted.client_nextthink = ws_predicted.client_time + anim->len;
	else
		ws_predicted.client_nextthink = 0;
}

// The server reproduces one scheduled transition per player frame, so we do the
// same: no catching up on several at once.
static void Weapon_Think(float buttons)
{
	float a = ws_predicted.client_thinkindex;
	float idx = ws_predicted.weapon_index;
	float held;

	if (!ws_predicted.client_nextthink)
		return;
	if (a < 0 || a >= weapons[idx].anim_count)
		return;
	if (ws_predicted.client_time < ws_predicted.client_nextthink)
		return;

	// The transition happens at its scheduled time, not at the end of the frame
	// that crossed it, so timings stay on the server's 100ms grid.
	held = ws_predicted.client_time;
	ws_predicted.client_time = ws_predicted.client_nextthink;
	ws_predicted.client_nextthink = 0;
	Weapon_EnterState(a, buttons);
	ws_predicted.client_time = held;
}

/*
 * The axe alternates between two swing animations. The server picks with a hash
 * of the fire time rather than a random number, so we can usually reach the same
 * answer.
 *
 * Usually, not always: the server evaluates this in double precision and QuakeC
 * has only floats, so late in a long match the product loses enough precision to
 * truncate to a different integer. The cost is that the swing occasionally pops
 * to the other chain when the snapshot lands, which is cosmetic.
 */
static float Weapon_AxeEntry(void)
{
	float t = ws_predicted.client_time;
	float r;

	r = fabs((((int)(t * 931.75) << 11) + ((int)t >> 6)) % 1000) / 1000;

	if ((r >= 0.25 && r < 0.5) || r >= 0.75)
		return 5;

	return 1;
}

static void Weapon_Attack(float buttons)
{
	float idx = ws_predicted.weapon_index;

	if (ws_predicted.client_time < ws_predicted.attack_finished)
		return;
	if (!(buttons & 1))
		return;

	for (float i = 0; i < weapons[idx].anim_count; i++)
	{
		anim_t *anim = &weapons[idx].anim[i];

		if (!(anim->flags & ANIM_DEFAULT))
			continue;

		// The idle state is not an attack entry for this weapon.
		if (!(anim->flags & ANIM_ATTACK))
			return;

		if (idx == 1)
			Weapon_EnterState(Weapon_AxeEntry(), buttons);
		else
			Weapon_EnterState(anim->next, buttons);

		ws_predicted.attack_finished = ws_predicted.client_time
			+ Weapon_AttackTime(idx, ws_predicted.predflags);
		return;
	}

	// Lightning has no idle state, so firing starts at state 0.
	if (weapons[idx].anim_count > 0 && (weapons[idx].anim[0].flags & ANIM_ATTACK))
		Weapon_EnterState(0, buttons);
}

// ==========================================================================
// Replay
// ==========================================================================

/*
 * Whether this weapon state can be predicted at all.
 *
 * Instagib's coilgun replaces the shotgun's model, sound and refire delay, but
 * the flag does not say which instagib mode is running and the delays differ
 * between them. Guessing would desync the animation and play the wrong sound
 * alongside the server's, so we stand aside and let the engine draw the gun.
 */
float Weapon_Predictable(float predflags)
{
	if (predflags == PRDFL_FORCEOFF)
		return FALSE;

	if (predflags & PRDFL_COILGUN)
		return FALSE;

	return TRUE;
}

static void Weapon_Simulate(void)
{
	float switched = SWITCH_APPLIED;
	float had_impulse = FALSE;

	/*
	 * The server has switched prediction off, usually because the round has not
	 * started or the player is hooked. Keep the clock moving and hold the
	 * weapon closed.
	 */
	if (!Weapon_Predictable(ws_predicted.predflags))
	{
		ws_predicted.client_time += input_timelength;
		ws_predicted.impulse = 0;
		ws_predicted.attack_finished = max(ws_predicted.attack_finished,
		                                   ws_predicted.client_time + 0.05);
		return;
	}

	if (!player_playable)
	{
		// Dead or spectating: the weapon clock does not advance at all.
		ws_predicted.impulse = 0;
		ws_predicted.attack_finished = ws_predicted.client_time + 0.05;
		return;
	}

	ws_predicted.client_time += input_timelength;

	if (input_impulse)
		ws_predicted.impulse = input_impulse;

	// Just after respawning the server discards attacks and switches.
	if (ws_predicted.client_time < respawn_guard_until)
		return;

	Weapon_Think(input_buttons);

	if (ws_predicted.impulse)
	{
		had_impulse = TRUE;
		switched = Weapon_Switch(ws_predicted.impulse);

		// A blocked switch stays queued until the animation ends.
		if (switched != SWITCH_BLOCKED)
			ws_predicted.impulse = 0;
	}

	// Do not fire the old weapon on a frame where a switch is waiting.
	if (had_impulse && switched == SWITCH_BLOCKED)
		return;

	Weapon_Attack(input_buttons);
}

void WeaponState_Replay(void)
{
	float threshold, last_shown;

	if (!ws_have_state)
		return;

	/*
	 * The player has just become playable again but every snapshot we hold
	 * predates that. Replaying now would animate, and fire the effects of, a
	 * shot belonging to the previous life.
	 */
	if (playable_pending)
	{
		ws_presented = ws_server;
		return;
	}

	// Too far behind to reconstruct: the input ring has wrapped.
	if (clientcommandframe - ws_state_frame >= PRED_MASK)
	{
		ws_presented = ws_server;
		return;
	}

	ws_predicted = ws_server;
	ws_presented = ws_server;

	/*
	 * Effects and the shown animation lag the newest prediction slightly, so a
	 * shot's sound, its projectile and its animation all appear together rather
	 * than the animation running ahead of the rest.
	 */
	threshold = bound(ws_state_frame + 1,
	                  clientcommandframe - (autocvar_cl_predict_buffer + 1),
	                  clientcommandframe - 1);

	last_shown = ws_state_frame;

	// The frame being assembled now has not been sent, so it is not replayed.
	for (float f = ws_state_frame + 1; f < clientcommandframe; f++)
	{
		if (!getinputstate(f))
			break;

		replay_frame = f;
		frame_may_sound = (f <= threshold) && (f > last_sound_frame);
		frame_may_projectile = (f <= threshold) && (f > last_projectile_frame);

		Weapon_Simulate();

		if (f <= threshold)
		{
			ws_presented = ws_predicted;
			last_shown = f;
		}
	}

	frame_may_sound = FALSE;
	frame_may_projectile = FALSE;

	if (autocvar_cl_predict_debug > 1)
	{
		print(sprintf("wpred: %g..%g thr %g shown %g idx %g frame %g\n",
		               ws_state_frame + 1, clientcommandframe - 1, threshold,
		               last_shown, ws_presented.weapon_index, ws_presented.frame));
	}
}

// ==========================================================================
// Presentation
// ==========================================================================

// Everything that would make the engine hide its own gun, plus the cases where
// we have nothing trustworthy to show.
static float WeaponState_Visible(void)
{
	if (!Pred_Enabled())
		return FALSE;
	if (!ws_have_state || playable_pending)
		return FALSE;
	if (!player_playable)
		return FALSE;
	if (!Weapon_Valid(ws_presented.weapon_index))
		return FALSE;
	if (!Weapon_Predictable(ws_presented.predflags))
		return FALSE;
	if (!cvar("r_drawviewmodel"))
		return FALSE;
	if ((getstatbits(STAT_ITEMS, 0, 23) & IT_INVISIBILITY) && !cvar("r_drawviewmodelinvis"))
		return FALSE;

	return TRUE;
}

// Returns true when we are drawing the gun ourselves.
float WeaponState_Present(void)
{
	float mdl, pose;

	if (!WeaponState_Visible())
	{
		viewweapon.modelindex = 0;
		return FALSE;
	}

	mdl = Weapon_ModelIndex(ws_presented.weapon_index);
	pose = ws_presented.frame;

	if (!mdl)
	{
		viewweapon.modelindex = 0;
		return FALSE;
	}

	// Start a new blend whenever the pose changes.
	if (viewweapon.modelindex != mdl)
	{
		viewweapon.modelindex = mdl;
		viewweapon.frame = pose;
		viewweapon.frame2 = pose;
		vw_lerptime = cltime;
	}
	else if (viewweapon.frame != pose)
	{
		viewweapon.frame2 = viewweapon.frame;
		viewweapon.frame = pose;
		vw_lerptime = cltime;
	}

	viewweapon.lerpfrac = bound(0, 1 - (cltime - vw_lerptime) * 10, 1);

	/*
	 * A view model entity is positioned in view space. The engine applies its
	 * own bob, but the user's gun offsets are ours to reproduce.
	 */
	viewweapon.origin = [cvar("cl_gunz"), -cvar("cl_gunx"), -cvar("cl_guny")];
	viewweapon.angles = [cvar("cl_gunanglex"), cvar("cl_gunangley"),
	                     cvar("cl_gunanglez")];

	addentity(viewweapon);
	return TRUE;
}
