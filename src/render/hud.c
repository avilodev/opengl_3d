#include "render/hud.h"
#include "render/renderer.h"
#include "gameplay/combat_tuning.h"
#include "gameplay/weapon_tuning.h"
#include "utils/math_utils.h"

#include <math.h>
#include <stdio.h>

/* Authored at 720p and scaled the same way the pause menu is. */
#define HUD_TICK_LENGTH 7.0f
#define HUD_TICK_THICKNESS 2.0f
#define HUD_MIN_GAP 3.0f
#define HUD_DOT_SIZE 2.0f
#define HUD_AMMO_MARGIN 42.0f
#define HUD_RING_SEGMENTS 40
#define HUD_ACOG_SEGMENTS 72
#define HUD_RING_THICKNESS 1.4f

/* The hit marker: four short bars on the diagonals, out past the crosshair's
   own ticks so the two never sit on top of each other. */
#define HUD_MARKER_INNER 6.0f
#define HUD_MARKER_LENGTH 7.0f
#define HUD_MARKER_THICKNESS 2.0f

/* The pool bar over a body that is not at full health. Small: it is there to
   say that the thing being shot has a pool and roughly where in it the player
   is, and a bar big enough to read precisely would be a bar big enough to hide
   the target behind. */
#define HUD_HEALTH_BAR_WIDTH 48.0f
#define HUD_HEALTH_BAR_HEIGHT 4.0f
#define HUD_HEALTH_BAR_LIFT 0.42f
#define HUD_HEALTH_BAR_RANGE 90.0f

/* The player's own pool, opposite the ammo counter. Wider and taller than the
   bar over a body because it is read at a glance from the corner of the eye
   rather than aimed at, and it is drawn at full health as well as below it -
   the bar over a target disappears when there is nothing to say, but a player
   who cannot find their own health bar has no way to know whether it is missing
   or full. */
#define HUD_PLAYER_BAR_WIDTH 240.0f
#define HUD_PLAYER_BAR_HEIGHT 14.0f
/* Where the colour turns. Below this the bar goes red, which is the whole of
   the warning: a number nobody has time to read during a fight, and a colour
   they do not have to. */
#define HUD_PLAYER_BAR_LOW 0.35f

/* The magnified sight picture. The window's size is not a number here - it
   comes from weapon_sight_window_fraction, so it lands on the rim of the bore
   the renderer actually drew. */
#define HUD_ACOG_RIM 5.0f
/* How far down the world outside the glass is taken. The renderer draws it at
   the player's own field of view, so it stays readable - this is a hint about
   where to look, not a blindfold. */
#define HUD_ACOG_SHADE 0.42f
#define HUD_ACOG_CHEVRON_WIDTH 15.0f
#define HUD_ACOG_CHEVRON_HEIGHT 11.0f
#define HUD_ACOG_STADIA_LENGTH 74.0f
#define HUD_ACOG_TICK_WIDTH 9.0f

/* Half the vertical FOV subtends half the viewport, so an angle maps to pixels
   through the ratio of their tangents. This is why the crosshair cannot drift
   out of agreement with the bullet: both are reading bloom_degrees. */
static float degrees_to_pixels(float degrees, float fov_degrees,
                               int viewport_height) {
    float half_fov = tanf(fov_degrees * 0.5f * MATH_DEG_TO_RAD);
    if (half_fov < 0.0001f) return 0.0f;
    return tanf(degrees * MATH_DEG_TO_RAD) / half_fov *
           ((float)viewport_height * 0.5f);
}

/* A ring of short tangential segments. The renderer draws filled discs and
   axis-aligned rectangles; a rotated rectangle per segment is the cheapest way
   to get an outline out of that without teaching it a new primitive. */
