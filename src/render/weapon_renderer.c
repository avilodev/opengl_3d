#include "render/weapon_renderer.h"
#include "render/primitives.h"
#include "render/weapon_model.h"
#include "render/viewmodel_arms.h"
#include "render/viewmodel_clip_arms.h"
#include "render/arm_placement.h"
#include "render/character_library.h"
#include "gameplay/weapon_tuning.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <GLFW/glfw3.h>
#include <math.h>
#include <string.h>

/* Everything here fades on `life`, which the weapon owns. The renderer reads
   that state and never writes to it. */

static void draw_tracers(const Weapon* weapon) {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < WEAPON_TRACER_COUNT; ++i) {
        const WeaponTracer* tracer = &weapon->tracers[i];
        if (tracer->life <= 0.0f) continue;
        float fade = tracer->life / WEAPON_TRACER_LIFETIME;
        /* Hot at the muzzle, cooling along the flight path, so the streak
           reads as leaving the gun rather than as a static line. */
        glColor4f(1.0f, 0.95f, 0.72f, fade);
        glVertex3f(tracer->start_x, tracer->start_y, tracer->start_z);
        glColor4f(1.0f, 0.66f, 0.30f, fade * 0.25f);
        glVertex3f(tracer->end_x, tracer->end_y, tracer->end_z);
    }
    glEnd();
    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* A square on the surface it hit, lifted a millimetre off it so the depth test
   does not fight the wall for the same pixels. */
