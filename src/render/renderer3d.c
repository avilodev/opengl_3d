#include "render/renderer3d.h"
#include "render/character_renderer.h"
#include "render/combatant_renderer.h"
#include "gameplay/traversal.h"
#include "render/weapon_renderer.h"
#include "render/viewmodel_arms.h"
#include "render/viewmodel_neck.h"
#include "render/character_animation.h"
#include "render/weapon_model.h"
#include "render/primitives.h"
#include "utils/math_utils.h"
#include "gameplay/arena.h"
#include "gameplay/movement_tuning.h"

#include <GLFW/glfw3.h>
#include <math.h>

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

static float g_aspect_ratio = 16.0f / 9.0f;
static int g_viewport_width = 1280;
static int g_viewport_height = 720;
static int g_stencil_bits = 0;

/* Segments in the disc the scope pass is masked to. Its edge is covered by the
   optic's own rim, so this only has to be smooth enough not to show through. */
#define SCOPE_DISC_SEGMENTS 96

static void set_perspective(float fov, float aspect, float near_plane,
                            float far_plane) {
    double top = near_plane * tan(fov * MATH_PI / 360.0);
    double right = top * aspect;
    glFrustum(-right, right, -top, top, near_plane, far_plane);
}


/* Metre lines read as scale up close but turn to moire across an arena this
   wide, so they are drawn dim and a brighter line every ten metres carries the
   distance instead. */
#define GRID_MAJOR_STEP 10

