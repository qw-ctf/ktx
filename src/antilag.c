//========================================================================
//
//  Copyright (C)  2020 - 2021	Samuel Piper
//
//  This code is free software; you can redistribute it and/or modify
//  it under the terms of the GNU GPL (General Public License); either
//  version 2 of the License, or (at your option) any later version.
//
//========================================================================
#include "g_local.h"
#include "fb_globals.h"

int ANTILAG_MEMPOOL_WORLDSEEK;
antilag_t ANTILAG_MEMPOOL[ANTILAG_MAXEDICTS];
antilag_t *antilag_list_players;
antilag_t *antilag_list_world;
float antilag_nextthink_world;
vec3_t antilag_origin;
vec3_t antilag_retvec;

void Physics_PushEntityTrace(float push_x, float push_y, float push_z)
{
	vec3_t push = { push_x, push_y, push_z };
	vec3_t end;

	VectorAdd(self->s.v.origin, push, end);

	traceline(PASSVEC3(self->s.v.origin), PASSVEC3(end), false, self);
}

float Physics_PushEntity(float push_x, float push_y, float push_z, int failonstartsolid) // SV_PushEntity
{
	vec3_t push = { push_x, push_y, push_z };

	Physics_PushEntityTrace(PASSVEC3(push));

	if (g_globalvars.trace_startsolid && failonstartsolid)
		return g_globalvars.trace_fraction;

	trap_setorigin(NUM_FOR_EDICT(self), PASSVEC3(g_globalvars.trace_endpos));

	if (g_globalvars.trace_fraction < 1)
	{
		if (self->s.v.solid >= SOLID_TRIGGER && (!((int)self->s.v.flags & FL_ONGROUND) || (self->s.v.groundentity != g_globalvars.trace_ent)))
		{
			other = PROG_TO_EDICT(g_globalvars.trace_ent);
			((void(*)())(self->touch))();
		}
	}

	return g_globalvars.trace_fraction;
}

#define MAX_CLIP_PLANES 5
void Physics_ClipVelocity(float vel_x, float vel_y, float vel_z, float norm_x, float norm_y, float norm_z, float f) // SV_ClipVelocity
{
	vec3_t vel = { vel_x, vel_y, vel_z };
	vec3_t norm = { norm_x, norm_y, norm_z };

	vec3_t vel2;
	VectorScale(norm, DotProduct(vel, norm), vel2);
	VectorScale(vel2, f, vel2);
	VectorSubtract(vel, vel2, vel);

	if (vel[0] > -0.1 && vel[0] < 0.1) vel[0] = 0;
	if (vel[1] > -0.1 && vel[1] < 0.1) vel[1] = 0;
	if (vel[2] > -0.1 && vel[2] < 0.1) vel[2] = 0;

	VectorCopy(vel, antilag_retvec);
}

void Physics_Bounce(float dt)
{
	float gravity_value = cvar("sv_gravity");

	if ((int)self->s.v.flags & FL_ONGROUND)
	{
		if (self->s.v.velocity[2] >= 1 / 32)
			self->s.v.flags = (int)self->s.v.flags &~ FL_ONGROUND;
		else if (!self->s.v.groundentity)
			return;
	}


	if (self->gravity)
		self->s.v.velocity[2] -= 0.5 * dt * self->gravity * gravity_value;
	else
		self->s.v.velocity[2] -= 0.5 * dt * gravity_value;

	VectorMA(self->s.v.angles, dt, self->s.v.avelocity, self->s.v.angles);

	float movetime, bump;
	movetime = dt;
	for (bump = 0; bump < MAX_CLIP_PLANES && movetime > 0; ++bump)
	{
		vec3_t move;
		VectorScale(self->s.v.velocity, movetime, move);
		Physics_PushEntity(PASSVEC3(move), true);

		if (g_globalvars.trace_fraction == 1)
			break;

		movetime *= 1 - min(1, g_globalvars.trace_fraction);

		float d, bouncefac, bouncestp;

		bouncefac = 0.5;
		bouncestp = 60 / 800;
		if (self->gravity)
			bouncestp *= self->gravity * gravity_value;
		else
			bouncestp *= gravity_value;

		Physics_ClipVelocity(PASSVEC3(self->s.v.velocity), PASSVEC3(g_globalvars.trace_plane_normal), 1 + bouncefac);
		VectorCopy(antilag_retvec, self->s.v.velocity);

		d = DotProduct(g_globalvars.trace_plane_normal, self->s.v.velocity);
		if (g_globalvars.trace_plane_normal[2] > 0.7 && d < bouncestp && d > -bouncestp)
		{
			self->s.v.flags = (int)self->s.v.flags | FL_ONGROUND;
			self->s.v.groundentity = g_globalvars.trace_ent;
			VectorClear(self->s.v.velocity);
			VectorClear(self->s.v.avelocity);
		}
		else
			self->s.v.flags = (int)self->s.v.flags &~ FL_ONGROUND;
	}

	if (!((int)self->s.v.flags & FL_ONGROUND))
	{
		if (self->gravity)
			self->s.v.velocity[2] -= 0.5 * dt * self->gravity * gravity_value;
		else
			self->s.v.velocity[2] -= 0.5 * dt * gravity_value;
	}
}

