#include "input/actions.h"

#include <GLFW/glfw3.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef enum { BIND_KEY, BIND_MOUSE } BindingDevice;
typedef struct { BindingDevice device; int code; } Binding;
typedef struct {
    const char* name;
    Action action;
    Binding default_binding;
    Binding alternate_binding;
} ActionEntry;

#define KEY_BIND(key) { BIND_KEY, GLFW_KEY_##key }
#define MOUSE_BIND(button) { BIND_MOUSE, GLFW_MOUSE_BUTTON_##button }

static const ActionEntry ACTIONS[ACTION_COUNT] = {
    {"move_forward", ACTION_MOVE_FORWARD, KEY_BIND(W), KEY_BIND(UNKNOWN)},
    {"move_backward", ACTION_MOVE_BACKWARD, KEY_BIND(S), KEY_BIND(UNKNOWN)},
    {"move_left", ACTION_MOVE_LEFT, KEY_BIND(A), KEY_BIND(UNKNOWN)},
    {"move_right", ACTION_MOVE_RIGHT, KEY_BIND(D), KEY_BIND(UNKNOWN)},
    {"sprint", ACTION_SPRINT, KEY_BIND(LEFT_SHIFT), KEY_BIND(RIGHT_SHIFT)},
    {"crouch", ACTION_CROUCH, KEY_BIND(LEFT_CONTROL), KEY_BIND(RIGHT_CONTROL)},
    {"jump", ACTION_JUMP, KEY_BIND(SPACE), KEY_BIND(UNKNOWN)},
    {"grapple", ACTION_GRAPPLE, KEY_BIND(F), KEY_BIND(UNKNOWN)},
    {"lean_left", ACTION_LEAN_LEFT, KEY_BIND(Q), KEY_BIND(UNKNOWN)},
    {"lean_right", ACTION_LEAN_RIGHT, KEY_BIND(E), KEY_BIND(UNKNOWN)},
    {"fire", ACTION_FIRE, MOUSE_BIND(LEFT), KEY_BIND(UNKNOWN)},
    {"aim", ACTION_AIM, MOUSE_BIND(RIGHT), KEY_BIND(UNKNOWN)},
    {"reload", ACTION_RELOAD, KEY_BIND(R), KEY_BIND(UNKNOWN)},
    {"toggle_view", ACTION_TOGGLE_VIEW, KEY_BIND(V), KEY_BIND(UNKNOWN)},
    {"front_view", ACTION_FRONT_VIEW, KEY_BIND(B), KEY_BIND(UNKNOWN)},
    {"pause", ACTION_PAUSE, KEY_BIND(ESCAPE), KEY_BIND(UNKNOWN)},
    {"resume", ACTION_RESUME, KEY_BIND(ENTER), KEY_BIND(UNKNOWN)},
    {"debug_damage", ACTION_DEBUG_DAMAGE, KEY_BIND(K), KEY_BIND(UNKNOWN)},
    {"debug_next_weapon", ACTION_DEBUG_NEXT_WEAPON, KEY_BIND(J), KEY_BIND(UNKNOWN)},
    {"debug_reload_placement", ACTION_DEBUG_RELOAD_PLACEMENT,
     KEY_BIND(F5), KEY_BIND(UNKNOWN)},
    {"debug_bake_placement", ACTION_DEBUG_BAKE_PLACEMENT,
     KEY_BIND(F6), KEY_BIND(UNKNOWN)}
};

static Binding bindings[ACTION_COUNT][2];
static int bindings_initialized;

typedef struct { const char* name; int code; } NamedCode;
static const NamedCode SPECIAL_KEYS[] = {
    {"SPACE", GLFW_KEY_SPACE}, {"ENTER", GLFW_KEY_ENTER},
    {"TAB", GLFW_KEY_TAB}, {"BACKSPACE", GLFW_KEY_BACKSPACE},
    {"ESCAPE", GLFW_KEY_ESCAPE}, {"LEFT", GLFW_KEY_LEFT},
    {"RIGHT", GLFW_KEY_RIGHT}, {"UP", GLFW_KEY_UP}, {"DOWN", GLFW_KEY_DOWN},
    {"LSHIFT", GLFW_KEY_LEFT_SHIFT}, {"RSHIFT", GLFW_KEY_RIGHT_SHIFT},
    {"LCTRL", GLFW_KEY_LEFT_CONTROL}, {"RCTRL", GLFW_KEY_RIGHT_CONTROL},
    {"LALT", GLFW_KEY_LEFT_ALT}, {"RALT", GLFW_KEY_RIGHT_ALT},
    {NULL, 0}
};

static void bindings_defaults(void) {
    for (int i = 0; i < ACTION_COUNT; ++i) {
        bindings[ACTIONS[i].action][0] = ACTIONS[i].default_binding;
        bindings[ACTIONS[i].action][1] = ACTIONS[i].alternate_binding;
    }
    bindings_initialized = 1;
}