static void draw_ring(float center_x, float center_y, float radius,
                      float thickness, float r, float g, float b, float a) {
    /* Segment length is the chord plus a hair, so the ring closes up. */
    float segment = 2.0f * radius *
                    (float)MATH_PI / (float)HUD_RING_SEGMENTS * 1.25f;
    for (int i = 0; i < HUD_RING_SEGMENTS; ++i) {
        float angle = 2.0f * (float)MATH_PI * (float)i / (float)HUD_RING_SEGMENTS;
        renderer_draw_rect_rotated(center_x + cosf(angle) * radius,
                                   center_y + sinf(angle) * radius,
                                   segment, thickness,
                                   angle + (float)MATH_PI * 0.5f, r, g, b, a);
    }
}

/* Everything outside the optic's glass, shaded rather than blacked out. The
   world around a scope is still worth seeing - it is where the player is about
   to move - so this only takes the periphery down enough that the eye settles
   on the magnified circle. Set HUD_ACOG_SHADE to 1.0 for an opaque surround.

   Built from the same tangential segments as draw_ring, with a radial thickness
   that runs past the corners of the screen, so the union is a filled annulus
   and the hole in the middle is the glass. The renderer has no annulus
   primitive and this does not need one.

   Segment length is taken from the OUTER radius: a rectangle has constant
   length, so covering the widest part covers everything inside it too. One
   pass, because overlapping translucent segments would band where they cross. */
static void draw_periphery_shade(float center_x, float center_y, float window,
                                 float reach, float alpha) {
    float thickness = reach * 2.0f;
    float radius = window + thickness * 0.5f;
    float segment = 2.0f * (window + thickness) *
                    tanf((float)MATH_PI / (float)HUD_ACOG_SEGMENTS) * 1.15f;
    for (int i = 0; i < HUD_ACOG_SEGMENTS; ++i) {
        float angle = 2.0f * (float)MATH_PI * (float)i /
                      (float)HUD_ACOG_SEGMENTS;
        renderer_draw_rect_rotated(center_x + cosf(angle) * radius,
                                   center_y + sinf(angle) * radius,
                                   segment, thickness,
                                   angle + (float)MATH_PI * 0.5f,
                                   0.012f, 0.014f, 0.020f, alpha);
    }
}

/* Chevron, stadia and range marks - the reticle of a 2.5x prism sight. The tip
   of the chevron is the aim point, so it sits exactly on the centre of the
   screen and everything else hangs below it. */
/* This one does NOT yet ride the gun's kick, and the crosshair and the
   holographic reticle both do. The reason is that its centre is doing two jobs:
   it is an aim point, and it is the rim of the window, which has to land on the
   disc renderer3d's scope pass masks to. That disc now follows the gun's
   COSMETIC rotation, weapon_sight_center putting the glass through the pose the
   weapon is drawn at - but not its aim, which is the kick, and by design: the
   kick moves where the round goes and the mask is only a hole in the screen.
   Offsetting the whole thing would peel the rim off the seam it exists to
   cover. Splitting the two, so the marks ride the kick inside a rim that stays
   put, is the fix; no weapon carries this sight yet, so it is worth less than it
   costs today. */
