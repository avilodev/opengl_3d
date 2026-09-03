#ifndef INPUT_ACTIONS_H
#define INPUT_ACTIONS_H

#include "input/input.h"

/* Semantic controls consumed by simulation. Physical keys and buttons are
   deliberately confined to actions.c. */
typedef enum {
    ACTION_MOVE_FORWARD,
    ACTION_MOVE_BACKWARD,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_SPRINT,
    ACTION_CROUCH,
    ACTION_JUMP,
    ACTION_GRAPPLE,
    ACTION_LEAN_LEFT,
    ACTION_LEAN_RIGHT,
    ACTION_FIRE,
    ACTION_AIM,
    ACTION_RELOAD,
    ACTION_TOGGLE_VIEW,
    ACTION_FRONT_VIEW,
    ACTION_PAUSE,
    ACTION_RESUME,
    /* Pays a flat figure into the player's own pool. A development control and
       labelled one: nothing in the world can hurt the player yet - the bodies
       out there have no AI and a muzzle inside its own owner's cylinder does
       not resolve against it - so without this the death loop is machinery
       nobody can feel. It goes when the session layer arrives with something
       that shoots back. */
    ACTION_DEBUG_DAMAGE,
    ACTION_COUNT
} Action;

typedef struct ActionState {
    unsigned char held[ACTION_COUNT];
    unsigned char pressed[ACTION_COUNT];
    unsigned char released[ACTION_COUNT];
    float look_x;
    float look_y;
} ActionState;

/* Load keybinds from key=value text. Missing files and entries use defaults. */
void actions_load_bindings(const char* path);
void actions_decode(ActionState* actions, const InputState* input);
int action_held(const ActionState* actions, Action action);
int action_pressed(const ActionState* actions, Action action);
int action_released(const ActionState* actions, Action action);

#endif
