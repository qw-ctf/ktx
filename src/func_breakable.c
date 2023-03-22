#include "g_local.h"

#define BREAKABLE_NO_MONSTERS 1
#define BREAK_EXPLODE 2
#define BREAK_CUSTOM 4

void VelocityForDamage(float dm, vec3_t v);

/*
================
SUB_DislodgeRestingEntities

This clears the FL_ONGROUND flag from any entities that are on top of
self.

The engine does not update the FL_ONGROUND flag automatically in some
cases, with the result that certain types of entities can be left
floating in mid-air if the entity they are resting on is removed from
under them.  This function is intended to be called in the case where
self is going to be removed, to ensure that other entities are not left
floating.  -- iw
================
*/
static void SUB_DislodgeRestingEntities()
{
	gedict_t *e;

	for (e = nextent (world); e != NULL && e != world; e = nextent (e))
	{
		if (streq(e->classname, "player"))
		{
			continue;
		}
		if (((int) e->s.v.flags & FL_ONGROUND) && e->s.v.groundentity == EDICT_TO_PROG(self))
		{
			e->s.v.flags = (int) e->s.v.flags & ~FL_ONGROUND;
		}
	}
}


static void spawn_breakable_templates_debris(string_t template, int count)
{
	gedict_t *new, *oldself;
	int i;

	for (i = 0; i < count; i++) {
		new = spawn();
		new->model = template;
		new->s.v.origin[0] = (self->s.v.maxs[0] - self->s.v.mins[0]) * g_random() + self->s.v.mins[0];
		new->s.v.origin[1] = (self->s.v.maxs[1] - self->s.v.mins[1]) * g_random() + self->s.v.mins[1];
		new->s.v.origin[2] = (self->s.v.maxs[2] - self->s.v.mins[2]) * g_random() + self->s.v.mins[2];

		setmodel (new, new->model);
		setsize (new, 0, 0, 0, 0, 0, 0);

		oldself = self;
		self = new;
		VelocityForDamage (self->s.v.health * 2, new->s.v.velocity);
		self = oldself;

		new->s.v.movetype = MOVETYPE_BOUNCE;
		new->s.v.solid = SOLID_NOT;
		new->s.v.avelocity[0] = g_random() * 600;
		new->s.v.avelocity[1] = g_random() * 600;
		new->s.v.avelocity[2] = g_random() * 600;
		new->think = (func_t) SUB_Remove;
		new->s.v.ltime = g_globalvars.time;
		new->s.v.nextthink = g_globalvars.time + 10 + g_random() * 10;
		new->s.v.flags = 0;
	}
}

static void make_breakable_templates_debris()
{
	if (!strnull(self->brk_template1))
	{
		spawn_breakable_templates_debris(self->brk_template1, self->brk_obj_count1);
	}

	if (!strnull(self->brk_template2))
	{
		spawn_breakable_templates_debris(self->brk_template2, self->brk_obj_count2);
	}

	if (!strnull(self->brk_template3))
	{
		spawn_breakable_templates_debris(self->brk_template3, self->brk_obj_count3);
	}

	if (!strnull(self->brk_template4))
	{
		spawn_breakable_templates_debris(self->brk_template4, self->brk_obj_count4);
	}

	if (!strnull(self->brk_template5))
	{
		spawn_breakable_templates_debris(self->brk_template5, self->brk_obj_count5);
	}
}


//Below this is from Rubicon2 -- dumptruck_ds