static void draw_acog_reticle(const Weapon* weapon, float fov_degrees,
                              int viewport_width, int viewport_height,
                              float scale, float alpha) {
    const float center_x = (float)viewport_width * 0.5f;
    const float center_y = (float)viewport_height * 0.5f;
    const float window = weapon_sight_window_fraction(weapon) *
                         (float)viewport_height * 0.5f;
    const float reach = sqrtf((float)viewport_width * (float)viewport_width +
                              (float)viewport_height * (float)viewport_height);

    draw_periphery_shade(center_x, center_y, window, reach,
                         alpha * HUD_ACOG_SHADE);

    /* A dark rim on the glass. It is not decoration: the magnified pass is
       masked to a 96-sided disc, and this is what covers that edge so the seam
       between the two views does not read as a jagged circle. */
    draw_ring(center_x, center_y, window + HUD_ACOG_RIM * scale * 0.5f,
              HUD_ACOG_RIM * scale * 1.6f, 0.05f, 0.055f, 0.07f, alpha);
    draw_ring(center_x, center_y, window + HUD_ACOG_RIM * scale * 1.4f,
              HUD_ACOG_RIM * scale * 0.5f, 0.24f, 0.25f, 0.28f, alpha * 0.8f);

    /* The cone, only once sustained fire has actually opened it. A weapon with
       no gun half has no cone, so there is nothing to open and nothing to
       draw. */
    const FirearmSpec* gun = weapon_spec_firearm(weapon->spec);
    float opened = gun ? clampf((weapon->bloom_degrees - gun->bloom_min) /
                                (gun->bloom_max - gun->bloom_min), 0.0f, 1.0f)
                       : 0.0f;
    if (opened > 0.02f) {
        float cone = degrees_to_pixels(weapon_cone_degrees(weapon), fov_degrees,
                                       viewport_height);
        draw_ring(center_x, center_y, cone,
                  fmaxf(1.0f, HUD_RING_THICKNESS * scale),
                  0.95f, 0.30f, 0.24f, alpha * opened * 0.34f);
    }

    float dim = weapon->is_reloading ? 0.5f : 1.0f;
    const float half_w = HUD_ACOG_CHEVRON_WIDTH * scale * 0.5f;
    const float rise = HUD_ACOG_CHEVRON_HEIGHT * scale;
    const float thin = fmaxf(1.0f, 1.7f * scale);
    const float arm = sqrtf(half_w * half_w + rise * rise);

    /* Two bars meeting at the aim point. The angle is whatever the arm's rise
       over its run works out to, so the chevron keeps its shape at any scale. */
    float lean = atan2f(rise, half_w);
    renderer_draw_rect_rotated(center_x - half_w * 0.5f,
                               center_y + rise * 0.5f, arm, thin,
                               -lean, 0.94f, 0.24f, 0.19f, alpha * dim);
    renderer_draw_rect_rotated(center_x + half_w * 0.5f,
                               center_y + rise * 0.5f, arm, thin,
                               lean, 0.94f, 0.24f, 0.19f, alpha * dim);

    /* The stadia below it, with range marks. The numbers are the reference
       optic's: further down the post is further away. */
    float stadia = HUD_ACOG_STADIA_LENGTH * scale;
    renderer_draw_rect(center_x - thin * 0.5f, center_y + rise,
                       thin, stadia, 0.10f, 0.10f, 0.12f, alpha * dim * 0.9f);
    const float marks[2] = {0.42f, 0.78f};
    const char* labels[2] = {"4", "6"};
    for (int i = 0; i < 2; ++i) {
        float y = center_y + rise + stadia * marks[i];
        float tick = HUD_ACOG_TICK_WIDTH * scale * (i == 0 ? 1.0f : 0.72f);
        renderer_draw_rect(center_x - tick * 0.5f, y - thin * 0.5f,
                           tick, thin, 0.10f, 0.10f, 0.12f, alpha * dim * 0.9f);
        renderer_draw_text(center_x + tick * 0.5f + 5.0f * scale,
                           y + 5.0f * scale, labels[i]);
    }
}

/* The holographic reticle is not drawn here. It is a collimated mark fixed in
   angle to the sight body, so it belongs on the gun: weapon_renderer draws it
   in the viewmodel pass, under the gun's own transform, seen through the
   optic's glass. Painted at the centre of the screen from here it appeared the
   instant the player touched the aim button, floating in front of a sight that
   was still down by the hip - an overlay switching on rather than an eye
   getting behind glass. It also could not follow the gun, which is fatal once
   the gun rotates: the reticle would have gone on claiming the round lands at
   the middle of the screen while the muzzle said otherwise. */

/* The crosshair sits where the GUN is pointing, which is the middle of the
   screen only while the gun is not carrying any kick.

   It has to. The round leaves along the gun's own angles now, so a crosshair
   pinned to the centre would go on claiming the shot lands there while the
   muzzle had climbed off it - the same lie the old centre-screen reticle told,
   in the one place the player reads at the hip. Both marks are composed from
   gun_pitch_degrees and gun_yaw_degrees, so a burst walks them up together and
   what climbs on screen is the gun rather than the view.

   Positive yaw takes the aim toward -x and positive pitch takes it up, while
   this projection has y increasing downward, so both offsets are negative. */
