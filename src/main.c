#include <GLFW/glfw3.h>

#include <stdio.h>

#include "gameplay/combatant.h"
#include "gameplay/arena.h"
#include "gameplay/player_combatant.h"
#include "gameplay/player_controller.h"
#include "gameplay/weapon_tuning.h"
#include "gameplay/player_visual_state.h"
#include "gameplay/spawn_select.h"
#include "gameplay/third_person_camera.h"
#include "gameplay/weapon.h"
#include "input/actions.h"
#include "input/commands.h"
#include "input/input.h" 
#include "render/character_library.h"
#include "render/character_renderer.h"
#include "render/character_transform.h"
#include "render/combatant_renderer.h"
#include "render/hud.h"
#include "render/renderer.h"
#include "render/renderer3d.h"
#include "render/weapon_renderer.h"
#include "states/pause_menu.h"

#define WINDOW_TITLE "Movement Shooter 3D"
/* Only used when there is no monitor to ask, which on Windows means no display
   is attached at all. */
#define WINDOW_FALLBACK_WIDTH 1280
#define WINDOW_FALLBACK_HEIGHT 720

static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    (void)window;
    renderer3d_resize(width, height);
}

/* Opens at the monitor's own resolution instead of a fixed size. Hinting the
   monitor's current video mode and then creating the window *on* that monitor
   asks for the mode the desktop is already in, so this comes up as a borderless
   full-screen window with no mode switch to flicker through and no scaling. */
static GLFWwindow* create_window(void) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : NULL;
    if (!mode)
        return glfwCreateWindow(WINDOW_FALLBACK_WIDTH, WINDOW_FALLBACK_HEIGHT,
                                WINDOW_TITLE, NULL, NULL);

    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height,
                                          WINDOW_TITLE, monitor, NULL);
    /* A driver that refuses the exclusive mode still owes us the size. */
    if (!window)
        window = glfwCreateWindow(mode->width, mode->height,
                                  WINDOW_TITLE, NULL, NULL);
    return window;
}

/* The fixed simulation step. 120Hz divides a 60Hz display evenly, so vsync
   normally means exactly two steps a frame and no beat frequency between the
   two rates. Every movement constant is tuned against this length.

   The catch-up cap stops a stalled frame - a drag, a minimize, an alt-tab -
   from being paid back as a burst of steps that teleports the player. Time
   past it is simply dropped. */
#define SIMULATION_STEP (1.0f / 120.0f)
#define SIMULATION_MAX_CATCHUP 0.1f

