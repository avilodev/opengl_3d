#ifndef INPUT_COMMANDS_H
#define INPUT_COMMANDS_H

typedef struct ActionState ActionState;

/* Focused simulation inputs. Gameplay receives only the decisions it owns,
   never the full action table or the physical input that produced it. */
typedef struct {
    float move_right;
    float move_forward;
    /* Edges, not held state: lean is a toggle, so what the player controller
       needs is the moment a key went down and not whether it is still down. */
    int lean_left_pressed;
    int lean_right_pressed;
    int sprint_held;
    int crouch_held;
    int crouch_pressed;
    int jump_pressed;
    int grapple_held;
    int grapple_pressed;
} PlayerCommand;

typedef struct {
    float look_x;
    float look_y;
    /* Cycles first person, third behind, third in front. */
    int toggle_view_pressed;
    /* And the shortcut straight to the last of those. */
    int front_view_pressed;
} CameraCommand;

typedef struct {
    int fire_held;
    int aim_held;
    int reload_pressed;
} WeaponCommand;

void commands_decode(const ActionState* actions,
                     PlayerCommand* player,
                     CameraCommand* camera,
                     WeaponCommand* weapon);

#endif