static void draw_impacts(const Weapon* weapon) {
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    for (int i = 0; i < WEAPON_IMPACT_COUNT; ++i) {
        const WeaponImpact* impact = &weapon->impacts[i];
        if (impact->life <= 0.0f) continue;
        float fade = impact->life / WEAPON_IMPACT_LIFETIME;

        /* Two axes across the face. The normal is axis-aligned - every surface
           in the arena is - so a fixed pair of tangents per axis is exact. */
        float ux, uy, uz, vx, vy, vz;
        if (fabsf(impact->normal_y) > 0.5f) {
            ux = 1; uy = 0; uz = 0; vx = 0; vy = 0; vz = 1;
        } else if (fabsf(impact->normal_x) > 0.5f) {
            ux = 0; uy = 1; uz = 0; vx = 0; vy = 0; vz = 1;
        } else {
            ux = 1; uy = 0; uz = 0; vx = 0; vy = 1; vz = 0;
        }
        float size = WEAPON_IMPACT_SIZE * (0.55f + fade * 0.45f);
        float ox = impact->x + impact->normal_x * 0.01f;
        float oy = impact->y + impact->normal_y * 0.01f;
        float oz = impact->z + impact->normal_z * 0.01f;
        /* Concrete throws a hot spark; a body does not. The mark on a target is
           bigger, redder and dimmer, which reads at a distance the spark does
           not - and it is the one piece of hit feedback that appears where the
           player is already looking rather than at the middle of the screen. */
        if (impact->on_body) {
            size *= 1.35f;
            glColor4f(0.95f, 0.16f, 0.14f, fade * fade * 0.75f);
        } else {
            glColor4f(1.0f, 0.82f, 0.48f, fade * fade * 0.9f);
        }
        glVertex3f(ox - (ux + vx) * size, oy - (uy + vy) * size, oz - (uz + vz) * size);
        glVertex3f(ox + (ux - vx) * size, oy + (uy - vy) * size, oz + (uz - vz) * size);
        glVertex3f(ox + (ux + vx) * size, oy + (uy + vy) * size, oz + (uz + vz) * size);
        glVertex3f(ox - (ux - vx) * size, oy - (uy - vy) * size, oz - (uz - vz) * size);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
}

/* The spent magazines, in the world and through the world's own projection -
   the one part of a reload that is not drawn in front of the eye. Solid
   geometry rather than a blended quad like the impacts, so it takes the world's
   lighting and depth the way any other object does: it IS an object, and a
   magazine that glowed through a wall would be a decal pretending to be one. */
static void draw_dropped_mags(const Weapon* weapon) {
    const WeaponSpec* spec = weapon->spec;
    const FirearmSpec* firearm = weapon_spec_firearm(spec);
    /* A weapon with no gun half drops no magazines, and the ring it would read
       is all zeroes anyway - but the part name is reached for before anything
       is looked at, so the null has to be caught here. */
    if (!firearm || !firearm->magazine_part) return;
    /* Empty: a magazine on the floor is one somebody has finished with. */
    const char* parts[2] = {firearm->magazine_part, NULL};

    for (int i = 0; i < WEAPON_DROPPED_MAG_COUNT; ++i) {
        const WeaponDroppedMag* mag = &weapon->dropped_mags[i];
        if (mag->life <= 0.0f) continue;
        glPushMatrix();
        glTranslatef(mag->x, mag->y, mag->z);
        glRotatef(mag->spin, 0, 1, 0);
        glRotatef(mag->tilt, 1, 0, 0);
        /* The mesh is authored hanging BELOW the gun's origin, so it is lifted
           by its own half-depth to turn about its middle instead of swinging
           around a point above its mouth. */
        glTranslatef(0.0f, WEAPON_MAG_REST_HEIGHT, 0.0f);
        weapon_model_draw_only(spec, parts);
        glPopMatrix();
    }
}

void weapon_renderer_draw_effects(const Weapon* weapon) {
    draw_tracers(weapon);
    draw_impacts(weapon);
    draw_dropped_mags(weapon);
}

/* A magnified prism sight, built as a ring of boxes rather than a solid tube so
   the bore is genuinely open and the world shows through it. The HUD paints the
   sight picture - the black surround and the chevron - into that opening. */
#define ACOG_SEGMENTS 14

static void draw_acog(float height, float forward, float radius) {
    const float body = WEAPON_ACOG_DEPTH;
    const float wall = WEAPON_ACOG_WALL;
    /* Chord of one segment at the ring's mean radius, with enough overlap that
       the shell reads as solid from the outside. */
    const float chord = 2.0f * (radius + wall * 0.5f) *
                        sinf((float)MATH_PI / ACOG_SEGMENTS) * 1.35f;

    glPushMatrix();
    glTranslatef(0, height, -forward);
    for (int i = 0; i < ACOG_SEGMENTS; ++i) {
        float angle = 2.0f * (float)MATH_PI * (float)i / ACOG_SEGMENTS;
        /* The top of the housing catches the light and the underside sits in
           shadow. Cheap, but it is what makes a ring of boxes read as round. */
        float lit = 0.5f + 0.5f * sinf(angle);
        glColor3f(.13f + lit * .10f, .14f + lit * .10f, .17f + lit * .11f);
        glPushMatrix();
        glTranslatef(cosf(angle) * (radius + wall * 0.5f),
                     sinf(angle) * (radius + wall * 0.5f), 0);
        glRotatef(angle / MATH_DEG_TO_RAD, 0, 0, 1);
        primitives_draw_box(wall, chord, body);
        glPopMatrix();

        /* Raised bezels at both ends, the heavy rims of the reference optic. */
        glColor3f(.10f + lit * .08f, .11f + lit * .08f, .13f + lit * .09f);
        for (int end = 0; end < 2; ++end) {
            glPushMatrix();
            glTranslatef(cosf(angle) * (radius + wall),
                         sinf(angle) * (radius + wall),
                         end ? body * 0.5f : -body * 0.5f);
            glRotatef(angle / MATH_DEG_TO_RAD, 0, 0, 1);
            primitives_draw_box(wall * 1.6f, chord, .022f);
            glPopMatrix();
        }
    }
    glPopMatrix();

    /* The mount carrying it to the rail. It stops at the OUTSIDE of the tube
       wall rather than at the bore: a post that reached the bore would stand in
       the bottom of the sight picture, which is the one place on this gun where
       the player is entitled to see nothing but the world. */
    float mount = height - (radius + wall);
    glColor3f(.16f, .17f, .21f);
    glPushMatrix(); glTranslatef(0, mount * 0.5f, -forward);
    primitives_draw_box(.044f, mount, .10f); glPopMatrix();
    glColor3f(.24f, .25f, .30f);
    glPushMatrix(); glTranslatef(radius + wall * 1.6f, height - radius * .4f,
                                 -forward + .07f);
    primitives_draw_box(.028f, .015f, .046f); glPopMatrix();
}

/* The optic on the gun's rail, drawn about its reticle - which is the origin
   the mesh was authored around and the point the whole sight picture is built
   from. Aiming translates the gun by the same two numbers in the opposite
   direction, so the reticle lands on the middle of the screen and the HUD's
   dot lands on the reticle. There is one place those numbers live, and both
   readers of them are here. */
void weapon_renderer_draw_sight(const WeaponSpec* spec) {
    /* A weapon with no gun half has no rail and so no optic to draw. */
    const FirearmSpec* firearm = weapon_spec_firearm(spec);
    if (!firearm || !firearm->sight) return;
    /* The translate is this function's job and not the mesh's. A gun's mesh is
       authored about its bore and draws at the caller's transform untouched;
       an optic's is authored about its reticle, so drawing it untouched puts
       the reticle on the bore at the back of the receiver and the whole sight
       inside the gun, which is exactly as invisible as it sounds. */
    float height = weapon_spec_sight_height(spec);
    float forward = weapon_spec_sight_forward(spec);
    glPushMatrix();
    glTranslatef(0.0f, height, -forward);
    int drawn = weapon_model_draw_sight(firearm->sight);
    glPopMatrix();
    if (drawn) return;
    /* No mesh for this sight yet, so it is boxes. Only the magnified optic is
       ever in that state now, and it is the one shape boxes can still tell:
       a ring with a genuinely open bore. It places itself, which is why it is
       outside the transform above. */
    draw_acog(height, forward, firearm->sight->window_radius);
}


/* --- the holographic reticle ---

   A collimated mark: projected at optical infinity, so it is fixed in ANGLE to
   the sight body and does not shift when the sight merely moves. That is the
   whole property a holographic sight is sold on, and it is why the reticle is
   drawn here, in the viewmodel pass, under the gun's own transform - it rotates
   with the gun for free, including through whatever the gun's physics do to it
   later, and there is no second copy of the pose for it to drift from.

   Two placements were tried and are wrong. Painted at the centre of the screen
   by the HUD it appeared the moment the aim button went down, hanging in front
   of a sight that was still at the hip, and it could not follow the gun at all.
   Pasted on the glass as a quad it sits a third of a metre from the eye and
   parallaxes, so it agrees with the bullet only when the eye is perfectly on
   axis - which is the one thing a reticle may never do.

   Drawn far away instead, the aperture does the rest of the work: the reticle
   is masked to the optic's own glass, so it can only be seen where the window
   actually is. Coming up from the hip the window is off to the side and the
   reticle is simply not there; as the gun converges the glass sweeps across it
   and it emerges from behind the hood. No fade curve decides that. Geometry
   does, which is why it keeps working when the geometry changes. */

/* Far enough that the residual parallax over the gun's own travel is a
   hundredth of a degree, and inside the 8m far plane this pass sets up. */
#define RETICLE_DISTANCE 6.0f
/* Angular sizes, so the mark covers the same amount of target at any distance
   the line above happens to be.

   This is the EXPS2's own reticle and not a generic dot: a wide circle with a
   small dot centred in it. That shape is the whole visible difference between a
   holographic sight and a reflex red dot, and it is not decoration - the circle
   is a ranging and snap-shooting aid.

   The sizes are exaggerated, and deliberately. The real reticle is a 68 minute
   circle - 1.13 degrees across, a finger's width at arm's length - around a one
   minute dot. Drawn true, at the world's 75 degree field of view, that is a
   4.6 pixel circle of 1.2 pixel line at 720p and a dot a fifth of a pixel
   across: a smudge and nothing. The mark this replaced was already exaggerated
   the same way, at 19 minutes for a dot that is nominally one, so the choice
   here is not whether to exaggerate but whether to do it to the whole shape or
   only to part of it.

   The whole shape, at about twice true size, with the circle-to-dot ratio
   compressed from 68:1 to roughly 5:1 - because 68:1 cannot be drawn either: a
   dot big enough to see would put the circle a hundred and forty pixels out,
   off the sight picture entirely. What survives the compression is what makes
   the sight recognisable, a coarse circle around a fine dot, and that is the
   part worth keeping. */
#define RETICLE_DOT_DEGREES 0.20f
#define RETICLE_CIRCLE_DEGREES 1.10f
#define RETICLE_CIRCLE_WIDTH_DEGREES 0.13f
/* The soft bloom of an emitter, close around the dot. What makes the mark read
   as light on glass rather than as a painted pixel. */
#define RETICLE_GLOW_DEGREES 0.58f
/* Radial thickness of the cone ring, as an angle for the same reason. */
#define RETICLE_RING_DEGREES 0.17f
/* The circle wants more sides than the dot does: a coarse dot is still a dot,
   but a coarse circle is visibly a polygon. */
#define RETICLE_SEGMENTS 18
#define RETICLE_CIRCLE_SEGMENTS 64

/* The cone the next round will leave inside, as a faint ring. Invisible at rest
   and fading in only once sustained fire has actually opened it: aim carefully
   and it is a clean mark, hold the trigger and the sight tells you what that
   cost. Drawn at the same distance as the mark and in the same projection, so
   it is the true angle rather than a decoration sized to look right. */
static void draw_reticle_ring(float half_angle_degrees, float thickness,
                              int segments,
                              float r, float g, float b, float a) {
    float radius = RETICLE_DISTANCE * tanf(half_angle_degrees * MATH_DEG_TO_RAD);
    float inner = fmaxf(0.0f, radius - thickness * 0.5f);
    float outer = radius + thickness * 0.5f;
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * (float)MATH_PI * (float)i / segments;
        float ca = cosf(angle), sa = sinf(angle);
        glVertex3f(ca * inner, sa * inner, -RETICLE_DISTANCE);
        glVertex3f(ca * outer, sa * outer, -RETICLE_DISTANCE);
    }
    glEnd();
}

