#include "func_rotate.h"
#include "g_local.h"

static void rotate_door_think();

static void rotate_door_reversedirection()
{
	vec3_t start;

	// change to alternate textures
	self->s.v.frame = 1 - self->s.v.frame;

	if (self->state == STATE_CLOSING)
	{
		VectorCopy(self->dest1, start);
		VectorCopy(self->dest2, self->dest);
		self->state = STATE_OPENING;
	}
	else
	{
		VectorCopy(self->dest2, start);
		VectorCopy(self->dest1, self->dest);
		self->state = STATE_CLOSING;
	}

	sound(self, CHAN_VOICE, self->noise2, 1, ATTN_NORM);

	VectorSubtract(self->dest, start, self->s.v.rotate);
	VectorScale(self->s.v.rotate, 1 / self->speed, self->s.v.rotate);
	self->think = (func_t) rotate_door_think;
	self->s.v.nextthink = g_globalvars.time + 0.02;
	self->s.v.endtime = g_globalvars.time + self->speed - (self->s.v.endtime - g_globalvars.time);
	self->s.v.ltime = g_globalvars.time;
}

static void rotate_door_group_reversedirection()
{
	string_t name;

	// tell all associated rotaters to reverse direction
	if (!strnull(self->s.v.group))
	{
		name = self->s.v.group;
		self = find(world, FOFS(s.v.group), name);
		while (self)
		{
			rotate_door_reversedirection();
			self = find(self, FOFS(s.v.group), name);
		}
	}
	else
	{
		rotate_door_reversedirection();
	}
}

static void rotate_door_think2()
{
	self->s.v.ltime = g_globalvars.time;

	// change to alternate textures
	self->s.v.frame = 1 - self->s.v.frame;

	VectorCopy(self->dest, self->s.v.angles);

	if (self->state == STATE_OPENING)
	{
		self->state = STATE_OPEN;
	}
	else
	{
		if ((int) self->s.v.spawnflags & ROTATE_DOOR_STAYOPEN)
		{
			rotate_door_group_reversedirection();
			return;
		}
		self->state = STATE_CLOSED;
	}

	sound(self, CHAN_VOICE, self->noise3, 1, ATTN_NORM);
	self->think = (func_t) NULL;
	RotateTargetsFinal();
}

static void rotate_door_think()
{
	float t;
	vec3_t v;

	t = g_globalvars.time - self->s.v.ltime;
	self->s.v.ltime = g_globalvars.time;

	if (self->s.v.ltime < self->s.v.endtime)
	{
		VectorScale(self->s.v.rotate, t, v);
		VectorAdd(self->s.v.angles, v, self->s.v.angles);
		RotateTargets();
	}
	else
	{
		VectorCopy(self->dest, self->s.v.angles);
		RotateTargets();
		self->think = (func_t) rotate_door_think2;
	}

	self->s.v.nextthink = g_globalvars.time + 0.01;
}

static void movewall_touch()
{
	gedict_t *owner = PROG_TO_EDICT(self->s.v.owner);

	if (g_globalvars.time < owner->attack_finished)
		return;

	if (self->dmg)
	{
		T_Damage(other, self, owner, self->dmg);
		owner->attack_finished = g_globalvars.time + 0.5;
	}
	else if (owner->dmg)
	{
		T_Damage(other, self, owner, owner->dmg);
		owner->attack_finished = g_globalvars.time + 0.5;
	}
}

static void movewall_blocked()
{
	gedict_t *temp;
	gedict_t *owner = PROG_TO_EDICT(self->s.v.owner);

	if (g_globalvars.time < owner->attack_finished)
		return;

	owner->attack_finished = g_globalvars.time + 0.5;

	if (streq(owner->classname, "func_rotate_door"))
	{
		temp = self;
		self = owner;
		rotate_door_group_reversedirection();
		self = temp;
	}

	if (self->dmg)
	{
		T_Damage(other, self, owner, self->dmg);
		owner->attack_finished = g_globalvars.time + 0.5;
	}
	else if (owner->dmg)
	{
		T_Damage(other, self, owner, owner->dmg);
		owner->attack_finished = g_globalvars.time + 0.5;
	}
}

