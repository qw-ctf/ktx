/*
 * Projectiles, both the server's and our own predictions of them.
 *
 * A shot we predict appears immediately as a local projectile. A round trip
 * later the server's own copy arrives; if the two look like the same shot we
 * hand over, easing across the positional disagreement so the player does not
 * see the rocket jump, and keeping the trail unbroken.
 *
 * Motion is integrated here per rendered frame rather than left to the engine's
 * entity physics, which runs on a coarse fixed tick: a rocket leaving the
 * muzzle a few units from the camera would visibly stutter.
 *
 * Note the asymmetry between the two kinds of entity. The engine owns every
 * entity the server sent, caching it by entity number to route later updates,
 * so those must never be removed here. Local projectiles are ours alone and can
 * be removed freely.
 */

enum
{
	PROJ_LINEAR,	// rockets, nails: straight line
	PROJ_GRENADE	// bounces under gravity
};

.float	proj_kind;

.vector	s_origin;		// last authoritative position
.vector	s_velocity;
/*
 * When that position was received, on our clock.
 *
 * The payload also carries the server's own timestamp, but that is the mod's
 * clock and shares no epoch with anything csqc can read, so subtracting one
 * from the other yields nonsense. Treating the moment the update arrives as
 * the moment it describes costs at most the packet's age and cannot drift.
 */
.float	s_recv;

.float	owner_num;		// entity number of the player who fired
.float	is_local;		// our own prediction, not an entity the server sent

.vector	start;			// muzzle position, for linear reconstruction
.float	starttime;		// server time it left the muzzle
.float	endtime;		// server time a local projectile gives up at

.vector	sim_origin;		// grenade simulation, carried between frames
.vector	sim_angles;
.float	sim_time;
.float	angle_time;
.float	at_rest;

.float	trail_fx;
.vector	trail_org;		// last point we emitted trail from
.float	trail_time;
.float	trail_seeded;

.vector	draw_origin;		// where this frame puts it
.vector	last_draw_origin;
.float	have_draw_origin;
.float	debug_count;

// Easing applied after taking over from a local prediction.
.float	handoff_from;
.float	handoff_until;
.vector	handoff_delta;

// ==========================================================================
// Reading
//
// A vector constructor evaluates its components left to right, so these read
// the three words in the order the server wrote them.
// ==========================================================================

static vector Projectile_ReadVector(void)
{
	return [ReadCoord(), ReadCoord(), ReadCoord()];
}

static vector Projectile_ReadAngles(void)
{
	return [ReadAngle(), ReadAngle(), ReadAngle()];
}

// ==========================================================================
// Trails
// ==========================================================================

/*
 * Pick the trail the user asked for, the way the engine does for its own
 * projectiles: 0 none, 1 or unset the model's natural trail, anything else the
 * name of a particle effect.
 */
static float Projectile_TrailEffect(float is_grenade)
{
	string want = cvar_string(is_grenade ? "r_grenadetrail" : "r_rockettrail");

	if (want == "0")
		return 0;

	if (want != "" && want != "1" && !stof(want))
		return particleeffectnum(want);

	return particleeffectnum(is_grenade ? "TR_GRENADE" : "TR_ROCKET");
}

static void Projectile_EmitTrail(vector org)
{
	if (!self.trail_fx)
		return;

	if (!self.trail_seeded)
	{
		self.trail_org = org;
		self.trail_seeded = TRUE;
		return;
	}

	// Space emissions out so a high frame rate does not pile particles up.
	if (cltime < self.trail_time)
		return;

	trailparticles(self.trail_fx, self, self.trail_org, org);
	self.trail_org = org;
	self.trail_time = cltime + TRAIL_PERIOD;
}

// ==========================================================================
// Motion
// ==========================================================================

/*
 * Restrict a projectile's traces to the world.
 *
 * QuakeWorld marks every packet entity solid on the client, so an unrestricted
 * trace also stops on players and items. Only the server decides what a
 * projectile hits; a local prediction that died on a team mate standing beside
 * you would just disappear for no reason the player can see. Clearing the low
 * bit of dimension_hit drops network entities from the trace, at the cost of
 * passing through doors and platforms.
 */