static void make_breakable_debris()
{
	float i;
	for (i = 0; i < self->cnt; i++)
	{
		gedict_t *new, *oldself;

		new = spawn();
		new->s.v.origin[0] = (self->s.v.maxs[0] - self->s.v.mins[0])*g_random() + self->s.v.mins[0];
		new->s.v.origin[1] = (self->s.v.maxs[1] - self->s.v.mins[1])*g_random() + self->s.v.mins[1];
		new->s.v.origin[2] = (self->s.v.maxs[2] - self->s.v.mins[2])*g_random() + self->s.v.mins[2];

		// setmodel (new, "progs/debris.mdl"); this was originally just an mdl from Rubicon2, now you set custom model via spawnflag or use the builtin from Rubicon2 -- dumptruck_ds
		setmodel (new, self->brk_mdl_debris); //dumptruck_ds
		setsize (new, 0, 0, 0, 0, 0, 0);

		oldself = self;
		self = new;
		VelocityForDamage (self->s.v.health * 2, new->s.v.velocity);
		self = oldself;

		new->s.v.movetype = MOVETYPE_BOUNCE;
		new->s.v.solid = SOLID_NOT;
		new->s.v.avelocity[0] = g_random()*600;
		new->s.v.avelocity[1] = g_random()*600;
		new->s.v.avelocity[2] = g_random()*600;
		new->think = (func_t) SUB_Remove;
		new->s.v.ltime = g_globalvars.time;
		new->s.v.nextthink = g_globalvars.time + 10 + g_random() * 10;
		new->s.v.flags = 0;

		// randomly choose size
		if (g_random() > 0.333)
		{
			new->s.v.frame = 1; //larger
		}
		else
		{
			new->s.v.frame = 2; //smaller
		}

		// choose skin based on "style" key
		self->s.v.skin = self->style;
	}
}

void func_breakable_die()
{
	//dumptruck_ds -- set the spawnflag for cosmetic explosion effect; use "dmg" value to hurt the player
	{
		gedict_t *ent; //thanks to Qmaster!!! He helped me sort out noise1 playing from 0 0 0 with this temp entity - dumptruck_ds
		vec3_t r;

		ent = spawn();
		VectorAdd(self->s.v.absmin, self->s.v.absmax, r);
		VectorScale(r, 0.5, ent->s.v.origin);
		setsize (ent, 0, 0, 0, 0, 0, 0);
		ent->s.v.solid = SOLID_NOT;
		ent->think = (func_t) SUB_Remove;
		ent->s.v.nextthink = g_globalvars.time + 60;

		sound(ent, CHAN_AUTO, self->noise1, 1, ATTN_NORM);
	}

	// this is to ensure that any debris from another func_breakable
	// which is resting on top of this func_breakable is not left
	// floating in mid-air after this entity is removed -- iw
	SUB_DislodgeRestingEntities();

	if ((int) self->s.v.spawnflags & BREAK_EXPLODE) {
		if ((int) self->s.v.spawnflags & BREAK_CUSTOM) {
			make_breakable_templates_debris();
		} else {
			make_breakable_debris();
		}
		sound(self, CHAN_VOICE, self->noise2, 1, ATTN_NORM);
		ent_remove (self);
	} else {
		vec3_t r;

		VectorAdd(self->s.v.absmin, self->s.v.absmax, r);
		VectorScale(r, 0.5, self->s.v.origin);
		setorigin (self, PASSVEC3(self->s.v.origin));

		if ((int) self->s.v.spawnflags & BREAK_CUSTOM) {
			make_breakable_templates_debris ();
			ent_remove (self);
		} else {
			make_breakable_debris ();
			ent_remove (self);
		}
	}
}

static void func_breakable_killed()
{
	activator = damage_attacker;
	SUB_UseTargets ();
	func_breakable_die ();
}

static void func_breakable_use()
{
	activator = other;
	SUB_UseTargets ();
	func_breakable_die ();
}