static void draw_grid(void) {
    glDisable(GL_LIGHTING);
    glBegin(GL_LINES);
    for (int pass = 0; pass < 2; ++pass) {
        int step = pass == 0 ? 1 : GRID_MAJOR_STEP;
        if (pass == 0) glColor3f(.16f, .19f, .25f);
        else glColor3f(.27f, .32f, .41f);
        for (int i = -(int)ARENA_HALF_SIZE; i <= (int)ARENA_HALF_SIZE; i += step) {
            if (pass == 0 && i % GRID_MAJOR_STEP == 0) continue;
            glVertex3f((float)i, 0, -ARENA_HALF_SIZE);
            glVertex3f((float)i, 0, ARENA_HALF_SIZE);
            glVertex3f(-ARENA_HALF_SIZE, 0, (float)i);
            glVertex3f(ARENA_HALF_SIZE, 0, (float)i);
        }
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

static void draw_walls(void) {
    for (int i = 0; i < ARENA_WALL_COUNT; ++i) {
        const ArenaWall* wall = &g_arena_walls[i];
        /* Material is navigation before it is decoration: amber points back
           to Bastion, clean cyan points back to Axiom, and blue/hazard surfaces
           name the spillway and gate machinery at a glance. Height still
           shades each family so mantle cover reads apart from a tall route. */
        float tall = fminf(wall->height / 16.0f, 1.0f);
        switch (wall->material) {
        case ARENA_MATERIAL_BASTION:
            glColor3f(.52f + tall * .12f, .31f + tall * .04f,
                      .12f + tall * .03f);
            break;
        case ARENA_MATERIAL_AXIOM:
            glColor3f(.17f + tall * .06f, .50f + tall * .10f,
                      .54f + tall * .12f);
            break;
        case ARENA_MATERIAL_WATER:
            glColor3f(.08f, .25f + tall * .08f, .39f + tall * .12f);
            break;
        case ARENA_MATERIAL_HAZARD:
            glColor3f(.62f + tall * .13f, .17f, .10f);
            break;
        default:
            glColor3f(.27f + tall * .15f, .34f + tall * .06f,
                      .38f + tall * .07f);
            break;
        }
        glPushMatrix();
        glTranslatef(wall->x, wall->height * 0.5f, wall->z);
        primitives_draw_box(wall->width, wall->height, wall->depth);
        glPopMatrix();
    }
}

static void draw_grapple(const PlayerController* player) {
    if (!player->grapple.active) return;
    glDisable(GL_LIGHTING);
    glColor3f(.82f, .86f, .92f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex3f(player->x, player->y + 1.45f, player->z);
    glVertex3f(player->grapple.x, player->grapple.y, player->grapple.z);
    glEnd();
    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

void renderer3d_resize(int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    glViewport(0, 0, width, height);
    g_viewport_width = width;
    g_viewport_height = height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    g_aspect_ratio = (float)width / (float)height;
    set_perspective(60, g_aspect_ratio, .1f, 300);
    glMatrixMode(GL_MODELVIEW);
}

void renderer3d_init(int width, int height) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glShadeModel(GL_SMOOTH);
    glClearColor(.055f, .075f, .11f, 1);
    glGetIntegerv(GL_STENCIL_BITS, &g_stencil_bits);
    renderer3d_resize(width, height);
}

int renderer3d_supports_scope_pass(void) {
    return g_stencil_bits > 0;
}

/* One pass of the world from the camera, at whatever field of view the caller
   wants. Drawn twice when a magnified sight is up: once wide for everything
   around the optic, once narrow for what is inside it.

   The PLAYER's body is not in both. A scope is only drawn in first person and
   that body only in third, so on any given frame at most one of them is on
   screen and the second pass never carries it. Every other body is in both:
   what is inside a magnified optic is the world, and a target that vanished
   the moment the player looked at it through a sight would be a hole in the
   one view a player uses to aim. */
static void draw_world(const ThirdPersonCamera* camera,
                       const PlayerController* player, const Weapon* weapon,
                       const CharacterLibrary* characters,
                       const CharacterInstance* body,
                       const CharacterVisualState* body_state,
                       int draw_body,
                       const CombatantBodies* combatant_bodies,
                       float fov_degrees) {
    const GLfloat light_position[] = {4, 7, 5, 1};
    const GLfloat ambient[] = {.24f, .24f, .29f, 1};
    const GLfloat diffuse[] = {.92f, .90f, .82f, 1};

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    set_perspective(fov_degrees, g_aspect_ratio, .1f, 300);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    /* The angles the world is drawn through, which are the player's own in
       every view but the one in front of them - see third_person_camera. */
    float view_yaw, view_pitch, view_roll;
    third_person_camera_view_angles(camera, &view_yaw, &view_pitch, &view_roll);
    glTranslatef(0, 0, -camera->distance);
    glTranslatef(-camera->lean_offset, 0, 0);
    glRotatef(-view_roll, 0, 0, 1);
    glRotatef(-view_pitch, 1, 0, 0);
    glRotatef(-view_yaw, 0, 1, 0);
    glTranslatef(-player->x, -(player->y + camera->target_height), -player->z);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);

    draw_grid();
    draw_walls();
    draw_grapple(player);
    weapon_renderer_draw_effects(weapon);
    /* Every body but the player's own, and unlike that one these are drawn in
       both views: what makes the player's body conditional is that it is where
       the camera is, not that it is a body. */
    if (combatant_bodies)
        combatant_bodies_draw(combatant_bodies, characters);
    if (draw_body)
        character_renderer_draw(characters, body, body_state);
}

/* Stencils out the optic's glass and resets the depth inside it, so the
   magnified pass lands there and nowhere else.

   The depth has to be reset by drawing rather than by clearing: glClear ignores
   the stencil test, and the wide pass has already left depths in the disc that
   would wrongly occlude the narrow one. Writing the disc at the far plane with
   GL_ALWAYS does both jobs in the one primitive. */
static void mask_scope_disc(float center_x, float center_y, float radius) {
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_ALWAYS);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, g_viewport_width, g_viewport_height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(center_x, center_y, 1.0f);
    for (int i = 0; i <= SCOPE_DISC_SEGMENTS; ++i) {
        float angle = 2.0f * (float)MATH_PI * (float)i / SCOPE_DISC_SEGMENTS;
        glVertex3f(center_x + cosf(angle) * radius,
                   center_y + sinf(angle) * radius, 1.0f);
    }
    glEnd();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);

    /* Everything from here lands inside the disc only. */
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
}