static void draw_reticle_disc(float half_angle_degrees,
                              float r, float g, float b, float a) {
    float radius = RETICLE_DISTANCE * tanf(half_angle_degrees * MATH_DEG_TO_RAD);
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, -RETICLE_DISTANCE);
    for (int i = 0; i <= RETICLE_SEGMENTS; ++i) {
        float angle = 2.0f * (float)MATH_PI * (float)i / RETICLE_SEGMENTS;
        glVertex3f(cosf(angle) * radius, sinf(angle) * radius,
                   -RETICLE_DISTANCE);
    }
    glEnd();
}

/* Called with the transform still at the gun, so -z is where the GUN points and
   not where the player is looking. Those differ by exactly the recoil the gun
   is carrying, which is the point: the reticle and the round are composed from
   the same two numbers.

   Done in two phases, and the split matters.

   The glass goes into the stencil first, in the viewmodel's own projection,
   because that is the projection the gun was drawn through and the mask has to
   land on the pixels the window actually covers. It is written with the depth
   test ON, so any part of the window the hood stands in front of fails depth
   and never makes it into the mask - the occlusion comes free, as part of the
   aperture, rather than needing a depth test the second phase could not do.

   The reticle itself is then drawn through the WORLD's projection. The gun is
   drawn through a narrower one - 50 degrees against the world's 75 - and an
   angle rendered through the wrong projection lands at the wrong place on
   screen: a one-degree kick drawn at 50 degrees sits about 1.66x further from
   the centre than the same degree of world. The mark would have agreed with the
   bullet only at zero kick, which is the one condition under which nobody is
   checking. Drawn at the world's field of view it agrees at every kick. */