static void movewall_think()
{
	self->s.v.ltime = g_globalvars.time;
	self->s.v.nextthink = g_globalvars.time + 0.02;
}

void SP_func_movewall()
{
	SetVector(self->s.v.angles, 0, 0, 0);
	self->s.v.movetype = MOVETYPE_PUSH;

	if ((int) self->s.v.spawnflags & MOVEWALL_NONBLOCKING)
	{
		self->s.v.solid = SOLID_NOT;
	}
	else
	{
		self->s.v.solid = SOLID_BSP;
		self->blocked = (func_t) movewall_blocked;
	}
	if ((int) self->s.v.spawnflags & MOVEWALL_TOUCH)
	{
		self->touch = (func_t) movewall_touch;
	}
	setmodel(self, self->model);
	if (!((int) self->s.v.spawnflags & MOVEWALL_VISIBLE))
	{
		self->model = NULL;
	}
	self->think = (func_t) movewall_think;
	self->s.v.nextthink = g_globalvars.time + 0.02;
	self->s.v.ltime = g_globalvars.time;
}

static void rotate_door_use()
{
	vec3_t start;

	if ((self->state != STATE_OPEN) && (self->state != STATE_CLOSED))
		return;

	if (!self->cnt)
	{
		self->cnt = 1;
		LinkRotateTargets();
	}

	self->s.v.frame = 1 - self->s.v.frame;

	if (self->state == STATE_CLOSED)
	{
		VectorCopy(self->dest1, start);
		VectorCopy(self->dest2, self->dest);
		self->state = STATE_OPENING;
	}
	else
	{
		VectorCopy(self->dest2, start);
		VectorCopy(self->dest1, self->dest);
		self->state = STATE_CLOSING;
	}

	sound(self, CHAN_VOICE, self->noise2, 1, ATTN_NORM);

	VectorSubtract(self->dest, start, self->s.v.rotate);
	VectorScale(self->s.v.rotate, 1 / self->speed, self->s.v.rotate);
	self->think = (func_t) rotate_door_think;
	self->s.v.nextthink = g_globalvars.time + 0.01;
	self->s.v.endtime = g_globalvars.time + self->speed;
	self->s.v.ltime = g_globalvars.time;
}

void SP_func_rotate_door()
{
	if (strnull(self->target))
	{
		G_Error("func_rotate_door without a target");
		return;
	}

	SetVector(self->dest1, 0, 0, 0);
	VectorCopy(self->s.v.angles, self->dest2);
	VectorCopy(self->dest1, self->s.v.angles);

	if (!self->speed)
	{
		self->speed = 2;
	}

	self->cnt = 2;

	if (!self->dmg)
	{
		self->dmg = 2;
	}
	else if (self->dmg < 0)
	{
		self->dmg = 0;
	}

	if (!self->s.v.sounds)
	{
		if (strnull(self->noise1) && strnull(self->noise2) && strnull(self->noise3))
		{
			self->s.v.sounds = 1;
		}
	}

	switch ((int) self->s.v.sounds)
	{
		case 1:
			self->noise1 = "doors/latch2.wav";
			self->noise2 = "doors/winch2.wav";
			self->noise3 = "doors/drclos4.wav";
			break;
		case 2:
			self->noise2 = "doors/airdoor1.wav";
			self->noise1 = "doors/airdoor2.wav";
			self->noise3 = self->noise1;
			break;
		case 3:
			self->noise2 = "doors/basesec1.wav";
			self->noise1 = "doors/basesec2.wav";
			self->noise3 = self->noise1;
			break;
		default:
			self->noise1 = self->noise2 = self->noise3 = "misc/null.wav";
			break;
	}

	trap_precache_sound(self->noise1);
	trap_precache_sound(self->noise2);
	trap_precache_sound(self->noise3);

	self->s.v.solid = SOLID_NOT;
	self->s.v.movetype = MOVETYPE_NONE;
	setmodel(self, self->model);
	setsize(self, PASSVEC3(self->s.v.mins), PASSVEC3(self->s.v.maxs));
	setorigin(self, PASSVEC3(self->s.v.origin));

	self->state = STATE_CLOSED;
	self->use = (func_t) rotate_door_use;
	self->think = (func_t) NULL;
}