/* Projects a point in front of the camera onto the screen, through the
   viewmodel's own projection - which is the one the optic is drawn with, and so
   the one that decides where on screen its glass actually is. */
static void project_viewmodel_point(float x, float y, float z, float fov_degrees,
                                    float* screen_x, float* screen_y) {
    float depth = fmaxf(0.02f, -z);
    float tangent = tanf(fov_degrees * 0.5f * MATH_PI / 180.0f);
    *screen_x = (0.5f + 0.5f * (x / depth) / (tangent * g_aspect_ratio)) *
                (float)g_viewport_width;
    *screen_y = (0.5f - 0.5f * (y / depth) / tangent) * (float)g_viewport_height;
}

/* Non-zero when the magnified pass ran, along with where it was drawn. */
static int scope_disc(const ThirdPersonCamera* camera, const Weapon* weapon,
                      const PlayerController* player,
                      float* center_x, float* center_y, float* radius) {
    /* The gun is only drawn in first person, and a magnified circle with no
       optic under it is just a hole in the screen. */
    if (!camera->is_first_person) return 0;
    if (!renderer3d_supports_scope_pass()) return 0;
    if (!weapon_sight_is_magnified(weapon)) return 0;
    /* Below this the optic is still down by the player's hip and a magnified
       circle floating over it reads as an artefact rather than as a sight. */
    if (weapon->ads < 0.10f) return 0;

    float center[3];
    weapon_sight_center(weapon, player, &center[0], &center[1], &center[2]);
    /* Through the same neck the viewmodel is drawn through, so the stencil
       window stays on the physical optic - the cant of a lean, and the pitch of
       a body the eye has turned away from. Asked of the module rather than
       recomposed here: this used to be a hand-rolled cos/sin pair, one of three
       copies of the lean, and the failure a fourth copy buys is a magnified
       circle sliding off the glass it is a window into.

       The neck is nearly always zero by the time this runs - it fades out with
       the aim, and nothing below ads 0.10 draws a scope at all - which is the
       argument for taking it through the same function rather than for leaving
       it out. */
    float view_pitch;
    third_person_camera_view_angles(camera, NULL, &view_pitch, NULL);
    ViewmodelNeck neck;
    viewmodel_neck_build(&neck, weapon->spec, view_pitch, player->lean,
                         weapon->ads);
    viewmodel_neck_transform_point(&neck, center, center);
    float sx = center[0], sy = center[1], sz = center[2];
    float view_fov = lerpf(weapon->spec->view_fov, weapon_spec_view_ads_fov(weapon->spec),
                           weapon->ads);
    project_viewmodel_point(sx, sy, sz, view_fov, center_x, center_y);
    *radius = weapon_sight_window_fraction(weapon) *
              (float)g_viewport_height * 0.5f;
    return *radius > 1.0f;
}

/* Where the camera actually is, which is the eye in first person and a point
   back along the view axis in third. Only the culling of other bodies wants
   this - the view matrix expresses the orbit as a translation instead - and it
   leaves out the lean's six centimetres on purpose, since nothing decided by a
   150-metre range test turns on them. */
static void camera_position(const ThirdPersonCamera* camera,
                            const PlayerController* player,
                            float eye[3], float look[3]) {
    float view_yaw, view_pitch, view_roll;
    third_person_camera_view_angles(camera, &view_yaw, &view_pitch, &view_roll);
    traversal_aim_direction(view_yaw, view_pitch, &look[0], &look[1], &look[2]);
    eye[0] = player->x - look[0] * camera->distance;
    eye[1] = player->y + camera->target_height - look[1] * camera->distance;
    eye[2] = player->z - look[2] * camera->distance;
}

