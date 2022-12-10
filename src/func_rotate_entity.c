#include "func_rotate.h"
#include "g_local.h"

static void rotate_entity_think()
{
	vec3_t delta;
	float t;

	t = g_globalvars.time - self->s.v.ltime;
	self->s.v.ltime = g_globalvars.time;

	VectorScale(self->s.v.rotate, t, delta);
	VectorAdd(self->s.v.angles, delta, self->s.v.angles);
	AnglesNormalize(self->s.v.angles);
	VectorCopy(self->s.v.angles, self->mangle);
	RotateTargets();
	self->s.v.nextthink = g_globalvars.time + 0.02;
}

static void rotate_entity_use()
{
	// change to alternate textures
	self->s.v.frame = 1 - self->s.v.frame;

	if (self->state == STATE_ACTIVE)
	{
		if ((int) self->s.v.spawnflags & ROTATE_ENTITY_TOGGLE)
		{
			if (self->speed)
			{
				self->count = 1;
				self->state = STATE_SLOWINGDOWN;
			}
			else
			{
				self->state = STATE_INACTIVE;
				self->think = (func_t) NULL;
			}
		}
	}
	else if (self->state == STATE_INACTIVE)
	{
		self->think = (func_t) rotate_entity_think;
		self->s.v.nextthink = g_globalvars.time + 0.02;
		self->s.v.ltime = g_globalvars.time;
		if (self->speed)
		{
			self->count = 0;
			self->state = STATE_SPEEDINGUP;
		}
		else
		{
			self->state = STATE_ACTIVE;
		}
	}
	else if (self->state == STATE_SPEEDINGUP)
	{
		if ((int) self->s.v.spawnflags & ROTATE_ENTITY_TOGGLE)
		{
			self->state = STATE_SLOWINGDOWN;
		}
	}
	else
	{
		self->state = STATE_SPEEDINGUP;
	}
}

static void rotate_entity_first_think()
{
	LinkRotateTargets();
	if ((int) (self->s.v.spawnflags) & ROTATE_ENTITY_START_ON)
	{
		self->state = STATE_ACTIVE;
		self->think = (func_t) rotate_entity_think;
		self->s.v.nextthink = g_globalvars.time + 0.02;
		self->s.v.ltime = g_globalvars.time;
	}
	else
	{
		self->state = STATE_INACTIVE;
		self->think = (func_t) NULL;
	}
	self->use = (func_t) rotate_entity_use;
}

void SP_rotate_object()
{
	VectorCopy(self->s.v.angles, self->mangle);
	SetVector(self->s.v.angles, 0, 0, 0);
	self->classname = "rotate_object";
	self->s.v.solid = SOLID_NOT;
	self->s.v.movetype = MOVETYPE_NONE;
	setsize(self, PASSVEC3(self->s.v.mins), PASSVEC3(self->s.v.maxs));
	setmodel(self, self->model);
	setorigin(self, PASSVEC3(self->s.v.origin));
}

void SP_func_rotate_entity()
{
	VectorCopy(self->s.v.angles, self->mangle);
	SetVector(self->s.v.angles, 0, 0, 0);
	self->s.v.solid = SOLID_NOT;
	self->s.v.movetype = MOVETYPE_NONE;

	setmodel(self, self->model);
	setsize(self, PASSVEC3(self->s.v.mins), PASSVEC3(self->s.v.maxs));
	setorigin(self, PASSVEC3(self->s.v.origin));

	if (self->speed != 0)
	{
		self->cnt = 1 / self->speed;
	}
	else
	{
		self->cnt = 1;
	}

	self->think = (func_t) rotate_entity_first_think;
	self->s.v.nextthink = g_globalvars.time + 0.1;
	self->s.v.ltime = g_globalvars.time;
}
