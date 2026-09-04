#include "states/pause_menu.h"

#include "gameplay/player_loadout.h"
#include "render/renderer.h"

enum {
    PAUSE_BUTTON_RESUME,
    PAUSE_BUTTON_SWITCH_CLASS,
    PAUSE_BUTTON_QUIT,
    PAUSE_ROOT_BUTTON_COUNT
};

/* Authored at 720p and scaled, so the panel keeps its proportions on a native
   resolution display instead of becoming a postage stamp in the middle of it. */
#define PAUSE_PANEL_WIDTH 340.0f
#define PAUSE_HEADER_HEIGHT 64.0f
#define PAUSE_BUTTON_WIDTH 240.0f
#define PAUSE_BUTTON_HEIGHT 52.0f
#define PAUSE_BUTTON_FIRST_Y 92.0f
#define PAUSE_BUTTON_PITCH 68.0f
#define PAUSE_PANEL_PAD_BOTTOM 18.0f

/* The class page is as tall as the class list is long, so the panel is measured
   from its buttons rather than stated as a number. A sixth class costs nothing
   here; a fixed height would have cost a layout edit per class. */
static float panel_height(int button_count) {
    return PAUSE_BUTTON_FIRST_Y + (float)(button_count - 1) * PAUSE_BUTTON_PITCH +
           PAUSE_BUTTON_HEIGHT + PAUSE_PANEL_PAD_BOTTOM;
}

/* The class page lists every class and then a way back out of it, which is why
   the last button on it is one past the last class. */
static int page_button_count(const PauseMenu* menu) {
    if (menu->page == PAUSE_PAGE_CLASS) return player_loadout_count() + 1;
    return PAUSE_ROOT_BUTTON_COUNT;
}

static const char* page_title(const PauseMenu* menu) {
    return menu->page == PAUSE_PAGE_CLASS ? "Choose Class" : "Paused";
}

static const char* button_label(const PauseMenu* menu, int index) {
    static const char* root_labels[PAUSE_ROOT_BUTTON_COUNT] = {
        "Resume", "Switch Class", "Exit Game"
    };
    if (menu->page != PAUSE_PAGE_CLASS) return root_labels[index];
    if (index >= player_loadout_count()) return "Back";
    return player_loadout_get(index)->name;
}

static void button_bounds(const PauseMenu* menu,
                          int viewport_width, int viewport_height, int index,
                          float* x, float* y, float* width, float* height) {
    float scale = renderer_ui_scale(viewport_height);
    *width = PAUSE_BUTTON_WIDTH * scale;
    *height = PAUSE_BUTTON_HEIGHT * scale;
    *x = ((float)viewport_width - *width) * 0.5f;
    *y = ((float)viewport_height - panel_height(page_button_count(menu)) * scale) * 0.5f +
         (PAUSE_BUTTON_FIRST_Y + index * PAUSE_BUTTON_PITCH) * scale;
}

void pause_menu_init(PauseMenu* menu, int selected_class) {
    menu->active = 0;
    menu->page = PAUSE_PAGE_ROOT;
    menu->hovered_button = -1;
    menu->quit_requested = 0;
    menu->selected_class = selected_class;
    menu->class_change_requested = 0;
}

void pause_menu_open(PauseMenu* menu) {
    menu->active = 1;
    /* Always opens on the root page: the class list is somewhere you went, not
       somewhere the menu remembers being. */
    menu->page = PAUSE_PAGE_ROOT;
    menu->hovered_button = -1;
}

void pause_menu_close(PauseMenu* menu) {
    menu->active = 0;
    menu->page = PAUSE_PAGE_ROOT;
    menu->hovered_button = -1;
}

static void button_clicked(PauseMenu* menu, int index) {
    if (menu->page == PAUSE_PAGE_CLASS) {
        if (index >= player_loadout_count()) {
            menu->page = PAUSE_PAGE_ROOT;
            return;
        }
        /* Re-picking the class already in hand is a request like any other: it
           re-arms the weapon, which is exactly what a stuck test wants. */
        menu->selected_class = index;
        menu->class_change_requested = 1;
        return;
    }

    if (index == PAUSE_BUTTON_RESUME) pause_menu_close(menu);
    if (index == PAUSE_BUTTON_SWITCH_CLASS) menu->page = PAUSE_PAGE_CLASS;
    if (index == PAUSE_BUTTON_QUIT) menu->quit_requested = 1;
}

void pause_menu_update(PauseMenu* menu, const InputState* input,
                       const ActionState* actions,
                       int viewport_width, int viewport_height) {
    const int count = page_button_count(menu);
    menu->hovered_button = -1;
    for (int i = 0; i < count; ++i) {
        float x, y, width, height;
        button_bounds(menu, viewport_width, viewport_height, i,
                      &x, &y, &width, &height);
        if (!input_mouse_in_rect(input, x, y, width, height)) continue;

        menu->hovered_button = i;
        if (!input_mouse_pressed(input, GLFW_MOUSE_BUTTON_LEFT)) continue;

        /* A click changes the page under the cursor, so stop walking this
           page's buttons the moment one of them is taken. */
        button_clicked(menu, i);
        break;
    }

    if (action_pressed(actions, ACTION_RESUME)) pause_menu_close(menu);
}

void pause_menu_render(const PauseMenu* menu,
                       int viewport_width, int viewport_height) {
    const int count = page_button_count(menu);
    const float scale = renderer_ui_scale(viewport_height);
    const float panel_width = PAUSE_PANEL_WIDTH * scale;
    const float panel_pixels = panel_height(count) * scale;
    const float header_height = PAUSE_HEADER_HEIGHT * scale;
    const float panel_x = ((float)viewport_width - panel_width) * 0.5f;
    const float panel_y = ((float)viewport_height - panel_pixels) * 0.5f;

    renderer_begin_frame();
    renderer_begin_layer(RENDER_LAYER_UI);
    renderer_set_screen_projection(viewport_width, viewport_height);

    renderer_draw_rect(0, 0, (float)viewport_width, (float)viewport_height,
                       0.015f, 0.02f, 0.035f, 0.62f);
    renderer_draw_rect(panel_x, panel_y, panel_width, panel_pixels,
                       0.08f, 0.10f, 0.15f, 0.97f);
    renderer_draw_rect(panel_x, panel_y, panel_width, header_height,
                       0.15f, 0.27f, 0.43f, 1.0f);
    renderer_draw_text_centered(panel_x, panel_y, panel_width, header_height,
                                page_title(menu));

    for (int i = 0; i < count; ++i) {
        float x, y, width, height;
        button_bounds(menu, viewport_width, viewport_height, i,
                      &x, &y, &width, &height);
        /* The class in hand keeps a lit button whether or not the cursor is on
           it, so the list says which class is worn as well as which are on
           offer. */
        int worn = menu->page == PAUSE_PAGE_CLASS && i == menu->selected_class;
        float shade = menu->hovered_button == i ? 0.34f : (worn ? 0.28f : 0.22f);
        renderer_draw_rect(x, y, width, height,
                           shade, shade + 0.02f, shade + 0.08f, 1.0f);
        renderer_draw_text_centered(x, y, width, height,
                                    button_label(menu, i));
    }

    renderer_end_frame();
}
