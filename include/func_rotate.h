#ifndef FUNC_ROTATE_H
#define FUNC_ROTATE_H

#define STATE_ACTIVE      0
#define STATE_INACTIVE    1
#define STATE_SPEEDINGUP  2
#define STATE_SLOWINGDOWN 3

#define STATE_CLOSED  4
#define STATE_OPEN    5
#define STATE_OPENING 6
#define STATE_CLOSING 7

#define STATE_WAIT 0
#define STATE_MOVE 1
#define STATE_STOP 2
#define STATE_FIND 3
#define STATE_NEXT 4

#define OBJECT_ROTATE    0
#define OBJECT_MOVEWALL  1
#define OBJECT_SETORIGIN 2

// spawnflags for func_rotate_entity
#define ROTATE_ENTITY_TOGGLE   1
#define ROTATE_ENTITY_START_ON 2

// spawnflags for path_rotate
#define PATH_ROTATE_ROTATION   1
#define PATH_ROTATE_ANGLES     2
#define PATH_ROTATE_STOP       4
#define PATH_ROTATE_NO_ROTATE  8
#define PATH_ROTATE_DAMAGE     16
#define PATH_ROTATE_MOVETIME   32
#define PATH_ROTATE_SET_DAMAGE 64

// spawnflags for func_rotate_door
#define ROTATE_DOOR_STAYOPEN 1

// spawnflags for func_movewall
#define MOVEWALL_VISIBLE     1
#define MOVEWALL_TOUCH       2
#define MOVEWALL_NONBLOCKING 4

void LinkRotateTargets();
void RotateTargets();
void RotateTargetsFinal();

#endif
