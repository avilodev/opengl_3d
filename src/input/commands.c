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
    /* The same two buttons the gun uses. A press rather than the held state,
       because a swing is a decision and a held trigger is not three of them -
       the chain advances on inputs, and auto-swinging would make the combo
       something that happens TO the player rather than something they do. */
    weapon->attack_pressed = action_pressed(actions, ACTION_FIRE);
    weapon->block_held = action_held(actions, ACTION_AIM);
    /* Heavy is attack with block held: a commitment made with both buttons,
       which is what stops it from being the one anybody spams. */
    weapon->heavy_pressed = weapon->attack_pressed && weapon->block_held;
}
