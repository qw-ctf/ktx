#include "func_rotate.h"
#include "g_local.h"

void LinkRotateTargets()
{
	gedict_t *ent;
	vec3_t v;

	VectorCopy(self->s.v.origin, self->s.v.oldorigin);

	ent = find(world, FOFS(targetname), self->target);
	while (ent)
	{
		if (streq(ent->classname, "rotate_object"))
		{
			ent->s.v.rotate_type = OBJECT_ROTATE;
			VectorSubtract(ent->s.v.origin, self->s.v.oldorigin, ent->s.v.oldorigin);
			VectorSubtract(ent->s.v.origin, self->s.v.oldorigin, ent->s.v.neworigin);
			ent->s.v.owner = EDICT_TO_PROG(self);
		}
		else if (streq(ent->classname, "func_movewall"))
		{
			ent->s.v.rotate_type = OBJECT_MOVEWALL;
			VectorAdd(ent->s.v.absmin, ent->s.v.absmax, v);
			VectorScale(v, 0.5, v);
			VectorSubtract(v, self->s.v.oldorigin, ent->s.v.oldorigin);
			VectorCopy(ent->s.v.oldorigin, ent->s.v.neworigin);
			ent->s.v.owner = EDICT_TO_PROG(self);
		}
		else
		{
			ent->s.v.rotate_type = OBJECT_SETORIGIN;
			VectorSubtract(ent->s.v.origin, self->s.v.oldorigin, ent->s.v.oldorigin);
			VectorSubtract(ent->s.v.origin, self->s.v.oldorigin, ent->s.v.neworigin);
		}
		ent = find(ent, FOFS(targetname), self->target);
	}
}

void RotateTargets()
{
	vec3_t vx, vy, vz, org;
	gedict_t *ent;

	trap_makevectors(self->s.v.angles);

	ent = find(world, FOFS(targetname), self->target);
	while (ent)
	{
		VectorCopy(ent->s.v.oldorigin, org);
		VectorScale(g_globalvars.v_forward, org[0], vx);
		VectorScale(g_globalvars.v_right, org[1], vy);
		VectorScale(vy, -1, vy);
		VectorScale(g_globalvars.v_up, org[2], vz);
		VectorAdd(vx, vy, ent->s.v.neworigin);
		VectorAdd(ent->s.v.neworigin, vz, ent->s.v.neworigin);

		if (ent->s.v.rotate_type == OBJECT_SETORIGIN)
		{
			VectorAdd(ent->s.v.neworigin, self->s.v.origin, org);
			setorigin(ent, PASSVEC3(org));
		}
		else if (ent->s.v.rotate_type == OBJECT_ROTATE)
		{
			VectorCopy(self->s.v.angles, ent->s.v.angles);
			VectorAdd(ent->s.v.neworigin, self->s.v.origin, org);
			setorigin(ent, PASSVEC3(org));
		}
		else
		{
			VectorSubtract(self->s.v.origin, self->s.v.oldorigin, org);
			VectorAdd(org, ent->s.v.neworigin, org);
			VectorSubtract(org, ent->s.v.oldorigin, ent->s.v.neworigin);

			VectorSubtract(ent->s.v.neworigin, ent->s.v.origin, org);
			VectorScale(org, 25, ent->s.v.velocity);
		}

		ent = find(ent, FOFS(targetname), self->target);
	}
}

void RotateTargetsFinal()
{
	gedict_t *ent;

	ent = find(world, FOFS(targetname), self->target);
	while (ent)
	{
		SetVector(ent->s.v.velocity, 0, 0, 0);
		if (ent->s.v.rotate_type == OBJECT_ROTATE)
		{
			VectorCopy(self->s.v.angles, ent->s.v.angles);
		}
		ent = find(ent, FOFS(targetname), self->target);
	}
}