void renderer3d_draw(const ThirdPersonCamera* camera, const PlayerController* player,
                     const Weapon* weapon, CharacterLibrary* characters,
                     CharacterInstance* body, const CharacterVisualState* body_state,
                     const Combatant* player_body,
                     const CombatantSet* combatants,
                     CombatantBodies* combatant_bodies,
                     float delta_time) {
    /* Third person, and standing. The second half is the same rule
       combatant_bodies_update applies to everybody else: the rig has no death
       clip, so a body that is down is not drawn rather than drawn at attention.
       It also decides the skinning, because a body nobody can see is a body not
       worth skinning - the playhead keeps running either way, which is what
       stops a pose from popping when the player stands back up. */
    int draw_body = !camera->is_first_person &&
                    (!player_body || player_body->alive);
    /* Before any pass, and exactly once. draw_world can run twice, and while
       it never draws the player's body in both - a scope is first person and
       that body is third - the rule this depends on is that skinning and the
       playhead are frame work, not draw work, whatever the passes do. The other
       bodies ARE drawn in both passes, which makes the rule load-bearing for
       them rather than merely correct: skinned inside the draw they would
       advance their clocks twice on exactly the frames a scope was up. */
    character_renderer_update(characters, body, body_state, delta_time,
                              draw_body);
    /* The body's turn, handed to the viewmodel's neck. Here because this is the
       line that just advanced it, and ONCE because three consumers read it and a
       second opinion is the defect viewmodel_neck.h exists to prevent. The pose
       is composed whatever `draw_body` says - see character_renderer.c - so this
       is live in first person, which is the only place it is used. */
    if (body) viewmodel_neck_set_look_yaw(
        character_animation_look_yaw(&body->animation));
    if (combatants && combatant_bodies) {
        float eye[3], look[3];
        camera_position(camera, player, eye, look);
        combatant_bodies_update(combatant_bodies, characters, combatants,
                                eye[0], eye[1], eye[2],
                                look[0], look[1], look[2], delta_time);
    }

    /* The UI pass leaves blending and texturing on, so the 3D pass restates
       what it needs instead of inheriting it. */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_STENCIL_TEST);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    /* The world around the optic, at the field of view the player would have
       without it. This is what makes a scope a window rather than a blindfold. */
    draw_world(camera, player, weapon, characters, body, body_state, draw_body,
               combatant_bodies, camera->fov_degrees);

    float center_x, center_y, radius;
    if (scope_disc(camera, weapon, player, &center_x, &center_y, &radius)) {
        mask_scope_disc(center_x, center_y, radius);
        draw_world(camera, player, weapon, characters, body, body_state, draw_body,
                   combatant_bodies, camera->scope_fov_degrees);
        glDisable(GL_STENCIL_TEST);
        /* glClear obeys the stencil write mask, so leaving it at zero would
           make next frame's stencil clear a no-op and strand this disc. */
        glStencilMask(0xFF);
    }

    /* Last, because it clears depth to sit in front of everything above. */
    /* The arms are cut out of the body's own rig, so they are handed the same
       library and the same rig index the body wears - one import, two views of
       it, and no chance of the hands and the third-person body disagreeing
       about what the player's arms look like. */
    weapon_renderer_draw_viewmodel(weapon, player, camera, g_aspect_ratio,
                                   characters, body ? body->rig
                                                    : CHARACTER_RIG_NONE);
}

/* The lazily loaded assets, given back. Everything here was loaded on the
   first frame that needed it rather than at startup, so this is the only place
   that knows they exist at all - and it runs while the context is still
   current, which freeing GL names requires. */
/* The lazily loaded assets this file owns, given back. The character rigs are
   not among them: those belong to the caller's library, which outlives the
   renderer and is freed alongside it. */
void renderer3d_shutdown(void) {
    weapon_model_shutdown();
    viewmodel_arms_shutdown();
}