static void gun_offset_pixels(const Weapon* weapon, float fov_degrees,
                              int viewport_height, float* x, float* y) {
    *x = -degrees_to_pixels(weapon->gun_yaw_degrees, fov_degrees,
                            viewport_height);
    *y = -degrees_to_pixels(weapon->gun_pitch_degrees, fov_degrees,
                            viewport_height);
}

static void draw_crosshair(const Weapon* weapon, float fov_degrees,
                           float scope_fov_degrees, int show_sight,
                           int viewport_width, int viewport_height, float scale) {
    float offset_x, offset_y;
    gun_offset_pixels(weapon, fov_degrees, viewport_height,
                      &offset_x, &offset_y);
    const float center_x = (float)viewport_width * 0.5f + offset_x;
    const float center_y = (float)viewport_height * 0.5f + offset_y;
    const float length = HUD_TICK_LENGTH * scale;
    const float thickness = HUD_TICK_THICKNESS * scale;
    /* The cone the bullet will use, not the raw bloom - through the scope the
       two differ, and the crosshair has to draw the one that is true. */
    float cone = weapon_cone_degrees(weapon);
    float gap = fmaxf(HUD_MIN_GAP * scale,
                      degrees_to_pixels(cone, fov_degrees, viewport_height));

    /* Dimmer as it opens, so a spraying player can see at a glance that the
       weapon has stopped being precise without having to read the gap. */
    const FirearmSpec* gun = weapon_spec_firearm(weapon->spec);
    float tightness = gun ? 1.0f - clampf((weapon->bloom_degrees -
                                           gun->bloom_min) /
                                          (gun->bloom_max - gun->bloom_min),
                                          0.0f, 1.0f)
                          : 1.0f;
    float alpha = weapon->is_reloading ? 0.28f : 0.55f + tightness * 0.35f;
    /* The ticks hand over to the reticle as the scope comes up. */
    alpha *= show_sight ? 1.0f - weapon->ads : 1.0f;
    float red = weapon->ammo_in_magazine > 0 ? 0.92f : 0.95f;
    float green = weapon->ammo_in_magazine > 0 ? 0.96f : 0.42f;
    float blue = weapon->ammo_in_magazine > 0 ? 1.0f : 0.36f;

    if (alpha > 0.01f) {
        renderer_draw_rect(center_x - gap - length, center_y - thickness * 0.5f,
                           length, thickness, red, green, blue, alpha);
        renderer_draw_rect(center_x + gap, center_y - thickness * 0.5f,
                           length, thickness, red, green, blue, alpha);
        renderer_draw_rect(center_x - thickness * 0.5f, center_y - gap - length,
                           thickness, length, red, green, blue, alpha);
        renderer_draw_rect(center_x - thickness * 0.5f, center_y + gap,
                           thickness, length, red, green, blue, alpha);

        /* The centre dot never moves, so there is always something to aim with
           when the cone has opened wider than the ticks are long. */
        float dot = HUD_DOT_SIZE * scale;
        renderer_draw_rect(center_x - dot * 0.5f, center_y - dot * 0.5f, dot, dot,
                           red, green, blue, alpha * 0.8f);
    }

    /* Which sight picture comes up is the weapon's, not the HUD's, decision -
       and for a holographic sight the answer is none of the HUD's business at
       all, because its reticle is drawn on the gun. What is left here is the
       chevron, which is painted against a magnified second pass of the world
       rather than against the viewmodel, so the optic's own field of view is
       what turns the cone into pixels for it. */
    if (weapon->ads > 0.01f && show_sight && gun && gun->sight &&
        gun->sight->reticle == WEAPON_RETICLE_CHEVRON)
        draw_acog_reticle(weapon, scope_fov_degrees, viewport_width,
                          viewport_height, scale, weapon->ads);
}

/* --- what the player is told about what they hit ---

   All three of these are anchored in the WORLD and drawn on the screen, which
   is the whole reason they are here rather than in the 3D pass: text and thin
   bars want the UI's batched, unlit, screen-projected path, and putting them
   through the world's projection would mean billboarding quads and baking a
   font into a texture the 3D renderer does not otherwise own.

   What makes that honest is that the position still comes from the world -
   third_person_camera_project, the same transform renderer3d draws through - so
   a figure stays on the shoulder it came off through a wall run rather than
   sliding around the screen. */