int main(void) {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    /* A compatibility context works with the bundled Windows GLFW library.
       The renderer boundary allows a later shader backend to replace it. */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    /* A magnified sight is drawn as a second pass of the world masked to the
       optic's glass, which needs somewhere to keep the mask. */
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = create_window();
    if (!window) {
        fprintf(stderr, "Failed to create the OpenGL window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    int framebuffer_width;
    int framebuffer_height;
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    renderer3d_init(framebuffer_width, framebuffer_height);
    renderer_init(framebuffer_width, framebuffer_height);
    renderer_font_init("assets/menu.ttf",
                       RENDERER_UI_BASE_FONT_SIZE *
                       renderer_ui_scale(framebuffer_height));

    InputState input;
    ActionState actions = {0};
    PlayerCommand player_command = {0};
    CameraCommand camera_command = {0};
    WeaponCommand weapon_command = {0};
    PlayerController player;
    ThirdPersonCamera camera;
    PauseMenu pause_menu;
    Weapon weapon;
    /* The local player's body as something drawn in the world, kept here
       rather than inside the renderer because a body is not a property of the
       view: a second player would be a second pair of these next to this one,
       and the renderer would draw both without knowing which was which.

       `body_state` is refilled from the controller every frame and is also,
       field for field, what would go on a wire; `body` is the playhead and the
       skinned vertices it drives, which are this body's alone. */
    /* The rigs this world has bodies wearing, owned here for the same reason
       the body is: a library outlives every body in it, and the renderer
       should no more own the asset than it owns the player. One entry today;
       a second character type is a second character_library_add. */
    CharacterLibrary characters = {0};
    CharacterInstance body = {0};
    CharacterVisualState body_state = {0};
    /* Zero means "no previous frame", which is what suppresses a landing
       animation on the frame the player spawns standing on the floor. */
    int body_state_valid = 0;
    /* Named rather than passed inline, because it is now worn by more than one
       body: every combatant in the arena is drawn from this one rig. That is
       the whole promise of the library - eight megabytes of mesh, skeleton and
       clips read once however many bodies wear them - and a second character
       type would be a second character_library_add and a different index here. */
    int player_rig = character_library_add(&characters,
                                           CHARACTER_RIG_PLAYER_MODEL,
                                           CHARACTER_RIG_PLAYER_CLIPS);
    character_instance_init(&body, player_rig);

    /* The other bodies in the world, split the same way the player's is: the
       set is where they are and what has been done to them, the bodies are what
       each of them is doing with the shared rig. */
    CombatantSet combatants;
    CombatantBodies combatant_bodies = {0};
    combatant_set_init(&combatants);
    combatant_bodies_init(&combatant_bodies, &combatants, player_rig);

    input_init(&input);
    actions_load_bindings("data/keybinds.cfg");
    player_controller_init(&player);
    third_person_camera_init(&camera);
    /* Until team selection exists the player is on Bastion. WHICH of Bastion's
       six slots is not decided here and never will be: spawn_select answers it
       from the map's own table and the bodies standing in it, and it answers
       the first entry with the same call as every later respawn. Entering the
       map and re-entering it are one act. */
    Combatant player_body;
    ArenaSpawn spawn;
    if (spawn_select(ARENA_TEAM_BASTION, combatants.items, combatants.count,
                     &spawn)) {
        player_controller_place(&player, spawn.x, spawn.y, spawn.z,
                                spawn.yaw_degrees);
        camera.yaw_degrees = spawn.yaw_degrees;
    }
    /* The player's own body: the same struct as everything else in the world
       that a round can land on, filled from the controller each step. */
    player_combatant_init(&player_body, &player);
    pause_menu_init(&pause_menu);
    /* The auto pistol (WEAPON_SPEC_AUTO_PISTOL) is still here and still
       works; the player is simply not carrying it yet. */
    weapon_init(&weapon, &WEAPON_SPEC_KATANA);
    /* The body in third person holds what the hands in first person hold. One
       assignment, off the weapon rather than off a second name for it, so the
       two views cannot end up carrying different guns. */
    body.weapon = weapon.spec;
    body.sheathed = weapon.sheathed;
    /* What its rounds can land on. Published once: the set of bodies in the
       world outlives every shot, and a weapon that was handed them per step
       would be a weapon whose signature grew every time the world did. */
    weapon_set_targets(&weapon, &combatants);
    input_set_cursor_captured(&input, window, 1);

    /* The renderer string is the one that answers "is this actually running on
       the graphics card". A driver that never loaded leaves Windows to answer
       the context request out of opengl32.dll itself, which reports "GDI
       Generic" at version 1.1 and rasterises on the CPU - the frame rate says
       something is wrong but not what, and this line does. On a laptop it also
       says which of the two cards took the job. */
    printf("OpenGL: %s\n", glGetString(GL_VERSION));
    printf("Renderer: %s (%s)\n", glGetString(GL_RENDERER), glGetString(GL_VENDOR));
    printf("Resolution: %dx%d\n", framebuffer_width, framebuffer_height);
    printf("Controls: WASD moves, Q/E lean, Shift sprints, Ctrl slides, Space jumps, F grapples, V cycles view, B looks from the front, Escape pauses.\n");
    /* Two weapons, two sets of buttons on the same two mice. */
    if (weapon_spec_melee(weapon.spec))
        printf("Weapon: %s - Left Mouse swings and draws it if it is away "
               "(again inside a swing to chain it), Right Mouse blocks, both "
               "together for the heavy, R sheathes. It puts itself away after "
               "%.0f seconds unused.\n",
               weapon.spec->name, (double)MELEE_AUTO_SHEATHE_SECONDS);
    else
    printf("Weapon: %s - hold Left Mouse to fire, Right Mouse to aim, R reloads.\n",
           weapon.spec->name);
    printf("Debug: K takes %.0f health off you - nothing in the world shoots back yet.\n",
           (double)COMBAT_DEBUG_DAMAGE);
    printf("Map: Floodgate K-12 - Bastion Railhead to Axiom Triage, 6v6.\n");
    /* Startup diagnostics are worth having after a crash, not just after a
       clean exit, so do not leave them sitting in the buffer. */
    fflush(stdout);

    double previous_time = glfwGetTime();
    float simulation_accumulator = 0.0f;
    /* A frame is not guaranteed to run a simulation step - on any display
       faster than the simulation rate, a good fraction of them run none at all.
       The edges those frames produced still have to reach the simulation, so
       they are held here until a step consumes them instead of being read
       straight off whichever frame happens to be current. Reading them live
       silently dropped roughly one press in six at 144Hz, which is a jump that
       does not fire. */
    int pending_jump = 0;
    /* Lean is a toggle driven by a press edge, so it needs latching exactly as
       the jump does - and needs it more, because both failure modes are
       visible. Dropped on a frame that ran no step, the lean simply never
       happens; replayed across two steps it toggles on and straight back off,
       which looks the same as a dead key. At a 120Hz step a 60Hz display runs
       two steps every frame and a 144Hz one regularly runs none, so unlatched
       it is broken on both. */
    int pending_lean_left = 0;
    int pending_lean_right = 0;
    int pending_crouch = 0;
    int pending_grapple = 0;
    int pending_reload = 0;
    /* Latched like every other press, and for the same reason: a frame that
       runs no simulation step would otherwise drop it. */
    int pending_debug_damage = 0;
    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        float delta_time = (float)(current_time - previous_time);
        previous_time = current_time;
        /* Avoid a large movement jump after dragging or minimizing the window. */
        if (delta_time > 0.1f) delta_time = 0.1f;

        int window_width;
        int window_height;
        glfwGetWindowSize(window, &window_width, &window_height);
        glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
        input_update(&input, window, framebuffer_width, framebuffer_height,
                     window_width, window_height);
        actions_decode(&actions, &input);
        commands_decode(&actions, &player_command, &camera_command,
                        &weapon_command);

        if (action_pressed(&actions, ACTION_PAUSE)) {
            if (pause_menu.active) pause_menu_close(&pause_menu);
            else pause_menu_open(&pause_menu);
            input_set_cursor_captured(&input, window, !pause_menu.active);
            /* The mouse belonged to the menu a moment ago. Whichever way this
               went, the next shot has to come from a fresh trigger pull. */
            weapon_latch_trigger(&weapon);
        }

        if (pause_menu.active) {
            pause_menu_update(&pause_menu, &input, &actions,
                              framebuffer_width, framebuffer_height);
            if (!pause_menu.active) {
                input_set_cursor_captured(&input, window, 1);
                /* Resume was clicked with the same button that fires. */
                weapon_latch_trigger(&weapon);
            }
            if (pause_menu.quit_requested)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            /* Nothing pressed before the menu opened should fire on resume. */
            pending_jump = pending_crouch = pending_grapple = pending_reload = 0;
            pending_lean_left = pending_lean_right = 0;
            pending_debug_damage = 0;
        } else {
            /* The simulation runs on a fixed step and the frame feeds it time.
               Movement is full of per-step decisions - a clamped air-strafe
               addition, a contact test, a timer crossing a threshold - whose
               outcome depends on how long a step is, so a variable step means
               the game does not play the same at 60 frames a second as at 144.
               Rendering stays free-running; only the simulation is quantized. */
            simulation_accumulator += delta_time;
            if (simulation_accumulator > SIMULATION_MAX_CATCHUP)
                simulation_accumulator = SIMULATION_MAX_CATCHUP;

            pending_jump |= player_command.jump_pressed;
            pending_lean_left |= player_command.lean_left_pressed;
            pending_lean_right |= player_command.lean_right_pressed;
            pending_crouch |= player_command.crouch_pressed;
            pending_grapple |= player_command.grapple_pressed;
            pending_reload |= weapon_command.reload_pressed;
            /* Read straight off the action table rather than through a command
               struct, the way the pause key is: this is a development control
               and not a decision the simulation owns, and giving it a field in
               PlayerCommand would be teaching gameplay a key exists. */
            pending_debug_damage |= action_pressed(&actions,
                                                   ACTION_DEBUG_DAMAGE);

            /* Aiming is not simulation. A mouse delta is an amount the frame
               already measured, and deferring it to the next step that happens
               to run makes turning stutter on a display faster than the
               simulation rate. The angles it leaves behind are what the steps
               below then read. */
            third_person_camera_apply_look(&camera, &camera_command);

            /* Where the body's own rifle is, for the frames the player is
               looking at that body rather than down the viewmodel. Read off the
               pose the last frame drew, which is what makes it a frame stale
               and why it is set here rather than inside a step: a streak's
               origin does not need this frame's hand, it needs the hand the
               player can see. In first person there is no body on screen and
               the viewmodel's own barrel is the right answer, so nothing is
               published and the weapon falls back to it. */
            float body_muzzle[3] = {0.0f, 0.0f, 0.0f};
            /* A weapon with no barrel has no muzzle to publish, and a streak
               that left one would be leaving a sword. */
            const FirearmSpec* body_gun = weapon_spec_firearm(weapon.spec);
            int body_muzzle_valid = body_gun && !camera.is_first_person &&
                character_weapon_muzzle(character_library_model(&characters, body.rig),
                                        &body, &body_state,
                                        body_gun->muzzle_up,
                                        body_gun->muzzle_forward, body_muzzle);
            weapon_set_world_muzzle(&weapon, body_muzzle, body_muzzle_valid);

            /* And where the animated hand has a melee weapon, for the same
               reason and at the same point: it comes off a rig and a clip,
               which is not something the simulation should have to hold, and
               everything that composes with it runs after this line. */
            ViewmodelPose melee_base;
            int melee_base_valid = weapon_renderer_melee_base(
                &characters, player_rig, &weapon, &melee_base);
            weapon_set_melee_base(&weapon, &melee_base, melee_base_valid);
            /* One bit, read off the weapon that owns it: a sword sheathed in
               third person and drawn in first would be two swords. */
            body.sheathed = weapon.sheathed;

            /* Where the sword actually is, off the body's own hand rather than
               off the viewmodel. The viewmodel is drawn through a projection of
               its own, and a camera-space offset put straight into the world
               lands about half as far off centre as the thing it came from -
               a correction the tracer has to make and a hit test should not.
               The hand is already in world units, and it is in the same place
               whether or not anyone is drawing it. */
            Mat4 blade;
            int blade_valid = weapon_spec_melee(weapon.spec) &&
                character_weapon_world(character_library_model(&characters,
                                                               body.rig),
                                       &body, &body_state, &blade);
            weapon_set_blade_transform(&weapon, &blade, blade_valid);

            while (simulation_accumulator >= SIMULATION_STEP) {
                PlayerCommand step_player = player_command;
                CameraCommand step_camera = camera_command;
                WeaponCommand step_weapon = weapon_command;
                /* Held states come from the live frame. A press is an edge and
                   belongs to exactly one step - the first one to run after it
                   happened - so it is taken from the latch and the latch is
                   cleared. Replaying it would fire a jump per step; reading it
                   off the frame would lose it whenever no step ran. */
                step_player.jump_pressed = pending_jump;
                step_player.lean_left_pressed = pending_lean_left;
                step_player.lean_right_pressed = pending_lean_right;
                step_player.crouch_pressed = pending_crouch;
                step_player.grapple_pressed = pending_grapple;
                step_weapon.reload_pressed = pending_reload;
                pending_jump = 0;
                pending_lean_left = 0;
                pending_lean_right = 0;
                pending_crouch = 0;
                pending_grapple = 0;
                pending_reload = 0;
                int debug_damage = pending_debug_damage;
                pending_debug_damage = 0;
                /* Aiming and the view toggle were already applied for this
                   frame, at frame rate, above. */
                step_camera.look_x = 0.0f;
                step_camera.look_y = 0.0f;
                step_camera.toggle_view_pressed = 0;
                step_camera.front_view_pressed = 0;

                /* Published by the weapon last step; a step of lag on a 0.16s
                   ease is not visible, and taking it here keeps the camera from
                   having to know what a weapon is. */
                /* A magnified optic narrows only its own circle, so the
                   periphery keeps its own field of view. Without a stencil
                   buffer there is nowhere to mask that circle, and it falls
                   back to zooming the whole screen the way the unmagnified
                   sight does. */
                third_person_camera_set_zoom(&camera, weapon.ads,
                                             weapon_spec_ads_fov(weapon.spec),
                                             weapon_sight_is_magnified(&weapon) &&
                                             renderer3d_supports_scope_pass());
                /* A dead player does not move and does not shoot. The camera
                   still runs, because a view that locks up reads as the game
                   having hung rather than as the player having died - and it
                   is what lets them watch the room they lost. */
                int player_alive = player_body.alive;
                if (player_alive) {
                    player_controller_update_lean(&player, &step_player,
                                                  SIMULATION_STEP);
                }
                third_person_camera_update(&camera, &step_camera, &player,
                                           SIMULATION_STEP);
                if (player_alive) {
                    player_controller_update(&player, &step_player,
                                             camera.yaw_degrees,
                                             camera.pitch_degrees,
                                             SIMULATION_STEP);
                }
                /* Where the controller just left them. Position and facing
                   only - the pool and the count are the body's own. */
                player_combatant_fill(&player_body, &player);
                if (debug_damage) {
                    combatant_damage(&player_body, COMBATANT_REGION_CHEST,
                                     COMBAT_DEBUG_DAMAGE);
                }
                /* Before the weapon and after the player, so a round leaving
                   this step resolves against where the bodies are during it
                   rather than where they were at the end of the last one. */
                combatant_set_update(&combatants, player.x, player.z,
                                     SIMULATION_STEP);
                /* After the camera, so the shot leaves along the angles the step
                   is about to be drawn at. The kick is not handed back here any
                   more: it stays on the gun, which is what the reticle is drawn
                   on, so the sight and the round cannot disagree. */
                if (player_alive) {
                    weapon_update(&weapon, &step_weapon, &player,
                                  camera.yaw_degrees, camera.pitch_degrees,
                                  camera.roll_degrees, camera.target_height,
                                  camera.fov_degrees, SIMULATION_STEP);
                }
                /* Last, so a player killed by this step's damage is down for
                   the whole of the count and not a step less. */
                if (combatant_tick(&player_body, SIMULATION_STEP)) {
                    ArenaSpawn respawn;
                    if (spawn_select(ARENA_TEAM_BASTION, combatants.items,
                                     combatants.count, &respawn)) {
                        player_controller_place(&player, respawn.x, respawn.y,
                                                respawn.z, respawn.yaw_degrees);
                        camera.yaw_degrees = respawn.yaw_degrees;
                    }
                    combatant_respawn(&player_body, player.x, player.y, player.z,
                                      player.heading_degrees);
                    /* A fresh magazine is part of coming back, and so is a
                       trigger that has to be pulled again - the fire button may
                       well still be held from the fight that killed them, and
                       weapon_init latches it. Re-initialising drops the targets
                       with everything else, so they are published again: the
                       same pair of calls that armed the weapon at startup. */
                    const WeaponSpec* spec = weapon.spec;
                    weapon_init(&weapon, spec);
                    weapon_set_targets(&weapon, &combatants);
                }
                simulation_accumulator -= SIMULATION_STEP;
            }
        }

        /* Once a frame, off the controller the simulation just stepped. The
           edges - jumped, landed, started sliding - are the difference against
           last frame's state, which is why the same struct is passed in as
           both: a body whose state came off a wire gets those bits from the
           packet instead, and the rig cannot tell the two apart. */
        player_visual_state_fill(&body_state,
                                 body_state_valid ? &body_state : NULL, &player,
                                 camera.yaw_degrees, camera.pitch_degrees,
                                 weapon.ads);
        body_state_valid = 1;

        /* The swing, on the body, as a one-shot. Driven off which attack the
           weapon says is running rather than off the input, so the animation
           and the blade that is cutting are the same decision - the third
           person cannot be swinging a clip the hit detection knows nothing
           about. The clip is looked up each time it changes and not each
           frame, which is what the comparison against the last one is for. */
        {
            static int played_attack = -2;
            static int played_recovery = -1;
            const SkinnedModel* rig = character_library_model(&characters,
                                                              body.rig);
            const MeleeSpec* body_melee = weapon_spec_melee(weapon.spec);
            if (rig && body_melee &&
                (weapon.melee_attack != played_attack ||
                 weapon.melee_recovering != played_recovery)) {
                played_attack = weapon.melee_attack;
                played_recovery = weapon.melee_recovering;
                if (weapon.melee_attack >= 0) {
                    /* Whichever half of the attack is running - the swing or
                       the settle after it - asked of the weapon rather than
                       worked out here, so the body plays what the blade is
                       doing and not a second opinion about it. */
                    float time = 0.0f;
                    int clip = skinned_model_find_clip(rig,
                        weapon_melee_clip(&weapon, &time));
                    character_animation_play_oneshot(&body.animation, rig, clip,
                        weapon.melee_recovering ? MELEE_RECOVER_SECONDS
                                                : MELEE_ATTACK_SECONDS);
                }
            }
        }

        /* A paused game keeps its pose: the body is not simulated while the
           menu is up, so animating it would have it walking on the spot. */
        renderer3d_draw(&camera, &player, &weapon, &characters, &body, &body_state,
                        &player_body, &combatants, &combatant_bodies,
                        pause_menu.active ? 0.0f : delta_time);
        if (pause_menu.active)
            pause_menu_render(&pause_menu, framebuffer_width, framebuffer_height);
        else
            hud_draw(&weapon, &camera, &player, &combatants, &player_body,
                     framebuffer_width, framebuffer_height);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    /* All of these give GL names back, so they run before the window that owns
       the context goes away. Bodies before the library they were wearing: an
       instance's buffers are its own, and the rigs behind them outlive it. */
    character_instance_free(&body);
    combatant_bodies_free(&combatant_bodies);
    character_library_free(&characters);
    renderer_cleanup();
    renderer3d_shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