void antilag_log(gedict_t *e, antilag_t *antilag)
{
	// stop extremely fast logging
	if (g_globalvars.time - antilag->rewind_time[antilag->rewind_seek] < 0.01)
		return;

	antilag->rewind_seek++;

	if (antilag->rewind_seek >= ANTILAG_MAXSTATES)
		antilag->rewind_seek = 0;

	VectorCopy(e->s.v.origin, antilag->rewind_origin[antilag->rewind_seek]);
	VectorCopy(e->s.v.velocity, antilag->rewind_velocity[antilag->rewind_seek]);
	antilag->rewind_time[antilag->rewind_seek] = g_globalvars.time;

	if ((int) e->s.v.flags & FL_ONGROUND)
		antilag->rewind_platform_edict[antilag->rewind_seek] = e->s.v.groundentity;
	else
		antilag->rewind_platform_edict[antilag->rewind_seek] = 0;
}

antilag_t *antilag_create_player(gedict_t *e)
{
	antilag_t *new_datastruct = &ANTILAG_MEMPOOL[NUM_FOR_EDICT(e)];
	memset(new_datastruct, 0, sizeof(antilag_t));
	new_datastruct->prev = NULL;
	new_datastruct->next = NULL;
	new_datastruct->owner = e;

	if (antilag_list_players != NULL)
	{
		new_datastruct->next = antilag_list_players;
		antilag_list_players->prev = new_datastruct;
	}

	antilag_list_players = new_datastruct;

	return new_datastruct;
}

void antilag_delete_player(gedict_t *e)
{
	antilag_t *data = e->antilag_data;
	if (data->prev != NULL)
	{
		data->prev->next = data->next;
	}
	else if (antilag_list_players == data)
	{
		antilag_list_players = data->next;
	}

	if (data->next != NULL)
		data->next->prev = data->prev;
}

antilag_t *antilag_create_world(gedict_t *e)
{
	antilag_t *new_datastruct = &ANTILAG_MEMPOOL[64 + ANTILAG_MEMPOOL_WORLDSEEK];
	memset(new_datastruct, 0, sizeof(antilag_t));
	ANTILAG_MEMPOOL_WORLDSEEK++;

	new_datastruct->prev = NULL;
	new_datastruct->next = NULL;
	new_datastruct->owner = e;

	if (antilag_list_world != NULL)
	{
		new_datastruct->next = antilag_list_world;
		antilag_list_world->prev = new_datastruct;
	}

	antilag_list_world = new_datastruct;

	return new_datastruct;
}

void antilag_delete_world(gedict_t *e)
{
	antilag_t *data = e->antilag_data;
	if (data->prev != NULL)
	{
		data->prev->next = data->next;
	}
	else if (antilag_list_world == data)
	{
		antilag_list_world = data->next;
	}

	if (data->next != NULL)
		data->next->prev = data->prev;
}

void antilag_updateworld()
{
	if (g_globalvars.time < antilag_nextthink_world)
		return;

	antilag_nextthink_world = g_globalvars.time + 0.05;
	
	antilag_t *list;
	for (list = antilag_list_world; list != NULL; list = list->next)
	{
		antilag_log(list->owner, list);
	}
}