static void draw_holographic_reticle(const Weapon* weapon, float world_fov,
                                     float aspect_ratio) {
    const WeaponSpec* spec = weapon->spec;
    const FirearmSpec* firearm = weapon_spec_firearm(spec);

    /* Colour and depth writes off: nothing of the glass should land on screen
       here, and a pane that wrote depth would occlude what it exists to
       reveal. */
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);

    glPushMatrix();
    glTranslatef(0.0f, weapon_spec_sight_height(spec),
                 -weapon_spec_sight_forward(spec));
    int has_glass = weapon_model_sight_glass(firearm->sight);
    glPopMatrix();

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x00);
    /* No mesh loaded, so no window to look through. The reticle is drawn
       unmasked rather than not at all: losing the reveal costs a look, losing
       the reticle is the gun not working. */
    glStencilFunc(has_glass ? GL_EQUAL : GL_ALWAYS, 1, 0xFF);

    /* The world's projection, and a modelview carrying only the gun's
       ROTATION. A collimated mark is fixed in angle and immune to the sight
       translating, so the gun's position is deliberately not in here - putting
       it in is what makes a reticle parallax. */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    double top = 0.02 * tan(world_fov * MATH_PI / 360.0);
    glFrustum(-top * aspect_ratio, top * aspect_ratio, -top, top, 0.02, 8.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glRotatef(weapon->gun_pitch_degrees, 1, 0, 0);
    glRotatef(weapon->gun_yaw_degrees, 0, 1, 0);
    /* The cant, so the mark follows the gun off-axis while the gun is still
       coming up. It unwinds to zero by the time the window reaches the eye, and
       until then the mask has the mark hidden anyway. */
    glRotatef(-3.0f * (1.0f - weapon->ads), 0, 1, 0);

    /* One approximation left, stated so it is not mistaken for exact: the
       camera's roll is not in here. The world pass carries it in its view
       matrix and this frame does not, so while the view is rolled - a wall run
       or a lean - the mark's offset from centre is off by the kick times the
       sine of the roll, a couple of hundredths of a degree at the angles
       involved. The HUD's crosshair makes the same approximation, so the two
       marks stay honest with each other; both are off against the world by the
       same small amount, and the viewmodel has never shared the world's roll
       either. Composing roll into both is the fix when it earns its keep.

       Depth cannot be tested across a projection change, and does not need to
       be: the mask was built with depth on, so it already excludes everything
       the hood covers. Additive, because this is emitted light. */
    glDisable(GL_DEPTH_TEST);
    /* Flat marks square to the eye, so there is no back face worth culling and
       no reason to depend on getting their winding right. */
    GLboolean was_culling = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    float dim = weapon->is_reloading ? 0.45f : 1.0f;

    float opened = clampf((weapon->bloom_degrees - firearm->bloom_min) /
                          (firearm->bloom_max - firearm->bloom_min), 0.0f, 1.0f);
    if (opened > 0.02f)
        draw_reticle_ring(weapon_cone_degrees(weapon),
                          RETICLE_DISTANCE *
                          tanf(RETICLE_RING_DEGREES * MATH_DEG_TO_RAD),
                          RETICLE_SEGMENTS * 2,
                          0.95f, 0.30f, 0.24f, dim * opened * 0.30f);

    /* The emitter's bloom, then the circle, then the dot. Reloading dims all
       three rather than hiding them, so the sight never blinks out mid-fight.

       The circle is dimmer than the dot on purpose. On the real optic it is the
       coarse mark and the dot is the fine one, and drawn at equal brightness the
       circle wins the eye simply by being bigger - which would make a precise
       sight feel like a wide one. */
    draw_reticle_disc(RETICLE_GLOW_DEGREES, 1.0f, 0.22f, 0.16f, dim * 0.18f);
    draw_reticle_ring(RETICLE_CIRCLE_DEGREES,
                      RETICLE_DISTANCE *
                      tanf(RETICLE_CIRCLE_WIDTH_DEGREES * MATH_DEG_TO_RAD),
                      RETICLE_CIRCLE_SEGMENTS,
                      1.0f, 0.26f, 0.20f, dim * 0.62f);
    draw_reticle_disc(RETICLE_DOT_DEGREES, 1.0f, 0.30f, 0.24f, dim);
    glDisable(GL_BLEND);
    if (was_culling) glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glDepthMask(GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    /* glClear obeys the write mask, so leaving it at zero would strand this
       frame's mask in the buffer - the same trap renderer3d's scope pass
       documents. */
    glStencilMask(0xFF);
    glEnable(GL_LIGHTING);
}

/* Compact, stubby, and all receiver: the sidearm. */
static void draw_body_auto_pistol(void) {
    glColor3f(.17f, .19f, .23f);
    glPushMatrix(); glTranslatef(0, 0, -0.16f);
    primitives_draw_box(.070f, .085f, .34f); glPopMatrix();

    glColor3f(.13f, .15f, .18f);
    glPushMatrix(); glTranslatef(0, -0.005f, -0.40f);
    primitives_draw_box(.048f, .046f, .30f); glPopMatrix();

    glColor3f(.21f, .23f, .27f);
    glPushMatrix(); glTranslatef(0, .052f, -0.24f);
    primitives_draw_box(.030f, .022f, .30f); glPopMatrix();

    glColor3f(.15f, .16f, .20f);
    glPushMatrix(); glTranslatef(0, -.085f, -0.14f);
    primitives_draw_box(.052f, .13f, .075f); glPopMatrix();

    glColor3f(.12f, .13f, .16f);
    glPushMatrix(); glTranslatef(0, -.055f, .010f); glRotatef(-16, 1, 0, 0);
    primitives_draw_box(.052f, .12f, .070f); glPopMatrix();

    glColor3f(.19f, .21f, .25f);
    glPushMatrix(); glTranslatef(0, -.006f, .11f);
    primitives_draw_box(.060f, .078f, .16f); glPopMatrix();
}

/* The rifle before there was a mesh for it, and still what draws if the mesh
   will not load: longer everywhere than the sidearm, with a full-length barrel
   out past a vented handguard, a deeper receiver, a longer rail, and a stock
   behind the grip. Its proportions are the ones weapon_specs.c is tuned to, so
   the fallback puts the muzzle and the rail where the real gun has them. */
static void draw_body_rifle(void) {
    /* Receiver. */
    glColor3f(.18f, .20f, .24f);
    glPushMatrix(); glTranslatef(0, 0, -0.20f);
    primitives_draw_box(.076f, .094f, .40f); glPopMatrix();

    /* Handguard, then the barrel and a muzzle device past it. The whole front
       end is kept short enough to stay under the sight line - a barrel that
       reaches far enough forward climbs back into the scope's window, because
       distance shrinks its offset faster than it shrinks the window. */
    glColor3f(.14f, .15f, .19f);
    glPushMatrix(); glTranslatef(0, -0.004f, -0.44f);
    primitives_draw_box(.060f, .060f, .20f); glPopMatrix();
    glColor3f(.11f, .12f, .15f);
    glPushMatrix(); glTranslatef(0, -0.004f, -0.55f);
    primitives_draw_box(.030f, .030f, .18f); glPopMatrix();
    glColor3f(.16f, .17f, .21f);
    glPushMatrix(); glTranslatef(0, -0.004f, -0.645f);
    primitives_draw_box(.042f, .042f, .048f); glPopMatrix();

    /* Vents cut into the handguard, three a side. */
    glColor3f(.09f, .10f, .12f);
    for (int i = 0; i < 3; ++i) {
        float z = -0.385f - (float)i * .055f;
        for (int side = -1; side <= 1; side += 2) {
            glPushMatrix(); glTranslatef((float)side * .031f, -0.004f, z);
            primitives_draw_box(.004f, .030f, .022f); glPopMatrix();
        }
    }

    /* The rail. Stops short of the handguard for the same reason the barrel
       does. */
    glColor3f(.22f, .24f, .28f);
    glPushMatrix(); glTranslatef(0, .056f, -0.215f);
    primitives_draw_box(.032f, .024f, .29f); glPopMatrix();

    /* Magazine, canted forward the way a box magazine sits. */
    glColor3f(.15f, .16f, .20f);
    glPushMatrix(); glTranslatef(0, -.105f, -0.20f); glRotatef(8, 1, 0, 0);
    primitives_draw_box(.056f, .17f, .085f); glPopMatrix();

    /* Grip. */
    glColor3f(.12f, .13f, .16f);
    glPushMatrix(); glTranslatef(0, -.070f, .015f); glRotatef(-18, 1, 0, 0);
    primitives_draw_box(.054f, .135f, .072f); glPopMatrix();

    /* Receiver extension and stock. */
    glColor3f(.17f, .18f, .22f);
    glPushMatrix(); glTranslatef(0, .002f, .10f);
    primitives_draw_box(.048f, .056f, .13f); glPopMatrix();
    glColor3f(.20f, .22f, .26f);
    glPushMatrix(); glTranslatef(0, -.004f, .19f);
    primitives_draw_box(.058f, .096f, .10f); glPopMatrix();
}

/* The magazine, while it is somewhere other than in the gun.

   Two questions, both answered off the reload track: WHETHER the gun still has
   one - it does not, for the beat between the spent one being let go and a
   fresh one being taken - and WHERE the loose one is, which is on the gun until
   it is pulled, then falling, then in the hand coming back up.

   Returns which materials the GUN must not draw this frame, so the two cannot
   disagree about whether a magazine is fitted: a magazine drawn in the hand and
   also in the well is worse than either alone. NULL means the gun draws itself
   whole, which is every frame outside a reload. */
static const char* const* magazine_split(const Weapon* weapon,
                                         const char* hide[3],
                                         ActionSample* sample) {
    const WeaponSpec* spec = weapon->spec;
    const FirearmSpec* firearm = weapon_spec_firearm(spec);
    /* No gun half is no magazine, and the null test has to come first: a sword
       reload-blends to zero anyway, but the two conditions were being read in
       the order that dereferences before it checks. */
    if (!firearm || weapon_reload_blend(weapon) <= 0.0f ||
        !firearm->magazine_part) return NULL;
    action_track_sample(weapon_spec_reload_sequence(spec),
                           weapon->reload, sample);
    /* Seated at BOTH ends of the span is the only case where the magazine is
       still the gun's own - anything else and it has started to move. */
    if (sample->from->mag_frame == ACTION_ON_WEAPON &&
        sample->to->mag_frame == ACTION_ON_WEAPON &&
        sample->from->mag[1] == 0.0f && sample->to->mag[1] == 0.0f &&
        sample->from->drawn && sample->to->drawn)
        return NULL;

    int n = 0;
    hide[n++] = firearm->magazine_part;
    if (firearm->rounds_part) hide[n++] = firearm->rounds_part;
    hide[n] = NULL;
    return hide;
}

/* The loose magazine, drawn after the gun and in the same camera space, with
   whatever transform the track has it at. Called with the GUN's transform
   current, so a magazine still nominally on the weapon needs no work at all. */
static void draw_loose_magazine(const Weapon* weapon, const Mat4* gun) {
    const WeaponSpec* spec = weapon->spec;
    const FirearmSpec* firearm = weapon_spec_firearm(spec);
    const char* hide[3];
    ActionSample sample;
    /* The same question draw_receiver asked, asked again rather than passed
       down: if the gun is still wearing its magazine there is nothing loose to
       draw, and the two answers come from one function so they cannot differ. */
    if (!magazine_split(weapon, hide, &sample)) return;
    const ActionKey* from = sample.from;
    const ActionKey* to = sample.to;
    /* The hand is empty for one beat of the track, and that beat is the point
       of the whole animation - it is the only frame that says a magazine was
       CHANGED rather than refilled where it sat.

       Taken from the NEARER key rather than from both, which is the difference
       between the beat happening and not: `drawn` is a state and not a
       quantity, there is no half a magazine, and a single empty key never has
       an empty key on both sides of it. Testing both would keep the magazine on
       screen through the one moment it is supposed to be gone. */
    if (!(sample.t < 0.5f ? from->drawn : to->drawn)) return;
    /* And once the hand has let go, the magazine belongs to the world: there is
       a real one falling in the arena from this instant, and drawing the held
       one as well would put two on screen. The track keeps describing it past
       this point only so the handoff has somewhere to hand off FROM. */
    {
        const ActionTrack* track = weapon_spec_reload_sequence(spec);
        if (weapon->reload >= track->drop_at &&
            from->mag_frame != ACTION_IN_HAND &&
            to->mag_frame != ACTION_IN_HAND)
            return;
    }

    float blend = weapon_reload_blend(weapon);
    /* Where a magazine that has just come into existence is, is a state and not
       a quantity - the same argument the `drawn` test above makes, applied to
       the position it is drawn at.

       On the span where the hand closes on a fresh magazine, one end of the
       span has one and the other does not, and the end that does not is still
       describing where the SPENT one had fallen to. Blended, the fresh magazine
       appeared a good twenty centimetres below the hand holding it and slid up
       into the fist over the next tenth of a second, which reads as a magazine
       being summoned rather than taken off a chest. Only the end that has it
       knows where it is.

       One-sided, and that matters: a magazine on its way OUT is moving along a
       path both ends of the span describe, and blending it is exactly what
       keeps it falling through the frames before the world takes it over. */
    float t = (!from->drawn && to->drawn) ? 1.0f : sample.t;

    /* Where the magazine sits, resolved in camera space at both ends of the
       span before being blended - the same reason the hand's own target is
       resolved before it is blended, since a key on the gun and a key in the
       hand are not in the same frame and the raw numbers do not interpolate. */
    float ends[2][3];
    const ActionKey* key[2] = {from, to};
    for (int e = 0; e < 2; ++e) {
        if (key[e]->mag_frame == ACTION_IN_HAND) {
            /* Carried: wherever the support hand is, offset so the magazine
               hangs below the fist rather than through it. Asked of the arms so
               the two cannot drift. */
            ViewmodelBlend b = {weapon_sprint_blend(weapon), blend,
                                weapon->ads, weapon->reload};
            float hand[3];
            viewmodel_arms_support_target(spec, &b, gun, hand);
            float seated[3];
            mat4_transform_point(gun, weapon_spec_mag_grip(spec), seated);
            /* The magazine's own origin, put where its grip lands on the hand. */
            for (int c = 0; c < 3; ++c)
                ends[e][c] = hand[c] - (seated[c] - gun->m[12 + c]);
        } else {
            float local[3];
            for (int c = 0; c < 3; ++c) local[c] = key[e]->mag[c];
            mat4_transform_point(gun, local, ends[e]);
        }
    }

    float at[3];
    for (int c = 0; c < 3; ++c) at[c] = lerpf(ends[0][c], ends[1][c], t);
    /* Faded in by the reload's own weight, so aiming out of a magazine change
       takes the loose magazine with it rather than leaving one hanging in the
       air beside a shouldered weapon. */
    float origin[3] = {gun->m[12], gun->m[13], gun->m[14]};
    for (int c = 0; c < 3; ++c) at[c] = lerpf(origin[c], at[c], blend);

    const char* parts[3];
    int n = 0;
    parts[n++] = firearm->magazine_part;
    /* Rounds on the fresh magazine and not on the spent one. The moment the
       player can see the difference is short, and it is the moment that says
       what just happened. */
    if (firearm->rounds_part && (t < 0.5f ? from->loaded : to->loaded))
        parts[n++] = firearm->rounds_part;
    parts[n] = NULL;

    glPushMatrix();
    glLoadIdentity();
    glTranslatef(at[0], at[1], at[2]);
    /* The gun's own rotation, so a magazine in the well is aligned with the
       well, plus whatever tumble the track has given it on the way down. */
    Mat4 spin = *gun;
    spin.m[12] = spin.m[13] = spin.m[14] = 0.0f;
    glMultMatrixf(spin.m);
    glRotatef(lerpf(from->mag_roll, to->mag_roll, t), 0, 0, 1);
    /* And the fore-and-aft rock, about the magazine's own mouth - which is
       where a real one pivots as it is guided into the well and as a spent one
       tips out of it. Applied after the roll so the two compose as a wrist
       does: the body swings out sideways and tips at the same time. */
    glRotatef(lerpf(from->mag_pitch, to->mag_pitch, t), 1, 0, 0);
    weapon_model_draw_only(spec, parts);
    glPopMatrix();
}

static void draw_receiver(const Weapon* weapon) {
    /* The mesh is authored in exactly these axes, so it needs no transform of
       its own; the boxes stand in for whichever gun has no mesh yet. */
    const FirearmSpec* firearm = weapon_spec_firearm(weapon->spec);
    const char* hide[3];
    ActionSample sample;
    const char* const* without = magazine_split(weapon, hide, &sample);
    const char* const* hidden = without ? without : weapon->spec->hidden_parts;
    /* The iron sights come off either way. When the magazine also has to, the
       two lists are merged rather than one replacing the other. */
    const char* merged[WEAPON_MAX_HIDDEN_PARTS + 3];
    if (without) {
        int n = 0;
        for (int i = 0; i < WEAPON_MAX_HIDDEN_PARTS &&
                        weapon->spec->hidden_parts[i]; ++i)
            merged[n++] = weapon->spec->hidden_parts[i];
        for (int i = 0; without[i]; ++i) merged[n++] = without[i];
        merged[n] = NULL;
        hidden = merged;
    }
    if (!weapon_model_draw(weapon->spec, hidden)) {
        /* The boxes, for a weapon whose mesh is missing or not authored yet.
           Picked off the hold rather than off a model enum, because the hold is
           the thing that actually differs between the two shapes drawn here -
           a gun held at two points is a long one, and one held at a single
           point is a sidearm. There are no boxes for a sword: it arrived with
           its mesh, and a box standing in for a katana would read as a bug
           rather than as a placeholder. */
        if (weapon->spec->kind != WEAPON_CLASS_FIREARM) { /* nothing to draw */ }
        else if (weapon->spec->hold == WEAPON_HOLD_TWO_POINT) draw_body_rifle();
        else draw_body_auto_pistol();
    }
    /* The optic ships as its own mesh and is bolted on here rather than baked
       into the gun, because that is what it is: one sight that any rail can
       carry, mounted at whatever height and offset the spec states. It is
       also what the HUD paints a reticle into and what a magnified pass is
       masked to, which is the second reason it stays the renderer's. */
    weapon_renderer_draw_sight(weapon->spec);

    if (weapon->muzzle_flash <= 0.0f) return;
    /* The flash is one frame's worth of light on the end of the barrel, sized
       by whatever is left of its timer so it never lingers as a blob. */
    float flash = weapon->muzzle_flash / WEAPON_MUZZLE_FLASH_TIME;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0f, .88f, .55f, flash);
    if (firearm) {
        glPushMatrix(); glTranslatef(0, firearm->muzzle_up,
                                     -firearm->muzzle_forward);
        primitives_draw_box(.085f * flash, .085f * flash, .10f * flash);
        glPopMatrix();
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* The clip and time this weapon is in, resolved against the loaded rig. Both
   the base pose and the arms go through this, so they cannot be sampling
   different frames of different clips. */
static int melee_sample(const CharacterLibrary* arms, int arms_rig,
                        const Weapon* weapon, Pose* arms_out,
                        ViewmodelPose* out) {
    if (!arms || arms_rig == CHARACTER_RIG_NONE || !weapon) return 0;
    const MeleeSpec* melee = weapon_spec_melee(weapon->spec);
    if (!melee) return 0;
    const SkinnedModel* model = character_library_model(arms, arms_rig);
    if (!model) return 0;
    float time = 0.0f;
    const char* name = weapon_melee_clip(weapon, &time);
    int clip = viewmodel_clip_arms_find(model, name);
    if (clip < 0) return 0;
    return viewmodel_clip_arms_base(model, clip, time, melee, arms_out, out);
}

int weapon_renderer_melee_base(const CharacterLibrary* arms, int arms_rig,
                               const Weapon* weapon, ViewmodelPose* out) {
    return melee_sample(arms, arms_rig, weapon, NULL, out);
}

void weapon_renderer_draw_viewmodel(const Weapon* weapon,
                                    const PlayerController* player,
                                    const ThirdPersonCamera* camera,
                                    float aspect_ratio,
                                    const CharacterLibrary* arms,
                                    int arms_rig) {
    if (!camera->is_first_person) return;

    /* Its own near plane and a cleared depth buffer: the gun is half a metre
       from the eye and would otherwise be sliced by the 0.1m near plane or by
       any wall the player is pressed against. */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    double fov = lerpf(weapon->spec->view_fov, weapon_spec_view_ads_fov(weapon->spec),
                       weapon->ads);
    double top = 0.02 * tan(fov * MATH_PI / 360.0);
    glFrustum(-top * aspect_ratio, top * aspect_ratio, -top, top, 0.02, 8.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glClear(GL_DEPTH_BUFFER_BIT);

    /* Camera space: -Z is forward. The light has to be restated in this space
       or the gun is lit by wherever the world's lamp happens to be. */
    const GLfloat light_position[] = {0.4f, 0.8f, 0.6f, 0};
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);

    /* A fill from the other side, for the viewmodel and nothing else.
       One directional lamp leaves half of any closed object on ambient alone,
       and the key above comes from the right - while the gun rides to the
       right of the eye, so the flank the camera actually looks at is the
       unlit one. At this gun's albedo that flank lands around 10/255, darker
       than the sky behind it, and the optic reads as a hole punched in the
       rifle rather than as a black object sitting on it.
       There is no shadowing and no bounce here to do that job, so it is faked:
       one dim, cool, opposing lamp, which is what a key-and-fill pair has
       always been. Cool rather than white so it reads as sky bounce and not
       as a second sun, and diffuse only - lifting the ambient instead would
       flatten the whole gun rather than open up one side of it. */
    const GLfloat fill_position[] = {-0.62f, 0.16f, 0.77f, 0};
    const GLfloat fill_diffuse[] = {0.30f, 0.33f, 0.42f, 1.0f};
    const GLfloat fill_ambient[] = {0, 0, 0, 1};
    glLightfv(GL_LIGHT1, GL_POSITION, fill_position);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, fill_diffuse);
    glLightfv(GL_LIGHT1, GL_AMBIENT, fill_ambient);
    glEnable(GL_LIGHT1);

    /* Every box here is a few centimetres across, and primitives_draw_box gets
       there by scaling a unit cube. GL scales the normals by the inverse of
       that, so a two centimetre wall arrives at the lighting stage with a
       normal fifty times too long and the surface saturates to white. The
       world never showed it because its boxes are metres wide, which shrinks
       the normals instead and only costs a little brightness. */
    GLboolean was_normalizing = glIsEnabled(GL_NORMALIZE);
    if (!was_normalizing) glEnable(GL_NORMALIZE);

    /* The pose comes from the weapon, which is also what its tracers leave
       from. Recomputing it here would let the drawn barrel and the streak's
       origin drift apart the first time either changed. */
    ViewmodelPose pose;
    weapon_viewmodel_pose(weapon, player, &pose);
    glRotatef(player->lean * VIEWMODEL_LEAN_ROLL, 0, 0, 1);
    glTranslatef(pose.x, pose.y, pose.z);
    /* Where the gun is pointing. The same two numbers the bullet is composed
       from, applied about the gun's own origin, which is what lets a reticle
       drawn under this transform be honest about where the round goes. The
       rotation that used to sit here rode on view_kick and was declared
       cosmetic - fine while nothing read it, fatal once the reticle does.

       Off the pose rather than off the weapon, which is not a tidy-up: these
       two lines are inside the gun's transform, and anything taking a point off
       the drawn gun has to travel through the same pair. Reading them from the
       same struct that carries the rest of the transform is what makes that
       one fact rather than two that happen to agree. */
    glRotatef(pose.kick_pitch, 1, 0, 0);
    glRotatef(pose.kick_yaw, 0, 1, 0);
    /* How the gun is CARRIED: canted toward the player at the hip, pulled in
       and rolled over during a sprint, square when aimed. Still cosmetic, and
       it does swing the reticle with it - correctly, since a canted sight
       points where it is canted. It costs nothing, because a carried gun's
       window is nowhere near the eye and the reticle is masked out entirely by
       the glass; by the time it can be seen the carry has unwound to zero.

       Named in the reverse of the order viewmodel_pose_transform_point applies
       them, GL handing the last transform named to the vertex first. Anything
       that takes a point off this gun goes through that function, so this is
       the one place the two orders have to be kept in step. */
    glRotatef(pose.yaw, 0, 1, 0);
    glRotatef(pose.pitch, 1, 0, 0);
    glRotatef(pose.roll, 0, 0, 1);

    /* The finished gun transform, taken off the stack rather than recomposed.
       The arms solve their hands onto points of the weapon mesh, so this is
       what turns those points into camera-space targets - and reading the live
       matrix is the only version that cannot drift from the gun that was
       actually drawn. */
    GLfloat gun_matrix[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, gun_matrix);

    /* The scabbard, in camera space. There is no body in first person to hang
       it off, so it goes on the eye instead, dropped to where a hip is - the
       same offsets the pelvis socket uses, measured from a different origin.
       Mostly out of frame, and drawn anyway because the draw and the sheathe
       are motions between the hand and this point, and a motion toward
       something that is not being drawn is a hand reaching into nothing.

       Before the weapon so the blade is over it rather than through it. */
    {
        const MeleeSpec* melee = weapon_spec_melee(weapon->spec);
        if (melee && melee->scabbard_path) {
            glPushMatrix();
            /* Through the same half turn the arms take. The spec's offsets
               and angles are stated in the RIG's frame, where the body faces
               +Z; the camera looks down -Z. Without this the sword would hang
               correctly on a third-person hip and point out through the
               player's front in first person, off one set of numbers that
               looked right in both places separately. */
            float worn[3];
            arm_placement_rig_to_camera(melee->scabbard_offset, 0.0f, worn);
            glTranslatef(worn[0], worn[1] - VIEWMODEL_HIP_BELOW_EYE, worn[2]);
            glRotatef(ARMS_FACING_DEGREES, 0, 1, 0);
            glRotatef(melee->scabbard_yaw, 0, 1, 0);
            glRotatef(melee->scabbard_pitch, 1, 0, 0);
            glRotatef(melee->scabbard_roll, 0, 0, 1);
            weapon_model_draw_scabbard(melee);
            glPopMatrix();
        }
    }

    if (!weapon->sheathed) draw_receiver(weapon);
    /* After the gun, so the hood is in the depth buffer to occlude with, and
       the glass is available to mask to. */
    const FirearmSpec* firearm = weapon_spec_firearm(weapon->spec);
    if (firearm && firearm->sight &&
        firearm->sight->reticle == WEAPON_RETICLE_HOLOGRAPHIC)
        draw_holographic_reticle(weapon, camera->fov_degrees, aspect_ratio);

    /* The arms, in camera space - they hang off the player's shoulders and are
       not children of the gun, so the gun's own pose comes back off first. Last
       in the pass, so the gun is already in the depth buffer and a hand that
       solves a little into the receiver is hidden by it rather than drawn
       through it. */
    /* The magazine, if a reload has taken it off the weapon. Drawn after the
       gun so the receiver is already in the depth buffer, and before the arms
       so a hand carrying it is drawn over it rather than through it. */
    {
        Mat4 gun_transform;
        memcpy(gun_transform.m, gun_matrix, sizeof gun_transform.m);
        glPushMatrix();
        draw_loose_magazine(weapon, &gun_transform);
        glPopMatrix();
    }

    if (arms && arms_rig != CHARACTER_RIG_NONE &&
        weapon_spec_melee(weapon->spec)) {
        /* The animated arms, in camera space, with the same clip and frame the
           base pose above came from. The sword has already been drawn at that
           pose, so the hand arrives around it rather than beside it. */
        glPopMatrix();
        glPushMatrix();
        glLoadIdentity();
        Pose swing;
        ViewmodelPose unused;
        if (melee_sample(arms, arms_rig, weapon, &swing, &unused))
            viewmodel_arms_draw_pose(arms, arms_rig, &swing);
    } else if (arms && arms_rig != CHARACTER_RIG_NONE) {
        glPopMatrix();
        glPushMatrix();
        glLoadIdentity();
        ViewmodelBlend blend = {
            weapon_sprint_blend(weapon), weapon_reload_blend(weapon),
            weapon->ads, weapon->reload,
        };
        viewmodel_arms_draw(arms, arms_rig, weapon->spec, gun_matrix, &blend);
    }

    /* The fill belongs to the gun, not to the frame. The world runs on one
       lamp and expects to find one still burning when it draws. */
    glDisable(GL_LIGHT1);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    if (!was_normalizing) glDisable(GL_NORMALIZE);
}
