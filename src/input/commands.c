#include "input/commands.h"
#include "input/actions.h"

void commands_decode(const ActionState* actions,
                     PlayerCommand* player,
                     CameraCommand* camera,
                     WeaponCommand* weapon) {
    player->move_right = (float)(action_held(actions, ACTION_MOVE_RIGHT) -
                                 action_held(actions, ACTION_MOVE_LEFT));
    player->move_forward = (float)(action_held(actions, ACTION_MOVE_FORWARD) -
                                   action_held(actions, ACTION_MOVE_BACKWARD));
    player->lean_left_pressed = action_pressed(actions, ACTION_LEAN_LEFT);
    player->lean_right_pressed = action_pressed(actions, ACTION_LEAN_RIGHT);
    player->sprint_held = action_held(actions, ACTION_SPRINT);
    player->crouch_held = action_held(actions, ACTION_CROUCH);
    player->crouch_pressed = action_pressed(actions, ACTION_CROUCH);
    player->jump_pressed = action_pressed(actions, ACTION_JUMP);
    player->grapple_held = action_held(actions, ACTION_GRAPPLE);
    player->grapple_pressed = action_pressed(actions, ACTION_GRAPPLE);

    camera->look_x = actions->look_x;
    camera->look_y = actions->look_y;
    camera->toggle_view_pressed = action_pressed(actions, ACTION_TOGGLE_VIEW);
    camera->front_view_pressed = action_pressed(actions, ACTION_FRONT_VIEW);

    weapon->fire_held = action_held(actions, ACTION_FIRE);
    weapon->aim_held = action_held(actions, ACTION_AIM);
    weapon->reload_pressed = action_pressed(actions, ACTION_RELOAD);
}