static void Projectile_TraceWorldOnly(entity e)
{
	e.dimension_hit = 254;
}

// True when a trace hit something whose behaviour we can anticipate: the world,
// or a door or platform. A hit on a player or an item is not predictable.
static float Projectile_HitPredictable(void)
{
	string mdl;

	if (!trace_networkentity)
		return TRUE;

	mdl = modelnameforindex(getentity(trace_networkentity, GE_MODELINDEX));
	return substring(mdl, 0, 1) == "*";
}

static vector Projectile_ClipVelocity(vector vel, vector normal, float overbounce)
{
	float backoff = (vel * normal) * overbounce;
	vector out = vel - normal * backoff;

	for (float i = 0; i < 3; i++)
	{
		if (out[i] > -0.1 && out[i] < 0.1)
			out[i] = 0;
	}

	return out;
}

/*
 * Grenade flight. Gravity is applied in two halves either side of the move,
 * which is what the server's physics does, so the arcs agree.
 */
static void Projectile_Bounce(float dt, float spin)
{
	float movetime = dt, bumps;
	float bouncestop = (60 / 800) * sv_gravity;
	vector to, org = self.sim_origin;
	float d;

	if (self.at_rest)
	{
		// A grenade at rest stays put until the server says it moved again.
		if (self.velocity_z >= 1 / 32)
			self.at_rest = FALSE;
		else
			return;
	}

	self.velocity_z -= 0.5 * dt * sv_gravity;

	if (spin)
	{
		self.sim_angles += self.avelocity * dt;
		self.angles = self.sim_angles;
	}

	for (bumps = 0; bumps < BOUNCE_MAX_BUMPS && movetime > 0; bumps++)
	{
		to = org + self.velocity * movetime;
		traceline(org, to, MOVE_NOMONSTERS, self);
		org = trace_endpos;

		if (trace_fraction == 1)
			break;

		movetime *= 1 - min(1, trace_fraction);
		self.velocity = Projectile_ClipVelocity(self.velocity, trace_plane_normal,
		                                        1 + BOUNCE_FACTOR);

		d = trace_plane_normal * self.velocity;

		// Settle once it is only trickling along a floor.
		if (trace_plane_normal_z > 0.7 && d < bouncestop && d > -bouncestop)
		{
			self.at_rest = TRUE;
			self.velocity = '0 0 0';
			self.avelocity = '0 0 0';
			break;
		}

		self.at_rest = FALSE;
	}

	if (!self.at_rest)
		self.velocity_z -= 0.5 * dt * sv_gravity;

	self.sim_origin = org;
}

// Ease out whatever disagreement was left when we took over a prediction.
static vector Projectile_Smoothed(vector authoritative)
{
	float frac, ease;

	if (cltime >= self.handoff_until)
		return authoritative;

	frac = (cltime - self.handoff_from) / (self.handoff_until - self.handoff_from);
	frac = bound(0, frac, 1);
	ease = frac * frac * (3 - 2 * frac);

	return authoritative - self.handoff_delta * (1 - ease);
}

/*
 * Advance a prediction of our own. Returns false once it should stop existing:
 * it has served its purpose, or it has run into something.
 */
static float Projectile_MoveLocal(void)
{
	vector ahead, from;
	float dt;

	// A prediction only has to last until the server's copy could arrive.
	if (time >= self.endtime)
	{
		if (autocvar_cl_predict_debug)
			print("proj: local expired unclaimed\n");
		return FALSE;
	}

	if (self.proj_kind == PROJ_GRENADE)
	{
		dt = max(0, time - self.sim_time);
		self.sim_time = time;
		if (dt > 0)
			Projectile_Bounce(dt, TRUE);
		self.draw_origin = self.sim_origin;
		return TRUE;
	}

	self.draw_origin = self.start + self.velocity * (time - self.starttime);

	ahead = self.draw_origin + self.velocity * 0.02;
	from = self.have_draw_origin ? self.last_draw_origin : self.start;

	traceline(from, ahead, MOVE_NOMONSTERS, self);
	if (trace_fraction < 1)
	{
		if (autocvar_cl_predict_debug)
			print("proj: local hit world\n");
		return FALSE;
	}

	return TRUE;
}

