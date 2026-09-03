#include "input/input.h"
#include <string.h>

static void update_button(InputButtonState* state, int down) {
    int was_down = state->down;
    state->down = (unsigned char)down;
    state->pressed = (unsigned char)(down && !was_down);
    state->released = (unsigned char)(!down && was_down);
}

static void scroll_callback(GLFWwindow* window, double x_offset, double y_offset) {
    (void)x_offset;
    InputState* input = (InputState*)glfwGetWindowUserPointer(window);
    if (input) input->pending_scroll_y += (float)y_offset;
}

void input_init(InputState* input) {
    memset(input, 0, sizeof(InputState));
}

void input_update(InputState* input, GLFWwindow* window,
                  int viewport_width, int viewport_height,
                  int window_width, int window_height) {
    if (window_width < 1) window_width = 1;
    if (window_height < 1) window_height = 1;

    if (input->attached_window != window) {
        input->attached_window = window;
        glfwSetWindowUserPointer(window, input);
        glfwSetScrollCallback(window, scroll_callback);
    }

    input->pointer.scroll_y = input->pending_scroll_y;
    input->pending_scroll_y = 0.0f;
    
    // Update mouse position with viewport scaling
    double raw_x, raw_y;
    glfwGetCursorPos(window, &raw_x, &raw_y);

    if (!input->pointer.cursor_initialized) {
        input->pointer.previous_cursor_x = raw_x;
        input->pointer.previous_cursor_y = raw_y;
        input->pointer.cursor_initialized = 1;
    }
    input->pointer.delta_x = (float)(raw_x - input->pointer.previous_cursor_x);
    input->pointer.delta_y = (float)(raw_y - input->pointer.previous_cursor_y);
    input->pointer.previous_cursor_x = raw_x;
    input->pointer.previous_cursor_y = raw_y;
    
    input->pointer.viewport_x = (float)(raw_x * ((double)viewport_width / (double)window_width));
    input->pointer.viewport_y = (float)(raw_y * ((double)viewport_height / (double)window_height));
    
    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button)
        update_button(&input->mouse_buttons[button],
                      glfwGetMouseButton(window, button) == GLFW_PRESS);
    
    // Update keyboard state
    for (int key = 0; key <= GLFW_KEY_LAST; key++) {
        update_button(&input->keys[key], glfwGetKey(window, key) == GLFW_PRESS);
    }
}

void input_set_cursor_captured(InputState* input, GLFWwindow* window, int captured) {
    glfwSetInputMode(window, GLFW_CURSOR,
                     captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    input->pointer.cursor_initialized = 0;
    input->pointer.delta_x = 0.0f;
    input->pointer.delta_y = 0.0f;
}

int input_key_down(const InputState* input, int key) {
    if (key < 0 || key > GLFW_KEY_LAST) return 0;
    return input->keys[key].down;
}

int input_key_pressed(const InputState* input, int key) {
    if (key < 0 || key > GLFW_KEY_LAST) return 0;
    return input->keys[key].pressed;
}

int input_key_released(const InputState* input, int key) {
    if (key < 0 || key > GLFW_KEY_LAST) return 0;
    return input->keys[key].released;
}

int input_mouse_down(const InputState* input, int button) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return 0;
    return input->mouse_buttons[button].down;
}

int input_mouse_pressed(const InputState* input, int button) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return 0;
    return input->mouse_buttons[button].pressed;
}

int input_mouse_released(const InputState* input, int button) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return 0;
    return input->mouse_buttons[button].released;
}

int input_mouse_in_rect(const InputState* input, float x, float y, float w, float h) {
    return (input->pointer.viewport_x >= x && input->pointer.viewport_x <= x + w &&
            input->pointer.viewport_y >= y && input->pointer.viewport_y <= y + h);
}
