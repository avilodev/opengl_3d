#include "states/pause_menu.h"

#include "render/renderer.h"

enum {
    PAUSE_BUTTON_RESUME,
    PAUSE_BUTTON_QUIT,
    PAUSE_BUTTON_COUNT
};

/* Authored at 720p and scaled, so the panel keeps its proportions on a native
   resolution display instead of becoming a postage stamp in the middle of it. */
#define PAUSE_PANEL_WIDTH 340.0f
#define PAUSE_PANEL_HEIGHT 230.0f
#define PAUSE_HEADER_HEIGHT 64.0f
#define PAUSE_BUTTON_WIDTH 240.0f
#define PAUSE_BUTTON_HEIGHT 52.0f
#define PAUSE_BUTTON_FIRST_Y 92.0f
#define PAUSE_BUTTON_PITCH 68.0f

static void button_bounds(int viewport_width, int viewport_height, int index,
                          float* x, float* y, float* width, float* height) {
    float scale = renderer_ui_scale(viewport_height);
    *width = PAUSE_BUTTON_WIDTH * scale;
    *height = PAUSE_BUTTON_HEIGHT * scale;
    *x = ((float)viewport_width - *width) * 0.5f;
    *y = ((float)viewport_height - PAUSE_PANEL_HEIGHT * scale) * 0.5f +
         (PAUSE_BUTTON_FIRST_Y + index * PAUSE_BUTTON_PITCH) * scale;
}

void pause_menu_init(PauseMenu* menu) {
    menu->active = 0;
    menu->hovered_button = -1;
    menu->quit_requested = 0;
}

void pause_menu_open(PauseMenu* menu) {
    menu->active = 1;
    menu->hovered_button = -1;
}

void pause_menu_close(PauseMenu* menu) {
    menu->active = 0;
    menu->hovered_button = -1;
}

void pause_menu_update(PauseMenu* menu, const InputState* input,
                       const ActionState* actions,
                       int viewport_width, int viewport_height) {
    menu->hovered_button = -1;
    for (int i = 0; i < PAUSE_BUTTON_COUNT; ++i) {
        float x, y, width, height;
        button_bounds(viewport_width, viewport_height, i,
                      &x, &y, &width, &height);
        if (!input_mouse_in_rect(input, x, y, width, height)) continue;

        menu->hovered_button = i;
        if (!input_mouse_pressed(input, GLFW_MOUSE_BUTTON_LEFT)) continue;

        if (i == PAUSE_BUTTON_RESUME) pause_menu_close(menu);
        if (i == PAUSE_BUTTON_QUIT) menu->quit_requested = 1;
    }

    if (action_pressed(actions, ACTION_RESUME)) pause_menu_close(menu);
}

void pause_menu_render(const PauseMenu* menu,
                       int viewport_width, int viewport_height) {
    const float scale = renderer_ui_scale(viewport_height);
    const float panel_width = PAUSE_PANEL_WIDTH * scale;
    const float panel_height = PAUSE_PANEL_HEIGHT * scale;
    const float header_height = PAUSE_HEADER_HEIGHT * scale;
    const float panel_x = ((float)viewport_width - panel_width) * 0.5f;
    const float panel_y = ((float)viewport_height - panel_height) * 0.5f;
    const char* labels[PAUSE_BUTTON_COUNT] = {"Resume", "Exit Game"};

    renderer_begin_frame();
    renderer_begin_layer(RENDER_LAYER_UI);
    renderer_set_screen_projection(viewport_width, viewport_height);

    renderer_draw_rect(0, 0, (float)viewport_width, (float)viewport_height,
                       0.015f, 0.02f, 0.035f, 0.62f);
    renderer_draw_rect(panel_x, panel_y, panel_width, panel_height,
                       0.08f, 0.10f, 0.15f, 0.97f);
    renderer_draw_rect(panel_x, panel_y, panel_width, header_height,
                       0.15f, 0.27f, 0.43f, 1.0f);
    renderer_draw_text_centered(panel_x, panel_y, panel_width, header_height,
                                "Paused");

    for (int i = 0; i < PAUSE_BUTTON_COUNT; ++i) {
        float x, y, width, height;
        button_bounds(viewport_width, viewport_height, i,
                      &x, &y, &width, &height);
        float shade = menu->hovered_button == i ? 0.34f : 0.22f;
        renderer_draw_rect(x, y, width, height,
                           shade, shade + 0.02f, shade + 0.08f, 1.0f);
        renderer_draw_text_centered(x, y, width, height, labels[i]);
    }

    renderer_end_frame();
}