/*
 * Grenades are corrected every server frame, because their bouncing cannot be
 * reproduced from the initial snapshot alone. Simulate forward from the last
 * correction rather than dead reckoning.
 */
static void Projectile_MoveServerGrenade(void)
{
	float dt;

	dt = max(0, time - self.sim_time);
	self.sim_time = time;
	if (dt > 0)
		Projectile_Bounce(dt, FALSE);

	// Spin runs on render time so it stays smooth between corrections.
	dt = max(0, cltime - self.angle_time);
	self.angle_time = cltime;
	if (dt > 0)
	{
		self.sim_angles += self.avelocity * min(dt, 0.05);
		self.angles = self.sim_angles;
	}

	self.draw_origin = Projectile_Smoothed(self.sim_origin);
}

// Everything else flies straight, so carry it on from the last known position.
static void Projectile_MoveServerLinear(void)
{
	self.draw_origin = Projectile_Smoothed(self.s_origin
	                                       + self.s_velocity * (time - self.s_recv));
}

float Projectile_Predraw(void)
{
	if (!self.modelindex)
		return PREDRAW_NEXT;

	if (self.is_local)
	{
		if (!Pred_ProjectilesEnabled())
			return PREDRAW_NEXT;

		if (!Projectile_MoveLocal())
		{
			remove(self);
			return PREDRAW_NEXT;
		}
	}
	else if (self.proj_kind == PROJ_GRENADE)
	{
		Projectile_MoveServerGrenade();
	}
	else
	{
		Projectile_MoveServerLinear();
	}

	setorigin(self, self.draw_origin);

	// Level 2 traces the first few frames of flight; level 1 keeps to the events
	// worth noticing, so it stays readable while playing.
	if (autocvar_cl_predict_debug > 1 && self.debug_count < 5)
	{
		self.debug_count++;
		print(sprintf("proj: %s #%g at %s age %g\n",
		              self.is_local ? "local" : "server",
		              self.entnum, vtos(self.draw_origin), time - self.s_recv));
	}

	self.last_draw_origin = self.draw_origin;
	self.have_draw_origin = TRUE;

	// Our own projectile starts inside our own face; drawing it there only
	// flickers, so carry the trail along without rendering it.
	if (self.owner_num == player_localentnum
		&& vlen(self.draw_origin - getproperty(VF_ORIGIN)) < TOO_CLOSE_RADIUS)
	{
		self.trail_org = self.draw_origin;
		return PREDRAW_NEXT;
	}

	Projectile_EmitTrail(self.draw_origin);

	if (self.modelflags & MF_ROCKET)
		dynamiclight_add(self.draw_origin, 200 * cvar("r_rocketlight"), '0.4 0.2 0.05');

	return PREDRAW_AUTOADD;
}

// ==========================================================================
// Local (predicted) projectiles
// ==========================================================================

/*
 * Returns true when the shot has been dealt with, so the replay does not keep
 * retrying it on later frames.
 */