/* How large a world-anchored mark is drawn, given how far away it is. Straight
   inverse distance is correct and unreadable: a figure at forty metres is a
   pixel high and one at two metres fills the screen. Clamped, it falls off
   enough to say "that one was further away" and never enough to be unreadable. */
static float distance_scale(float depth) {
    if (depth < 0.01f) depth = 0.01f;
    return clampf(DAMAGE_NUMBER_REFERENCE / depth,
                  DAMAGE_NUMBER_SCALE_MIN, DAMAGE_NUMBER_SCALE_MAX);
}

/* Which band the figure came from, as a colour. The head is the only one that
   changes hue: a player who is shown a different colour for every band learns
   three colours, and a player who is shown one for the shot worth crossing a
   room for learns the thing that matters. */
static void region_colour(int region, float* r, float* g, float* b) {
    if (region == COMBATANT_REGION_HEAD) {
        *r = 1.0f; *g = 0.82f; *b = 0.30f;
    } else if (region == COMBATANT_REGION_LEGS) {
        *r = 0.80f; *g = 0.84f; *b = 0.88f;
    } else {
        *r = 1.0f; *g = 0.97f; *b = 0.94f;
    }
}

static void draw_damage_numbers(const CombatantSet* combatants,
                                const ThirdPersonCamera* camera,
                                const PlayerController* player,
                                int viewport_width, int viewport_height,
                                float scale) {
    float aspect = (float)viewport_width / (float)viewport_height;
    for (int i = 0; i < DAMAGE_NUMBER_MAX; ++i) {
        const DamageNumber* number = &combatants->numbers.items[i];
        if (number->life <= 0.0f) continue;

        float screen_x, screen_y, depth;
        if (!third_person_camera_project(camera, player, number->x, number->y,
                                         number->z, aspect,
                                         &screen_x, &screen_y, &depth))
            continue;

        /* Held at full and then faded over the last third, so the figure is
           legible for as long as it is worth reading and does not spend its
           whole life dissolving. */
        float alpha = clampf(number->life / (DAMAGE_NUMBER_LIFETIME * 0.34f),
                             0.0f, 1.0f);
        float size = DAMAGE_NUMBER_SCALE * scale * distance_scale(depth) *
                     (1.0f + DAMAGE_NUMBER_POP * damage_number_pop_blend(number));

        char text[16];
        /* Rounded rather than truncated, so a band multiplier that lands on a
           half never reads as a point of damage the player did not do. */
        snprintf(text, sizeof text, "%d", (int)(number->amount + 0.5f));
        float width = renderer_text_width(text) * size;
        float x = screen_x * (float)viewport_width - width * 0.5f;
        float y = screen_y * (float)viewport_height;

        float r, g, b;
        region_colour(number->region, &r, &g, &b);
        /* A dark copy one pixel down and across. The figures are drawn over
           whatever the body happens to be standing in front of, and against a
           pale wall a white number on its own is a smear. */
        renderer_draw_text_tinted(x + scale, y + scale, size, text,
                                  0.04f, 0.03f, 0.05f, alpha * 0.75f);
        renderer_draw_text_tinted(x, y, size, text, r, g, b, alpha);
    }
}

/* A pool bar over anything that has been hit and has not finished coming back.
   It is not drawn at full health, which is what makes it a readout of a fight
   rather than a permanent label on every body in the arena. */
