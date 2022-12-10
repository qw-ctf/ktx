#include "func_rotate.h"
#include "g_local.h"

static void rotate_train_next();
static void rotate_train_find();

static void rotate_train_think()
{
	float t, timeelapsed;
	vec3_t delta;

	t = g_globalvars.time - self->s.v.ltime;
	self->s.v.ltime = g_globalvars.time;

	if (self->s.v.endtime && (g_globalvars.time >= self->s.v.endtime))
	{
		self->s.v.endtime = 0;
		if (self->state == STATE_MOVE)
		{
			setorigin(self, PASSVEC3(self->s.v.finaldest));
			SetVector(self->s.v.velocity, 0, 0, 0);
		}

		if (self->think1)
			self->think1();
	}
	else
	{
		timeelapsed = (g_globalvars.time - self->cnt) * self->s.v.duration;
		if (timeelapsed > 1)
			timeelapsed = 1;
		VectorScale(self->dest2, timeelapsed, delta);
		VectorAdd(self->dest1, delta, delta);
		setorigin(self, PASSVEC3(delta));
	}

	VectorScale(self->s.v.rotate, t, delta);
	VectorAdd(self->s.v.angles, delta, self->s.v.angles);
	AnglesNormalize(self->s.v.angles);
	RotateTargets();

	self->s.v.nextthink = g_globalvars.time + 0.02;
};

static void rotate_train_use()
{
	if (self->think1 != rotate_train_find)
	{
		if (VectorLength(self->s.v.velocity))
			return; // already activated
		if (self->think1)
		{
			self->think1();
		}
	}
}

static void rotate_train_wait()
{
	gedict_t *goalentity = PROG_TO_EDICT(self->s.v.goalentity);
	self->state = STATE_WAIT;

	if (!strnull(goalentity->noise))
	{
		sound(self, CHAN_VOICE, goalentity->noise, 1, ATTN_NORM);
	}
	else
	{
		sound(self, CHAN_VOICE, self->noise, 1, ATTN_NORM);
	}

	if ((int) goalentity->s.v.spawnflags & PATH_ROTATE_ANGLES)
	{
		SetVector(self->s.v.rotate, 0, 0, 0);
		VectorCopy(self->s.v.finalangle, self->s.v.angles);
	}
	if ((int) goalentity->s.v.spawnflags & PATH_ROTATE_NO_ROTATE)
	{
		SetVector(self->s.v.rotate, 0, 0, 0);
	}
	self->s.v.endtime = self->s.v.ltime + goalentity->wait;
	self->think1 = rotate_train_next;
}

static void rotate_train_stop()
{
	gedict_t *goalentity = PROG_TO_EDICT(self->s.v.goalentity);

	self->state = STATE_STOP;

	if (!strnull(goalentity->noise))
	{
		sound(self, CHAN_VOICE, goalentity->noise, 1, ATTN_NORM);
	}
	else
	{
		sound(self, CHAN_VOICE, self->noise, 1, ATTN_NORM);
	}
	if ((int) goalentity->s.v.spawnflags & PATH_ROTATE_ANGLES)
	{
		SetVector(self->s.v.rotate, 0, 0, 0);
		VectorCopy(self->s.v.finalangle, self->s.v.angles);
	}
	if ((int) goalentity->s.v.spawnflags & PATH_ROTATE_NO_ROTATE)
	{
		SetVector(self->s.v.rotate, 0, 0, 0);
	}

	self->dmg = 0;
	self->think1 = rotate_train_next;
};

static void rotate_train_set_damage_on_targets(float amount)
{
	gedict_t *ent;

	ent = find(world, FOFS(targetname), self->target);
	while (ent)
	{
		if (streq(ent->classname, "trigger_hurt"))
		{
			ent->dmg = amount;
			ent->s.v.solid = amount ? SOLID_TRIGGER : SOLID_NOT;
			ent->s.v.nextthink = -1;
		}
		else if (streq(ent->classname, "func_movewall"))
		{
			ent->dmg = amount;
		}
		ent = find(ent, FOFS(targetname), self->target);
	}
}

static void rotate_train_set_target_origin()
{
	gedict_t *ent;
	vec3_t org;

	ent = find(world, FOFS(targetname), self->target);
	while (ent)
	{
		if (ent->s.v.rotate_type == OBJECT_MOVEWALL)
		{
			VectorSubtract(self->s.v.origin, self->s.v.oldorigin, org);
			VectorAdd(org, ent->s.v.neworigin, org);
			VectorSubtract(self->s.v.origin, self->s.v.oldorigin, org);
			setorigin(ent, PASSVEC3(org));
		}
		else
		{
			VectorAdd(ent->s.v.neworigin, self->s.v.origin, org);
			setorigin(ent, PASSVEC3(org));
		}
		ent = find(ent, FOFS(targetname), self->target);
	}
}