void antilag_lagmove(antilag_t *data, float goal_time)
{
	//don't rewind past spawns
	goal_time = max(goal_time, data->owner->spawn_time);

	int old_seek = data->rewind_seek;
	int seek = data->rewind_seek - 1;
	if (seek < 0)
		seek = ANTILAG_MAXSTATES - 1;

	float seek_time = data->rewind_time[seek];
	while (seek != data->rewind_seek && seek_time > goal_time)
	{
		old_seek = seek;
		seek--;
		if (seek < 0)
			seek = ANTILAG_MAXSTATES - 1;
		seek_time = data->rewind_time[seek];
	}

	float under_time = data->rewind_time[old_seek];
	float over_time = data->rewind_time[seek];
	float frac = (goal_time - over_time) / (under_time - over_time);

	gedict_t *owner = data->owner;

	vec3_t lerp_origin;
	if (frac <= 1)
	{
		vec3_t diff;
		VectorSubtract(data->rewind_origin[old_seek], data->rewind_origin[seek], diff);

		if (VectorLength(diff) > 48) // whoops, maybe we teleported?
			frac = 1;

		VectorScale(diff, frac, diff);
		VectorAdd(data->rewind_origin[seek], diff, lerp_origin);
	}
	else
	{
		float frac = (goal_time - over_time) / (g_globalvars.time - over_time);
		frac = min(frac, 1);

		vec3_t diff;
		VectorSubtract(owner->s.v.origin, data->rewind_origin[data->rewind_seek], diff);

		if (VectorLength(diff) > 48) // whoops, maybe we teleported?
			frac = 1;

		VectorScale(diff, frac, diff);
		VectorAdd(data->rewind_origin[data->rewind_seek], diff, lerp_origin);
	}

	trap_setorigin(NUM_FOR_EDICT(owner), PASSVEC3(lerp_origin));
}

void antilag_getorigin(antilag_t *data, float goal_time)
{
	int old_seek = data->rewind_seek;
	int seek = data->rewind_seek - 1;
	if (seek < 0)
		seek = ANTILAG_MAXSTATES - 1;

	float seek_time = data->rewind_time[seek];
	while (seek != data->rewind_seek && seek_time > goal_time)
	{
		old_seek = seek;
		seek--;
		if (seek < 0)
			seek = ANTILAG_MAXSTATES - 1;
		seek_time = data->rewind_time[seek];
	}

	float under_time = data->rewind_time[old_seek];
	float over_time = data->rewind_time[seek];
	float frac = (goal_time - over_time) / (under_time - over_time);

	gedict_t *owner = data->owner;

	vec3_t lerp_origin;
	if (frac <= 1)
	{
		vec3_t diff;
		VectorSubtract(data->rewind_origin[old_seek], data->rewind_origin[seek], diff);
		VectorScale(diff, frac, diff);
		VectorAdd(data->rewind_origin[seek], diff, lerp_origin);
	}
	else
	{
		float frac = (goal_time - over_time) / (g_globalvars.time - over_time);
		vec3_t diff;
		VectorSubtract(owner->s.v.origin, data->rewind_origin[data->rewind_seek], diff);
		VectorScale(diff, frac, diff);
		VectorAdd(data->rewind_origin[data->rewind_seek], diff, lerp_origin);
	}

	VectorCopy(lerp_origin, antilag_origin);
}

int antilag_getseek(antilag_t *data, float ms)
{
	float goal_time = g_globalvars.time - ms;

	int seek = data->rewind_seek - 1;
	if (seek < 0)
		seek = ANTILAG_MAXSTATES - 1;

	float seek_time = data->rewind_time[seek];
	while (seek != data->rewind_seek && seek_time > goal_time)
	{
		seek--;
		if (seek < 0)
			seek = ANTILAG_MAXSTATES - 1;
		seek_time = data->rewind_time[seek];
	}

	return seek;
}