static void draw_health_bars(const CombatantSet* combatants,
                             const ThirdPersonCamera* camera,
                             const PlayerController* player,
                             int viewport_width, int viewport_height,
                             float scale) {
    float aspect = (float)viewport_width / (float)viewport_height;
    for (int i = 0; i < combatants->count; ++i) {
        const Combatant* combatant = &combatants->items[i];
        if (!combatant->active || !combatant->alive) continue;
        float fraction = combatant_health_fraction(combatant);
        if (fraction >= 0.999f) continue;

        float screen_x, screen_y, depth;
        if (!third_person_camera_project(camera, player, combatant->x,
                                         combatant->y + COMBAT_BODY_HEIGHT +
                                         HUD_HEALTH_BAR_LIFT,
                                         combatant->z, aspect,
                                         &screen_x, &screen_y, &depth))
            continue;
        if (depth > HUD_HEALTH_BAR_RANGE) continue;

        float size = scale * distance_scale(depth);
        float width = HUD_HEALTH_BAR_WIDTH * size;
        float height = fmaxf(1.0f, HUD_HEALTH_BAR_HEIGHT * size);
        float x = screen_x * (float)viewport_width - width * 0.5f;
        float y = screen_y * (float)viewport_height - height * 0.5f;
        /* Faded out at the far end of the range rather than switched off, so a
           body does not acquire a bar the instant a player runs toward it. */
        float alpha = clampf((HUD_HEALTH_BAR_RANGE - depth) / 20.0f, 0.0f, 1.0f);

        renderer_draw_rect(x - size, y - size, width + size * 2.0f,
                           height + size * 2.0f, 0.03f, 0.03f, 0.05f,
                           alpha * 0.62f);
        renderer_draw_rect(x, y, width, height, 0.16f, 0.17f, 0.21f,
                           alpha * 0.85f);
        renderer_draw_rect(x, y, width * fraction, height,
                           0.92f, 0.28f, 0.24f, alpha);
    }
}

/* Four diagonal bars around the aim point for a beat after a round lands. It is
   derived from the newest damage figure rather than from a timer of its own: a
   round landing IS a figure being thrown, so one event drives both and they
   cannot disagree about whether the player hit anything.

   It sits on the GUN's aim and not on the middle of the screen, for the same
   reason the crosshair does - a confirmation that has drifted off the mark it
   is confirming is worse than none. */
static void draw_hit_marker(const Weapon* weapon, const CombatantSet* combatants,
                            float fov_degrees, int viewport_width,
                            int viewport_height, float scale) {
    float age;
    int region;
    if (!damage_numbers_latest(&combatants->numbers, &age, &region)) return;
    if (age > COMBAT_HIT_MARKER_TIME) return;

    float offset_x, offset_y;
    gun_offset_pixels(weapon, fov_degrees, viewport_height, &offset_x, &offset_y);
    float center_x = (float)viewport_width * 0.5f + offset_x;
    float center_y = (float)viewport_height * 0.5f + offset_y;

    float fade = 1.0f - age / COMBAT_HIT_MARKER_TIME;
    float r, g, b;
    region_colour(region, &r, &g, &b);
    float length = HUD_MARKER_LENGTH * scale;
    float thickness = fmaxf(1.0f, HUD_MARKER_THICKNESS * scale);
    /* Spread outward as it fades, which is what makes it read as a strike
       rather than as a mark switching on and off. */
    float inner = (HUD_MARKER_INNER + (1.0f - fade) * 3.0f) * scale;
    float reach = inner + length * 0.5f;
    const float diagonal = 0.70710678f;

    for (int i = 0; i < 4; ++i) {
        float sx = (i & 1) ? 1.0f : -1.0f;
        float sy = (i & 2) ? 1.0f : -1.0f;
        renderer_draw_rect_rotated(center_x + sx * reach * diagonal,
                                   center_y + sy * reach * diagonal,
                                   length, thickness,
                                   sx * sy > 0.0f ? 0.7853982f : -0.7853982f,
                                   r, g, b, fade * 0.95f);
    }
}

/* The pool, the figure, and - while the player is down - what they are waiting
   for. All three off the one Combatant, so the bar and the count cannot
   disagree about whether the player is alive. */