/*QUAKED func_breakable (0 .5 .8) ? NO_MONSTERS X X X X X X X NOT_ON_EASY NOT_ON_NORMAL NOT_ON_HARD_OR_NIGHTMARE NOT_IN_DEATHMATCH NOT_IN_COOP NOT_IN_SINGLEPLAYER X NOT_ON_HARD_ONLY NOT_ON_NIGHTMARE_ONLY

"Breakable - See manual for full details

Defaults to built-in .mdl file with 32 styles, cnt is number of pieces of debris to spawn (built-in only)
Or use spawnflag 4 and break_template1-4 to set path of custom .mdl or .bsp models.
brk_object_count1-4 sets the number of pieces of each break_template when using custom .mdl or bsp models.
If noise1 is not set it will default to various sounds in sounds/break folder
Use spawnflag 2 for an explosion, dmg is amount of damage inflicted"

spawnflags(flags)
1 : "No Monster Damage" : 0 : "Only the player can break"
2 : "Explosion" : 0 : "Produces explosion effect and sound"
4 : "Use custom mdls or bsp models" : 0 : "Uses models specified in break_template1, 2, etc"

style(choices) : "Built-in debris style" : 0
0 : "Green Metal (default)"
1 : "Red Metal"
2 : "Concrete"
3 : "Pine wood"
4 : "Brown wood"
5 : "Red wood"
6 : "Stained Glass Yellow Flames"
7 : "Stained Glass Red Rays"
8 : "Stained Glass Yellow Dragon"
9 : "Stained Glass Blue Dragon"
10 : "Stained Glass Red Dragon"
11 : "Light Copper"
12 : "Dark Copper"
13 : "Tan Bricks Large"
14 : "Brown Bricks Large"
15 : "Green Bricks Large"
16 : "Generic Light Brown"
17 : "Red Brown Computer"
18 : "Grey Black Computer"
19 : "Blue Green Metal"
20 : "Blue Green Runic Wall"
21 : "Brown Metal"
22 : "Dark Brown Metal"
23 : "Medium Brown Metal"
24 : "Blue Metal"
25 : "Green Stonework"
26 : "Blue Stonework"
27 : "Brown Bricks"
28 : "Tan Blue Bricks"
29 : "Red Bricks"
30 : "Blue Bricks"
31 : "Metal Rivets"

noise1(string) : "Break noise (overrides default sounds)"
cnt(integer) : "Number of pieces of debris to spawn" : 5
health(integer) : "Health of breakable" : 20
dmg(integer) : "Amount of Explosive Damage" : 20
break_template1(string) : "Template 1 model path, e.g. maps/break/brk.bsp or progs/brick.mdl"
break_template2(string) : "Template 2 model path, e.g. maps/break/brk.bsp or progs/brick.mdl"
break_template3(string) : "Template 3 model path, e.g. maps/break/brk.bsp or progs/brick.mdl"
break_template4(string) : "Template 4 model path, e.g. maps/break/brk.bsp or progs/brick.mdl"
break_template5(string) : "Template 5 model path, e.g. maps/break/brk.bsp or progs/brick.mdl"
brk_obj_count1(integer) : "Template 1 spawn count"
brk_obj_count2(integer) : "Template 2 spawn count"
brk_obj_count3(integer) : "Template 3 spawn count"
brk_obj_count4(integer) : "Template 4 spawn count"
brk_obj_count5(integer) : "Template 5 spawn count"
*/

static void break_template_setup()
{
	if (!strnull(self->brk_template1))
	{
		trap_precache_model(self->brk_template1);
	}
	if (!strnull(self->brk_template2))
	{
		trap_precache_model(self->brk_template2);
	}
	if (!strnull(self->brk_template3))
	{
		trap_precache_model(self->brk_template3);
	}
	if (!strnull(self->brk_template4))
	{
		trap_precache_model(self->brk_template4);
	}
	if (!strnull(self->brk_template5))
	{
		trap_precache_model(self->brk_template5);
	}
}

static string_t breakable_default_sound(int style)
{
	switch (self->style)
	{
	case 0:
	case 11:
	case 12:
	case 17:
	case 18:
	case 19:
	case 24:
	case 31:
		return "break/metal2.wav";
	case 3:
	case 4:
	case 5:
		return (g_random() > 0.6) ? "break/wood1.wav" : "break/wood2.wav";
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
		return "break/metal1.wav";
	default:
		return (g_random() > 0.6) ? "break/bricks1.wav" : "break/stones1.wav";
	}
}

void SP_func_breakable()
{
	break_template_setup();

	self->classname = "breakable";
	self->s.v.solid = SOLID_BSP;
	self->s.v.movetype = MOVETYPE_PUSH;

	setorigin(self, PASSVEC3(self->s.v.origin));
	setmodel (self, self->model);
	setsize(self, PASSVEC3(self->s.v.mins), PASSVEC3(self->s.v.maxs));

	self->brk_mdl_debris = "progs/debris.mdl";
	trap_precache_model (self->brk_mdl_debris);
	trap_precache_sound ("blob/hit1.wav");

	if (strnull(self->noise1))
	{
		self->noise1 = breakable_default_sound(self->style);
	}

	trap_precache_sound(self->noise1);

	if (self->s.v.health <= 0)
	{
		self->s.v.health = 20;
	}

	if (self->cnt <= 0)
	{
		self->cnt = 5;
	}

	if (!strnull(self->targetname))
	{
		self->use = (func_t) func_breakable_use;
	}
	else
	{
		self->s.v.takedamage = DAMAGE_YES;
		self->th_die = func_breakable_killed;
	}
}