float Projectile_SpawnLocal(float widx, float a)
{
	entity proj, oself;
	vector org, vel, off;
	float speed;

	if (!Pred_ProjectilesEnabled())
		return TRUE;

	if (autocvar_cl_nopred_weapon)
		return FALSE;

	/*
	 * Without antilag the server will not rewind to meet us, so a projectile
	 * drawn here would not be where the server puts it.
	 */
	if (!ws_predicted.ping)
		return FALSE;

	if (weapons[widx].anim[a].proj_model == "")
		return FALSE;

	makevectors(input_angles);

	off = Weapon_ProjectileOffset(widx, ws_predicted.predflags, weapons[widx].anim[a].proj_off);
	speed = Weapon_ProjectileSpeedScale(widx, ws_predicted.predflags);

	org = Pred_OriginForFrame(replay_frame);
	org += v_right * off_x + v_forward * off_y;
	org_z += off_z;

	vel = weapons[widx].anim[a].proj_vel;
	vel = (v_right * vel_x + v_forward * vel_y + v_up * vel_z) * speed;

	proj = spawn();
	setmodel(proj, weapons[widx].anim[a].proj_model);
	Projectile_TraceWorldOnly(proj);
	proj.drawmask = MASK_ENGINE;
	proj.predraw = Projectile_Predraw;
	proj.is_local = TRUE;
	proj.owner_num = player_localentnum;
	proj.velocity = vel;
	proj.angles = vectoangles(vel);
	proj.endtime = time + max(ws_predicted.ping + TRAIL_PERIOD, 0.05);
	proj.s_recv = time;	// so its reported age means time since it was fired

	if (proj.modelflags & MF_GRENADE)
	{
		proj.proj_kind = PROJ_GRENADE;
		proj.avelocity = GRENADE_SPIN;
		proj.sim_origin = org;
		proj.sim_angles = proj.angles;
		proj.sim_time = time;
		proj.angle_time = cltime;
		proj.trail_fx = Projectile_TrailEffect(TRUE);

		// The server advances a newly spawned missile by one frame before
		// anyone sees it; match that so the arcs line up.
		oself = self;
		self = proj;
		Projectile_Bounce(NEWMIS_STEP, TRUE);
		self = oself;
		org = proj.sim_origin;
	}
	else
	{
		proj.proj_kind = PROJ_LINEAR;
		proj.start = org;
		proj.starttime = time - NEWMIS_STEP;
		org += vel * NEWMIS_STEP;

		if (proj.modelflags & MF_ROCKET)
			proj.trail_fx = Projectile_TrailEffect(FALSE);
	}

	setorigin(proj, org);
	proj.trail_org = org;
	proj.trail_seeded = TRUE;
	proj.last_draw_origin = org;
	proj.have_draw_origin = TRUE;

	if (autocvar_cl_predict_debug)
		print(sprintf("proj: predicted %s on frame %g\n",
		               weapons[widx].anim[a].proj_model, replay_frame));

	return TRUE;
}

// ==========================================================================
// Hand-off
// ==========================================================================

/*
 * A server projectile has arrived. If one of ours looks like the same shot,
 * carry our visual continuity onto the server's entity and drop ours. The
 * server entity always survives: the engine owns it.
 */
static void Projectile_TakeOver(entity server_proj, vector server_origin)
{
	entity best = world;
	vector diff, dir, local_org, smooth_from;
	float par, perp, dist, best_dist = 0, speed, ease;

	if (server_proj.owner_num != player_localentnum)
		return;

	for (entity e = world; (e = findfloat(e, is_local, TRUE)); )
	{
		if (e.owner_num != server_proj.owner_num)
			continue;
		if (e.modelindex != server_proj.modelindex)
			continue;
		if (time > e.endtime + 0.1)
			continue;

		local_org = (e.proj_kind == PROJ_GRENADE)
			? e.sim_origin
			: e.start + e.velocity * (time - e.starttime);

		diff = server_origin - local_org;
		dist = vlen(diff);

		if (e.proj_kind == PROJ_GRENADE)
		{
			if (dist > HANDOFF_GRENADE_DIST)
				continue;
		}
		else if (dist > HANDOFF_DIST)
		{
			/*
			 * Further apart than we would like, but still on our flight line
			 * and only ahead or behind along it. That is latency, not a
			 * different shot.
			 */
			dir = normalize(e.velocity);
			par = diff * dir;
			perp = vlen(diff - dir * par);

			if (perp >= HANDOFF_PERP || fabs(par) > HANDOFF_PHASE)
				continue;
		}

		if (best == world || dist < best_dist)
		{
			best = e;
			best_dist = dist;
		}
	}

	if (best == world)
		return;

	smooth_from = best.have_draw_origin ? best.last_draw_origin : server_origin;
	speed = max(1, vlen(best.velocity));
	ease = best.have_draw_origin
		? bound(HANDOFF_MIN_TIME, best_dist / speed * 6, HANDOFF_MAX_TIME)
		: bound(0.026, best_dist / speed * 0.5, 0.05);

	// Continue the trail from where ours had reached, and walk the rendered
	// position across to the server's over the easing window.
	server_proj.trail_org = best.trail_org;
	server_proj.trail_seeded = best.trail_seeded;
	server_proj.trail_time = best.trail_time;
	server_proj.last_draw_origin = smooth_from;
	server_proj.have_draw_origin = TRUE;

	server_proj.handoff_from = cltime;
	server_proj.handoff_until = cltime + ease;
	server_proj.handoff_delta = server_origin - smooth_from;

	remove(best);

	if (autocvar_cl_predict_debug)
		print(sprintf("proj: took over, %g units out, easing %gs\n", best_dist, ease));
}