static char* trim(char* text) {
    while (isspace((unsigned char)*text)) ++text;
    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static int parse_binding(const char* name, Binding* out) {
    if (strcmp(name, "MOUSE_LEFT") == 0) { *out = (Binding)MOUSE_BIND(LEFT); return 1; }
    if (strcmp(name, "MOUSE_RIGHT") == 0) { *out = (Binding)MOUSE_BIND(RIGHT); return 1; }
    if (strcmp(name, "MOUSE_MIDDLE") == 0) { *out = (Binding)MOUSE_BIND(MIDDLE); return 1; }
    if (strncmp(name, "MOUSE_", 6) == 0 && name[6] >= '1' && name[6] <= '8' && !name[7]) {
        *out = (Binding){BIND_MOUSE, name[6] - '1'};
        return 1;
    }
    if (name[0] && !name[1] && isalpha((unsigned char)name[0])) {
        *out = (Binding){BIND_KEY, GLFW_KEY_A + (toupper((unsigned char)name[0]) - 'A')};
        return 1;
    }
    if (name[0] && !name[1] && isdigit((unsigned char)name[0])) {
        *out = (Binding){BIND_KEY, GLFW_KEY_0 + (name[0] - '0')};
        return 1;
    }
    if (name[0] == 'F' && name[1]) {
        int number = 0;
        if (sscanf(name + 1, "%d", &number) == 1 && number >= 1 && number <= 12) {
            *out = (Binding){BIND_KEY, GLFW_KEY_F1 + number - 1};
            return 1;
        }
    }
    for (int i = 0; SPECIAL_KEYS[i].name; ++i) {
        if (strcmp(name, SPECIAL_KEYS[i].name) == 0) {
            *out = (Binding){BIND_KEY, SPECIAL_KEYS[i].code};
            return 1;
        }
    }
    return 0;
}

void actions_load_bindings(const char* path) {
    bindings_defaults();
    FILE* file = fopen(path, "r");
    if (!file) {
        printf("[INPUT] %s not found, using default bindings\n", path);
        return;
    }
    char line[160];
    while (fgets(line, sizeof(line), file)) {
        char* text = trim(line);
        if (!*text || *text == '#') continue;
        char* equals = strchr(text, '=');
        if (!equals) continue;
        *equals = '\0';
        char* action_name = trim(text);
        char* binding_name = trim(equals + 1);
        Binding parsed[2] = {{BIND_KEY, GLFW_KEY_UNKNOWN},
                             {BIND_KEY, GLFW_KEY_UNKNOWN}};
        char* comma = strchr(binding_name, ',');
        if (comma) *comma = '\0';
        if (!parse_binding(trim(binding_name), &parsed[0]) ||
            (comma && !parse_binding(trim(comma + 1), &parsed[1]))) {
            fprintf(stderr, "[INPUT] Unknown binding '%s' for '%s'\n", binding_name, action_name);
            continue;
        }
        int found = 0;
        for (int i = 0; i < ACTION_COUNT; ++i) {
            if (strcmp(action_name, ACTIONS[i].name) == 0) {
                bindings[ACTIONS[i].action][0] = parsed[0];
                bindings[ACTIONS[i].action][1] = parsed[1];
                found = 1;
                break;
            }
        }
        if (!found) fprintf(stderr, "[INPUT] Unknown action '%s'\n", action_name);
    }
    fclose(file);
    printf("[INPUT] Loaded bindings from %s\n", path);
}

static int binding_held(const InputState* input, Binding binding) {
    if (binding.device == BIND_KEY)
        return binding.code != GLFW_KEY_UNKNOWN && input_key_down(input, binding.code);
    return input_mouse_down(input, binding.code);
}

static int binding_pressed(const InputState* input, Binding binding) {
    if (binding.device == BIND_KEY)
        return binding.code != GLFW_KEY_UNKNOWN && input_key_pressed(input, binding.code);
    return input_mouse_pressed(input, binding.code);
}

void actions_decode(ActionState* actions, const InputState* input) {
    if (!bindings_initialized) bindings_defaults();
    for (int i = 0; i < ACTION_COUNT; ++i) {
        int was_held = actions->held[i];
        actions->held[i] = (unsigned char)(binding_held(input, bindings[i][0]) ||
                                           binding_held(input, bindings[i][1]));
        actions->pressed[i] = (unsigned char)(binding_pressed(input, bindings[i][0]) ||
                                              binding_pressed(input, bindings[i][1]));
        actions->released[i] = (unsigned char)(was_held && !actions->held[i]);
    }
    actions->look_x = input->pointer.delta_x;
    actions->look_y = input->pointer.delta_y;
}

int action_held(const ActionState* actions, Action action) {
    return action >= 0 && action < ACTION_COUNT && actions->held[action];
}
int action_pressed(const ActionState* actions, Action action) {
    return action >= 0 && action < ACTION_COUNT && actions->pressed[action];
}
int action_released(const ActionState* actions, Action action) {
    return action >= 0 && action < ACTION_COUNT && actions->released[action];
}