static void rotate_train_next()
{
	gedict_t *targ;
	gedict_t *current, *goalentity;
	vec3_t vdestdelta;
	float len, traveltime, div;
	string_t temp;

	self->state = STATE_NEXT;

	goalentity = PROG_TO_EDICT(self->s.v.goalentity);

	current = goalentity;
	targ = find(world, FOFS(targetname), self->s.v.path);
	if (!streq(targ->classname, "path_rotate"))
		G_Error("Next target is not path_rotate");

	if (!strnull(goalentity->noise1))
	{
		self->noise1 = goalentity->noise1;
	}
	sound(self, CHAN_VOICE, self->noise1, 1, ATTN_NORM);

	self->s.v.goalentity = EDICT_TO_PROG(targ);
	self->s.v.path = targ->target;
	if (strnull(self->s.v.path))
		G_Error("rotate_train_next: no next target");

	if ((int) targ->s.v.spawnflags & PATH_ROTATE_STOP)
	{
		self->think1 = rotate_train_stop;
	}
	else if (targ->wait)
	{
		self->think1 = rotate_train_wait;
	}
	else
	{
		self->think1 = rotate_train_next;
	}

	if (!strnull(current->s.v.event))
	{
		// Trigger any events that should happen at the corner.
		temp = self->target;
		self->target = current->s.v.event;
		self->message = current->message;
		SUB_UseTargets();
		self->target = temp;
		self->message = NULL;
	}

	if ((int) current->s.v.spawnflags & PATH_ROTATE_ANGLES)
	{
		SetVector(self->s.v.rotate, 0, 0, 0);
		VectorCopy(self->s.v.finalangle, self->s.v.angles);
	}

	if ((int) current->s.v.spawnflags & PATH_ROTATE_ROTATION)
	{
		VectorCopy(current->s.v.rotate, self->s.v.rotate);
	}

	if ((int) current->s.v.spawnflags & PATH_ROTATE_DAMAGE)
	{
		self->dmg = current->dmg;
	}

	if ((int) current->s.v.spawnflags & PATH_ROTATE_SET_DAMAGE)
	{
		rotate_train_set_damage_on_targets(current->dmg);
	}

	if (current->speed == -1)
	{
		// Warp to the next path_corner
		setorigin(self, PASSVEC3(targ->s.v.origin));
		self->s.v.endtime = self->s.v.ltime + 0.01;
		rotate_train_set_target_origin();

		if ((int) targ->s.v.spawnflags & PATH_ROTATE_ANGLES)
		{
			VectorCopy(targ->s.v.angles, self->s.v.angles);
		}

		self->s.v.duration = 1;        // 1 / duration
		self->cnt = g_globalvars.time; // start time
		SetVector(self->dest2, 0, 0, 0);
		VectorCopy(self->s.v.origin, self->dest1); // original position
		VectorCopy(self->s.v.origin, self->s.v.finaldest);
	}
	else
	{
		self->state = STATE_MOVE;

		VectorCopy(targ->s.v.origin, self->s.v.finaldest);
		if (VectorCompare(self->s.v.finaldest, self->s.v.origin))
		{
			SetVector(self->s.v.velocity, 0, 0, 0);
			self->s.v.endtime = self->s.v.ltime + 0.1;

			self->s.v.duration = 1;                    // 1 / duration
			self->cnt = g_globalvars.time;             // start time
			SetVector(self->dest2, 0, 0, 0);           // delta
			VectorCopy(self->s.v.origin, self->dest1); // original position
			VectorCopy(self->s.v.origin, self->s.v.finaldest);
			return;
		}
		// set destdelta to the vector needed to move
		VectorSubtract(self->s.v.finaldest, self->s.v.origin, vdestdelta);

		// calculate length of vector
		len = vlen(vdestdelta);

		if ((int) current->s.v.spawnflags & PATH_ROTATE_MOVETIME)
		{
			traveltime = current->speed;
		}
		else
		{
			// check if there's a speed change
			if (current->speed > 0)
				self->speed = current->speed;

			if (!self->speed)
				G_Error("No speed is defined!");

			// divide by speed to get time to reach dest
			traveltime = len / self->speed;
		}

		if (traveltime < 0.1)
		{
			SetVector(self->s.v.velocity, 0, 0, 0);
			self->s.v.endtime = self->s.v.ltime + 0.1;
			if ((int) targ->s.v.spawnflags & PATH_ROTATE_ANGLES)
			{
				VectorCopy(targ->s.v.angles, self->s.v.angles);
			}
			return;
		}

		// qcc won't take vec/float
		div = 1 / traveltime;

		if ((int) targ->s.v.spawnflags & PATH_ROTATE_ANGLES)
		{
			VectorCopy(targ->s.v.angles, self->s.v.finalangle);
			AnglesNormalize(self->s.v.angles);
			VectorSubtract(targ->s.v.angles, self->s.v.angles, self->s.v.rotate);
			VectorScale(self->s.v.rotate, div, self->s.v.rotate);
		}

		// set endtime to trigger a think when dest is reached
		self->s.v.endtime = self->s.v.ltime + traveltime;

		// scale the destdelta vector by the time spent traveling to get velocity
		VectorScale(vdestdelta, div, self->s.v.velocity);

		self->s.v.duration = div;                  // 1 / duration
		self->cnt = g_globalvars.time;             // start time
		VectorCopy(vdestdelta, self->dest2);       // delta
		VectorCopy(self->s.v.origin, self->dest1); // original position
	}
}

