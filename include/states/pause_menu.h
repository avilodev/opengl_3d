#ifndef PAUSE_MENU_H
#define PAUSE_MENU_H

#include "input/input.h"
#include "input/actions.h"

/* Which list of buttons the panel is showing. The pages are a display state and
   nothing else: whatever page is open, the menu still answers the same three
   questions below about what the player asked for. */
typedef enum {
    PAUSE_PAGE_ROOT,
    PAUSE_PAGE_CLASS
} PausePage;

typedef struct {
    int active;
    PausePage page;
    int hovered_button;
    int quit_requested;
    /* What the player picked, as an index into the loadout table, and a flag
       raised on the frame they picked it. The menu does not arm a weapon or
       touch the player - it reports the choice and the game applies it. */
    int selected_class;
    int class_change_requested;
} PauseMenu;

void pause_menu_init(PauseMenu* menu, int selected_class);
void pause_menu_open(PauseMenu* menu);
void pause_menu_close(PauseMenu* menu);
void pause_menu_update(PauseMenu* menu, const InputState* input,
                       const ActionState* actions,
                       int viewport_width, int viewport_height);
void pause_menu_render(const PauseMenu* menu,
                       int viewport_width, int viewport_height);

#endif