void antilag_lagmove_all(gedict_t *e, float ms)
{
	float rewind_time = g_globalvars.time - ms;
	time_corrected = rewind_time;

	antilag_t *list;
	for (list = antilag_list_players; list != NULL; list = list->next)
	{
		if (list->owner->s.v.health <= 0)
			continue;

		VectorCopy(list->owner->s.v.origin, list->held_origin);
		VectorCopy(list->owner->s.v.velocity, list->held_velocity);

		if (list->owner == e)
		{
			int lag_platform = list->rewind_platform_edict[antilag_getseek(list, ms)];
			if (lag_platform)
			{
				gedict_t *plat = PROG_TO_EDICT(lag_platform);
				if (plat->antilag_data != NULL)
				{
					vec3_t diff;
					VectorClear(diff);
					antilag_getorigin(plat->antilag_data, rewind_time);
					VectorSubtract(antilag_origin, plat->s.v.origin, diff);

					vec3_t org;
					VectorAdd(e->s.v.origin, diff, org);

					trap_setorigin(NUM_FOR_EDICT(e), PASSVEC3(org));
				}
			}
			continue;
		}

		antilag_lagmove(list, rewind_time);
	}

	for (list = antilag_list_world; list != NULL; list = list->next)
	{
		VectorCopy(list->owner->s.v.origin, list->held_origin);
		VectorCopy(list->owner->s.v.velocity, list->held_velocity);
		antilag_lagmove(list, rewind_time);
	}
}

void antilag_lagmove_all_nohold(gedict_t *e, float ms, int plat_rewind)
{
	float rewind_time = g_globalvars.time - ms;
	time_corrected = rewind_time;

	antilag_t *list;
	for (list = antilag_list_players; list != NULL; list = list->next)
	{
		if (list->owner->s.v.health <= 0)
			continue;

		if (list->owner == e)
		{
			if (plat_rewind)
			{
				int lag_platform = list->rewind_platform_edict[antilag_getseek(list, ms)];
				if (lag_platform)
				{
					gedict_t *plat = PROG_TO_EDICT(lag_platform);
					if (plat->antilag_data != NULL)
					{
						vec3_t diff;
						VectorClear(diff);
						antilag_getorigin(plat->antilag_data, rewind_time);
						VectorSubtract(antilag_origin, plat->s.v.origin, diff);

						vec3_t org;
						VectorAdd(e->s.v.origin, diff, org);

						trap_setorigin(NUM_FOR_EDICT(e), PASSVEC3(org));
					}
				}
			}
			continue;
		}

		antilag_lagmove(list, rewind_time);
	}

	for (list = antilag_list_world; list != NULL; list = list->next)
	{
		antilag_lagmove(list, rewind_time);
	}
}

void antilag_unmove_all()
{
	if (cvar("sv_antilag") != 1)
		return;

	time_corrected = g_globalvars.time;

	antilag_t *list;
	for (list = antilag_list_players; list != NULL; list = list->next)
	{
		if (list->owner->s.v.health <= 0)
			continue;

		trap_setorigin(NUM_FOR_EDICT(list->owner), PASSVEC3(list->held_origin));
	}

	for (list = antilag_list_world; list != NULL; list = list->next)
	{
		trap_setorigin(NUM_FOR_EDICT(list->owner), PASSVEC3(list->held_origin));
	}
}

void antilag_lagmove_all_hitscan(gedict_t *e)
{
	if (cvar("sv_antilag") != 1)
		return;

	float ms = (atof(ezinfokey(e, "ping")) / 1000) - 0.026;

	if (ms > ANTILAG_REWIND_MAXHITSCAN)
		ms = ANTILAG_REWIND_MAXHITSCAN;
	else if (ms < 0)
		ms = 0;

	antilag_lagmove_all(e, ms);
}