static void draw_player_state(const Combatant* body, int viewport_width,
                              int viewport_height, float scale) {
    float margin = HUD_AMMO_MARGIN * scale;
    float width = HUD_PLAYER_BAR_WIDTH * scale;
    float height = HUD_PLAYER_BAR_HEIGHT * scale;
    float x = margin;
    float y = (float)viewport_height - margin - height;
    float fraction = combatant_health_fraction(body);
    /* A body that is down reads as empty however the pool got there, which is
       the only state where the bar and the health could otherwise disagree. */
    if (!body->alive) fraction = 0.0f;

    float edge = fmaxf(1.0f, 2.0f * scale);
    renderer_draw_rect(x - edge, y - edge, width + edge * 2.0f,
                       height + edge * 2.0f, 0.03f, 0.03f, 0.05f, 0.62f);
    renderer_draw_rect(x, y, width, height, 0.16f, 0.17f, 0.21f, 0.85f);
    if (fraction > 0.0f) {
        int low = fraction <= HUD_PLAYER_BAR_LOW;
        renderer_draw_rect(x, y, width * fraction, height,
                           low ? 0.92f : 0.42f, low ? 0.28f : 0.78f,
                           low ? 0.24f : 0.52f, 0.95f);
    }

    char text[32];
    snprintf(text, sizeof text, "%d", (int)(fraction * body->max_health + 0.5f));
    renderer_draw_text_centered(x, y - height * 2.2f, width, height * 2.0f, text);

    if (body->alive) return;

    /* Rounded UP, so the last second of the count is shown as one rather than
       as a zero the player then sits through. */
    int seconds = (int)ceilf(fmaxf(0.0f, body->respawn_timer));
    snprintf(text, sizeof text, "DOWN - RESPAWN IN %d", seconds);
    float notice_width = 420.0f * scale;
    float notice_height = 40.0f * scale;
    renderer_draw_text_centered(((float)viewport_width - notice_width) * 0.5f,
                                (float)viewport_height * 0.5f + 90.0f * scale,
                                notice_width, notice_height, text);
}

static void draw_ammo(const Weapon* weapon, int viewport_width,
                      int viewport_height, float scale) {
    /* A weapon with no magazine has no count to give, and an ammo readout on a
       sword would be a number the player cannot spend. */
    const FirearmSpec* gun = weapon_spec_firearm(weapon->spec);
    if (!gun) return;

    char text[32];
    if (weapon->is_reloading)
        snprintf(text, sizeof text, "RELOADING");
    else
        snprintf(text, sizeof text, "%d / %d", weapon->ammo_in_magazine,
                 gun->magazine);

    float margin = HUD_AMMO_MARGIN * scale;
    float width = 220.0f * scale;
    float height = 34.0f * scale;
    renderer_draw_text_centered((float)viewport_width - margin - width,
                                (float)viewport_height - margin - height,
                                width, height, text);
}

void hud_draw(const Weapon* weapon, const ThirdPersonCamera* camera,
              const PlayerController* player, const CombatantSet* combatants,
              const Combatant* player_body,
              int viewport_width, int viewport_height) {
    float scale = renderer_ui_scale(viewport_height);
    float fov_degrees = camera->fov_degrees;
    /* Third person draws no gun, so there is no optic for a sight picture to
       belong to; the plain crosshair carries the cone on its own. */
    float scope_fov_degrees = camera->is_first_person
        ? camera->scope_fov_degrees : camera->fov_degrees;
    renderer_begin_frame();
    renderer_begin_layer(RENDER_LAYER_UI);
    renderer_set_screen_projection(viewport_width, viewport_height);
    /* The world-anchored marks first, so the crosshair and the hit marker are
       drawn over them rather than under a figure that happened to fly through
       the middle of the screen. */
    if (combatants && player) {
        draw_health_bars(combatants, camera, player, viewport_width,
                         viewport_height, scale);
        draw_damage_numbers(combatants, camera, player, viewport_width,
                            viewport_height, scale);
    }
    draw_crosshair(weapon, fov_degrees, scope_fov_degrees,
                   camera->is_first_person,
                   viewport_width, viewport_height, scale);
    if (combatants)
        draw_hit_marker(weapon, combatants, fov_degrees, viewport_width,
                        viewport_height, scale);
    draw_ammo(weapon, viewport_width, viewport_height, scale);
    if (player_body)
        draw_player_state(player_body, viewport_width, viewport_height, scale);
    renderer_end_frame();
}
