#ifndef PAUSE_MENU_H
#define PAUSE_MENU_H

#include "input/input.h"
#include "input/actions.h"

typedef struct {
    int active;
    int hovered_button;
    int quit_requested;
} PauseMenu;

void pause_menu_init(PauseMenu* menu);
void pause_menu_open(PauseMenu* menu);
void pause_menu_close(PauseMenu* menu);
void pause_menu_update(PauseMenu* menu, const InputState* input,
                       const ActionState* actions,
                       int viewport_width, int viewport_height);
void pause_menu_render(const PauseMenu* menu,
                       int viewport_width, int viewport_height);

#endif