void antilag_lagmove_all_proj(gedict_t *owner, gedict_t *e)
{
	if (cvar("sv_antilag") != 1)
		return;

	float ms = (atof(ezinfokey(owner, "ping")) / 1000) - 0.026;

	if (ms > ANTILAG_REWIND_MAXPROJECTILE)
		ms = ANTILAG_REWIND_MAXPROJECTILE;
	else if (ms < 0)
		ms = 0;

	// log hold stats, because we use nohold antilag moving
	antilag_t *list;
	for (list = antilag_list_players; list != NULL; list = list->next)
	{
		if (list->owner->s.v.health <= 0)
			continue;

		VectorCopy(list->owner->s.v.origin, list->held_origin);
		VectorCopy(list->owner->s.v.velocity, list->held_velocity);
	}

	for (list = antilag_list_world; list != NULL; list = list->next)
	{
		VectorCopy(list->owner->s.v.origin, list->held_origin);
		VectorCopy(list->owner->s.v.velocity, list->held_velocity);
	}

	vec3_t old_org;
	VectorCopy(owner->s.v.origin, old_org);
	antilag_lagmove_all_nohold(owner, ms, true);
	VectorSubtract(owner->s.v.origin, old_org, old_org);
	VectorAdd(e->s.v.origin, old_org, old_org);
	trap_setorigin(NUM_FOR_EDICT(e), PASSVEC3(old_org));
	VectorCopy(e->s.v.origin, e->oldangles); // store for later maybe
	e->s.v.armorvalue = ms;

	gedict_t *oself = self;

	float step_time = min(0.05, ms);
	float current_time = g_globalvars.time - ms;
	while (current_time < g_globalvars.time)
	{
		step_time = bound(0.01, min(step_time, (g_globalvars.time - current_time) - 0.01), 0.05);
		if (e->s.v.nextthink) { e->s.v.nextthink -= step_time; }

		antilag_lagmove_all_nohold(owner, (g_globalvars.time - current_time), false);
		traceline(PASSVEC3(e->s.v.origin), e->s.v.origin[0] + e->s.v.velocity[0] * step_time,
			e->s.v.origin[1] + e->s.v.velocity[1] * step_time, e->s.v.origin[2] + e->s.v.velocity[2] * step_time,
			false, e);

		trap_setorigin(NUM_FOR_EDICT(e), PASSVEC3(g_globalvars.trace_endpos));

		if (g_globalvars.trace_fraction < 1)
		{
			//if (g_globalvars.trace_ent)
			//{
			other = PROG_TO_EDICT(g_globalvars.trace_ent);
			self = e;
			self->s.v.flags = ((int)self->s.v.flags) | FL_GODMODE;
			((void(*)())(self->touch))();
			break;
			//}
		}

		current_time += step_time;
	}

	self = oself;

	// restore origins to held values
	antilag_unmove_all();
}


void antilag_lagmove_all_proj_bounce(gedict_t *owner, gedict_t *e)
{
	if (cvar("sv_antilag") != 1)
		return;

	float ms = (atof(ezinfokey(owner, "ping")) / 1000) - 0.026;

	if (ms > ANTILAG_REWIND_MAXPROJECTILE)
		ms = ANTILAG_REWIND_MAXPROJECTILE;
	else if (ms < 0)
		ms = 0;

	// log hold stats, because we use nohold antilag moving
	antilag_t *list;
	for (list = antilag_list_players; list != NULL; list = list->next)
	{
		if (list->owner->s.v.health <= 0)
			continue;

		VectorCopy(list->owner->s.v.origin, list->held_origin);
		VectorCopy(list->owner->s.v.velocity, list->held_velocity);
	}

	for (list = antilag_list_world; list != NULL; list = list->next)
	{
		VectorCopy(list->owner->s.v.origin, list->held_origin);
		VectorCopy(list->owner->s.v.velocity, list->held_velocity);
	}

	vec3_t old_org;
	VectorCopy(owner->s.v.origin, old_org);
	antilag_lagmove_all_nohold(owner, ms, true);
	VectorSubtract(owner->s.v.origin, old_org, old_org);
	VectorAdd(e->s.v.origin, old_org, old_org);
	trap_setorigin(NUM_FOR_EDICT(e), PASSVEC3(old_org));
	VectorCopy(e->s.v.origin, e->oldangles); // store for later maybe
	e->s.v.armorvalue = ms;

	gedict_t *oself = self;
	self = e;

	float step_time = min(0.05, ms);
	float current_time = g_globalvars.time - ms;
	while (current_time < g_globalvars.time)
	{
		step_time = bound(0.01, min(step_time, (g_globalvars.time - current_time) - 0.01), 0.05);
		
		antilag_lagmove_all_nohold(owner, (g_globalvars.time - current_time), false);
		Physics_Bounce(step_time);
		if (self->s.v.nextthink) { self->s.v.nextthink -= step_time; }
		current_time += step_time;
	}

	self = oself;

	// restore origins to held values
	antilag_unmove_all();
}













