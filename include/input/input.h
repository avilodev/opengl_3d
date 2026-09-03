#ifndef INPUT_H
#define INPUT_H

#include <GLFW/glfw3.h>

typedef struct {
    unsigned char down;
    unsigned char pressed;
    unsigned char released;
} InputButtonState;

typedef struct {
    float viewport_x;
    float viewport_y;
    float delta_x;
    float delta_y;
    float scroll_y;
    double previous_cursor_x;
    double previous_cursor_y;
    int cursor_initialized;
} InputPointerState;

typedef struct {
    InputButtonState keys[GLFW_KEY_LAST + 1];
    InputButtonState mouse_buttons[GLFW_MOUSE_BUTTON_LAST + 1];
    InputPointerState pointer;
    GLFWwindow* attached_window;
    float pending_scroll_y;
} InputState;

// ============================================================================
// INPUT MODULE
// Handles all keyboard/mouse input processing
// ============================================================================

// Initialize input state
void input_init(InputState* input);

// Update input state (call once per frame, before processing)
void input_update(InputState* input, GLFWwindow* window, 
                  int viewport_width, int viewport_height,
                  int window_width, int window_height);

/* Capture/release the cursor for camera controls. */
void input_set_cursor_captured(InputState* input, GLFWwindow* window, int captured);

// Query functions
int input_key_down(const InputState* input, int key);
int input_key_pressed(const InputState* input, int key);
int input_key_released(const InputState* input, int key);
int input_mouse_down(const InputState* input, int button);
int input_mouse_pressed(const InputState* input, int button);
int input_mouse_released(const InputState* input, int button);
int input_mouse_in_rect(const InputState* input, float x, float y, float w, float h);

#endif // INPUT_H