// ==========================================================================
// Parsing
// ==========================================================================

void Projectile_Parse(float isnew)
{
	float sendflags = ReadByte();
	vector spawn_org, here;
	float had_origin = FALSE;

	if (isnew)
	{
		self.drawmask = MASK_ENGINE;
		self.predraw = Projectile_Predraw;
		self.is_local = FALSE;
		Projectile_TraceWorldOnly(self);
	}

	if (sendflags & PROJECTILE_ORIGIN)
	{
		self.s_origin = Projectile_ReadVector();
		self.s_velocity = Projectile_ReadVector();
		ReadFloat();	// the server's own clock; see .s_recv
		self.s_recv = time;
		had_origin = TRUE;
	}

	if (sendflags & PROJECTILE_MODEL)
	{
		// Set the model by name so its flags come with it; those tell us
		// whether this is a rocket or a grenade.
		setmodel(self, modelnameforindex(ReadShort()));
		self.effects = ReadShort();

		if (self.modelflags & MF_GRENADE)
		{
			self.proj_kind = PROJ_GRENADE;
			self.avelocity = GRENADE_SPIN;
			self.trail_fx = Projectile_TrailEffect(TRUE);
		}
		else
		{
			self.proj_kind = PROJ_LINEAR;
			if (self.modelflags & MF_ROCKET)
				self.trail_fx = Projectile_TrailEffect(FALSE);
		}
	}

	if (sendflags & PROJECTILE_ANGLES)
		self.angles = Projectile_ReadAngles();

	if (sendflags & PROJECTILE_OWNER)
		self.owner_num = ReadShort();

	if (sendflags & PROJECTILE_SPAWN_ORIGIN)
	{
		spawn_org = Projectile_ReadVector();

		// Start the trail at the muzzle, but only when that is plausibly where
		// this projectile came from.
		if (isnew && vlen(spawn_org - self.s_origin) < 150)
		{
			self.trail_org = spawn_org;
			self.trail_seeded = TRUE;
		}
	}

	if (self.proj_kind == PROJ_GRENADE && had_origin)
	{
		/*
		 * Re-seat the simulation on every correction. Grenades are resent each
		 * server frame precisely because their bouncing cannot be reproduced
		 * from the initial snapshot alone.
		 */
		self.sim_origin = self.s_origin;
		self.velocity = self.s_velocity;
		self.sim_time = self.s_recv;
		self.at_rest = vlen(self.s_velocity) < 1;
		self.avelocity = self.at_rest ? '0 0 0' : GRENADE_SPIN;

		if (isnew)
		{
			self.sim_angles = self.angles;
			self.angle_time = cltime;
		}
	}

	if (isnew && autocvar_cl_predict_debug)
	{
		print(sprintf("proj: server #%g model %g owner %g\n",
		              self.entnum, self.modelindex, self.owner_num));
	}

	if (isnew)
	{
		here = (self.proj_kind == PROJ_GRENADE)
			? self.s_origin
			: self.s_origin + self.s_velocity * (time - self.s_recv);

		setorigin(self, here);
		self.last_draw_origin = here;
		self.have_draw_origin = TRUE;

		if (!self.trail_seeded)
		{
			self.trail_org = here;
			self.trail_seeded = TRUE;
		}

		Projectile_TakeOver(self, here);
	}
}