static void rotate_train_find()
{
	gedict_t *targ;

	self->state = STATE_FIND;

	LinkRotateTargets();

	// the first target is the point of rotation.
	// the second target is the path.
	targ = find(world, FOFS(targetname), self->s.v.path);

	if (!streq(targ->classname, "path_rotate"))
		G_Error("Next target is not path_rotate");

	// Save the current entity
	self->s.v.goalentity = EDICT_TO_PROG(targ);

	if ((int) targ->s.v.spawnflags & PATH_ROTATE_ANGLES)
	{
		VectorCopy(targ->s.v.angles, self->s.v.angles);
		AnglesNormalize(targ->s.v.angles);
		VectorCopy(targ->s.v.angles, self->s.v.finalangle);
	}

	self->s.v.path = targ->target;
	setorigin(self, PASSVEC3(targ->s.v.origin));
	rotate_train_set_target_origin();
	RotateTargetsFinal();
	self->think1 = rotate_train_next;
	if (!self->targetname)
	{
		// not triggered, so start immediately
		self->s.v.endtime = self->s.v.ltime + 0.1;
	}
	else
	{
		self->s.v.endtime = 0;
	}

	self->s.v.duration = 1;                    // 1 / duration
	self->cnt = g_globalvars.time;             // start time
	SetVector(self->dest2, 0, 0, 0);           // delta
	VectorCopy(self->s.v.origin, self->dest1); // original position
}

void SP_path_rotate()
{
	if (!strnull(self->noise))
	{
		trap_precache_sound(self->noise);
	}
	if (!strnull(self->noise1))
	{
		trap_precache_sound(self->noise1);
	}
}

void SP_func_rotate_train()
{
	if (strnull(self->target))
	{
		G_Error("func_rotate_train without a target");
		return;
	}

	if (!self->speed)
	{
		self->speed = 100;
	}

	if (strnull(self->noise))
	{
		if (self->s.v.sounds == 0)
		{
			self->noise = "misc/null.wav";
		}

		if (self->s.v.sounds == 1)
		{
			self->noise = "plats/train2.wav";
		}
	}

	if (strnull(self->noise1))
	{
		if (self->s.v.sounds == 0)
		{
			self->noise1 = ("misc/null.wav");
		}
		if (self->s.v.sounds == 1)
		{
			self->noise1 = ("plats/train1.wav");
		}
	}

	trap_precache_sound(self->noise);
	trap_precache_sound(self->noise1);

	self->cnt = 1;
	self->s.v.solid = SOLID_NOT;
	self->s.v.movetype = MOVETYPE_STEP;
	self->use = (func_t) rotate_train_use;

	setmodel(self, self->model);
	setsize(self, PASSVEC3(self->s.v.mins), PASSVEC3(self->s.v.maxs));
	setorigin(self, PASSVEC3(self->s.v.origin));

	// start trains on the second frame, to make sure their targets have had
	// a chance to spawn
	self->s.v.ltime = g_globalvars.time;
	self->s.v.nextthink = self->s.v.ltime + 0.1;
	self->s.v.endtime = self->s.v.ltime + 0.1;
	self->think = (func_t) rotate_train_think;
	self->think1 = rotate_train_find;
	self->state = STATE_FIND;

	self->s.v.duration = 1; // 1 / duration
	self->cnt = 0.1;        // start time
	SetVector(self->dest2, 0, 0, 0);
	VectorCopy(self->s.v.origin, self->dest1); // original position

	self->s.v.flags = (int) self->s.v.flags | FL_ONGROUND;
}
