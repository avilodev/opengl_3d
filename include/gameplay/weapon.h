#ifndef WEAPON_H
#define WEAPON_H

#include "input/commands.h"
#include "gameplay/combatant.h"
#include "gameplay/player_controller.h"
#include "gameplay/weapon_spec.h"
#include "gameplay/viewmodel_pose.h"
#include "gameplay/melee_swing.h"
#include "utils/mat4.h"

/* One gun in the player's hands. Which gun is the spec it was initialised
   with; this file has no numbers of its own.

   It has no state gates: there is no movement state in which it refuses to
   fire, reload, aim, or hold its accuracy, because a weapon that forces the
   player to stop is a weapon that fights the game.

   The effect rings live here rather than in the renderer because their
   lifetimes are simulation, not drawing. The renderer reads them and never
   writes to them. */

#define WEAPON_TRACER_COUNT 24
#define WEAPON_IMPACT_COUNT 32
/* Spent magazines lying about. Four is more than a player can drop inside one
   magazine's lifetime, so the ring never has to overwrite a live one. */
#define WEAPON_DROPPED_MAG_COUNT 4

typedef struct {
    float start_x, start_y, start_z;
    float end_x, end_y, end_z;
    /* Seconds remaining. At or below zero the slot is free. */
    float life;
} WeaponTracer;

typedef struct {
    float x, y, z;
    float normal_x, normal_y, normal_z;
    float life;
    /* Whether it landed on a body rather than on the arena. The mark is drawn
       differently for each, which is the cheapest honest answer to "did that
       one connect" the player has: the number and the hit marker both arrive a
       moment later and at the middle of the screen, and this one is out there
       on the target where they were already looking. */
    unsigned char on_body;
} WeaponImpact;

/* A magazine the player has thrown away, in WORLD space - which is what makes
   it worth having. Everything else about a reload lives in front of the eye and
   moves with it; a dropped magazine is the one part that stays where it was
   dropped, and a player who turns around and sees it on the floor learns
   something about the world that no viewmodel animation can tell them.

   Simulated here rather than drawn as an effect because it has state that
   outlives a frame: it falls, it lands, and then it lies there. */
typedef struct {
    float x, y, z;
    float velocity_x, velocity_y, velocity_z;
    /* Degrees, and the rate it is turning at. Zeroed on landing so it stops
       dead rather than spinning on the floor. */
    float spin;
    float spin_rate;
    float tilt;
    float tilt_rate;
    /* Where the floor is under it, found once when it is dropped. */
    float rest_y;
    /* Seconds remaining. Landing shortens this to the rest time, so a magazine
       always lies still for the same beat however far it fell. */
    float life;
    int grounded;
} WeaponDroppedMag;

typedef enum {
    WEAPON_MELEE_ACTION_NONE,
    WEAPON_MELEE_ACTION_DRAW,
    WEAPON_MELEE_ACTION_SHEATHE
} WeaponMeleeAction;

typedef struct {
    /* Never null after weapon_init. Points at a static WeaponSpec, so it stays
       valid for the life of the process and costs nothing to copy around. */
    const WeaponSpec* spec;

    /* What this gun can hit, or NULL for a gun that traces geometry only.

       Published once rather than passed per step, for the same reason
       world_muzzle is published rather than passed: the set of bodies in the
       world outlives every shot and is not an input to one. NULL is a real and
       supported state - it is what the headless probe runs against, and it is
       what the arithmetic in this file has to keep working under, because
       nothing about a cone, a kick or a tracer depends on there being anything
       out there to hit. */
    CombatantSet* targets;

    int ammo_in_magazine;
    int is_reloading;
    float reload_time_left;
    /* Time until the next round may leave the barrel. */
    float fire_cooldown;
    /* Time since the last round did. Bloom recovery waits on this. */
    float quiet_time;
    /* The hip-fire cone. What a shot actually uses is weapon_cone_degrees,
       which applies the scope to it - never read this directly to aim. */
    float bloom_degrees;
    float muzzle_flash;

    /* How far into the scope the player is, 0 to 1. Held on right mouse and
       eased over WEAPON_ADS_TIME, so the viewmodel, the zoom, the cone and the
       reticle all move on one number and cannot fall out of step. */
    float ads;

    /* How far the gun is stowed for a ledge hang, 0 to 1, eased like ads. Both
       hands are on the ledge, and with no hands to draw the gun leaving frame
       is what says so - it is the whole tell, so it moves on its own smoothed
       number rather than snapping with the state. */
    float stow;

    /* How far into the sprint carry the gun is, 0 to 1, eased like the other
       two. The third pose on its own number, for the same reason as the first
       two: one number moves the offset, the rotation, the shoulder placement
       and the sway together, so they cannot fall out of step.

       Do not read this directly to pose anything. Precedence is stow over ads
       over sprint - both of the others move the gun along the same axes, and a
       half-aimed half-sprinting pose is neither - so what a pose wants is
       weapon_sprint_blend, which applies it. */
    float sprint;

    /* How far through a magazine change, 0 to 1, and NOT eased - this one is a
       clock. The other three are states the player holds and can be halfway
       into; a reload is a sequence that starts, runs and finishes, so it is
       driven straight off reload_time_left rather than approached. Zero
       whenever no reload is running.

       The pose's swell in and out of the reload is shaped from this by
       reload_sequence_gun_blend; what a caller posing the gun wants is
       weapon_reload_blend, which applies the precedence as well. */
    float reload;

    /* Where the gun is pointing, relative to where the player is looking.
       Recoil accumulates here and decays back to zero, so it is a bounce and
       never a pull the player has to hold against.

       This is the gun's aim, not a decoration. The shot direction is composed
       from it and so is the reticle, because a holographic reticle is fixed in
       angle to the sight body: it points where the GUN points, while the
       bullet leaves the EYE. The two agree only if one number drives both.
       Kicking the camera instead - which is what this used to do - left the
       gun free to rotate cosmetically underneath a reticle that knew nothing
       about it, and the reticle would have started lying about where the round
       goes the moment the gun moved. */
    float gun_pitch_degrees;
    float gun_yaw_degrees;

    /* Where the body's own rifle has its muzzle, in world units, and whether
       anyone has said. The viewmodel is drawn in camera space and its barrel is
       placed from that - which is exactly right while the player is looking
       down it, and nonsense the moment they are not: in third person the
       viewmodel is not drawn at all, and a streak leaving its barrel came out
       of thin air half a metre beside the player's head, swinging around them
       as they turned. So whoever draws the body publishes where that body's gun
       actually is, and the tracer leaves from there instead.

       Set by the frame, not by the simulation - see weapon_set_world_muzzle. */
    float world_muzzle[3];
    int world_muzzle_valid;

    /* The viewmodel's shove back along the barrel as a round leaves, springing
       out over WEAPON_VIEW_KICK_DECAY. Purely cosmetic - it is a translation,
       and a translation cannot move where a collimated reticle points. The
       rotation that used to ride on this is gun_pitch_degrees now, because
       that one does move the aim and has to be the same number the bullet
       reads. */
    float view_kick;

    /* A shot needs a fresh press of the trigger. Set while the button is down
       from before the weapon was allowed to fire - closing the pause menu with
       a click, most obviously - so that click does not become a burst. */
    int trigger_latched;

    unsigned int random_state;

    /* The melee half of the weapon. Which attack is running, how far into its
       clip it is, and whether the blade is in its scabbard.

       `attack` is an index into the spec's own attack table and -1 for idle,
       which makes the attack the player is in the whole of the combo state -
       the table already says where each one goes next, so there is nothing
       here for a second state machine to disagree with. */
    /* Which scripted action is running, if any, and how far through it - 0 to
       1 of the spec's own draw_time or sheathe_time. A draw and a sheathe are
       the same kind of thing in opposite directions, so they are one clock and
       a direction rather than two clocks that could both be running. */
    WeaponMeleeAction melee_action;
    float melee_action_progress;

    /* Seconds since the sword last did anything. A weapon that has been idle
       long enough puts itself away, which is what keeps a sword from being
       carried drawn through every corridor between fights - and what makes
       drawing it mean something when it happens.

       An attack pressed while it is away buffers here rather than being
       dropped: the player asked to swing, and answering "you were sheathed"
       is the game arguing with an input it understood perfectly well. It fires
       the moment the draw lands. */
    float melee_quiet_time;
    int melee_attack_buffered;
    int melee_heavy_buffered;

    int melee_attack;
    /* Whether what is playing is the attack or the recovery that follows it.
       The same index either way, because a recovery belongs to its attack and
       is not a fourth thing the chain can be in - it is how an attack ENDS
       when nothing followed it, and it is over as soon as anything does. */
    int melee_recovering;
    float melee_time;
    float melee_duration;
    int sheathed;

    /* What the running swing has already cut, and where the blade was last
       frame. The previous transform is kept because a sweep needs both ends of
       the interval and only one of them is this frame's - and it lives here
       rather than on the swing so a swing that starts mid-motion sweeps from
       where the blade actually was rather than from its first frame. */
    MeleeSwing swing;
    /* The blade's world transform on the last step, and whether there has been
       one. A sweep needs both ends of the interval and only one of them is
       this step's. */
    Mat4 blade_previous;
    Mat4 blade_now;
    int blade_valid;

    /* Where the animated hand put the weapon this frame, in camera space,
       filled by whoever has the rig before the pose is composed. A firearm
       leaves it invalid and takes its base pose from the spec instead.

       Cached rather than solved in weapon_viewmodel_pose because that function
       has no rig and should not grow one: a sword's base pose is an animation
       sample, and sampling belongs where the model is. */
    ViewmodelPose melee_base;
    int melee_base_valid;

    WeaponTracer tracers[WEAPON_TRACER_COUNT];
    WeaponImpact impacts[WEAPON_IMPACT_COUNT];
    WeaponDroppedMag dropped_mags[WEAPON_DROPPED_MAG_COUNT];
} Weapon;

void weapon_init(Weapon* weapon, const WeaponSpec* spec);

/* Says which bodies this gun's rounds can land on. The weapon does not own the
   set and does not outlive it; whoever owns the world calls this once.

   Mutable, and it is the one thing here that is: resolving a hit takes health
   off a body and throws a figure for it, which is a write. The trace that finds
   the body takes the set const - a question and its consequence are separate
   calls, and only the second of them is allowed to change anything. */
void weapon_set_targets(Weapon* weapon, CombatantSet* targets);

/* `aim_yaw`/`aim_pitch` are the player's look angles, with no recoil in them:
   the kick lives on the gun now, and this composes it in itself through
   weapon_aim_angles. Handing it pre-kicked angles would apply the kick twice.
   `eye_height` is the camera's height above the player's feet, so a crouched or
   mid-landing shot leaves from where the player is actually looking from.

   `view_fov_degrees` is the WORLD's field of view this frame, which the tracer
   needs and the bullet does not: the gun is drawn through its own, narrower
   projection, so placing the visible streak on the barrel means knowing both. */
/* Says where the body's rifle muzzle is this frame, for the frames the player
   is not looking down the viewmodel. `valid` zero puts the streak back on the
   viewmodel's own barrel, which is what first person wants.

   It is a frame's job to set this and not a simulation step's: which camera is
   up and where the drawn body's hand ended up are both presentation, and the
   pose it is read off is the one the last frame drew. That makes it a frame
   stale, which for the origin of a streak that lives sixty milliseconds is not
   a quantity anyone can see. */
void weapon_set_world_muzzle(Weapon* weapon, const float muzzle[3], int valid);

/* Where the animated hand has put a melee weapon this frame, in camera space.
   Published by the frame rather than solved inside the simulation, for the
   same reason the world muzzle is: it comes off a rig and a clip, and neither
   of those is something weapon.c should have to hold. `valid` zero puts the
   weapon back on the spec's rest pose, which is where it sits for the frame or
   two before the arms are ready. */
void weapon_set_melee_base(Weapon* weapon, const ViewmodelPose* base, int valid);

/* Which clip a melee weapon is playing and how far into it, for whoever has
   the rig to sample. NULL on a weapon that is not melee.

   Here and not in the renderer because it is the answer to a gameplay
   question - what is this weapon doing - and two callers want it: the pose the
   sword is drawn at and the arms it is drawn in. One derivation read twice,
   rather than two that agree until one of them is edited. */
const char* weapon_melee_clip(const Weapon* weapon, float* time_out);

/* Starts putting the sword away, or taking it back out - whichever the weapon
   is not already doing. Ignored on a firearm, and while an action is already
   running: a draw interrupted halfway is a sword in two places. */
void weapon_toggle_sheathe(Weapon* weapon);

/* Where the sword actually is in the world, published by the frame off the
   body's own hand - see character_weapon_world. Melee hit detection sweeps
   between the last two of these, so a frame that does not publish one simply
   does not cut, which is the right answer for a frame with no body posed. */
void weapon_set_blade_transform(Weapon* weapon, const Mat4* world, int valid);

/* Where the visible streak starts, in world units. The round itself always
   leaves the eye - what the crosshair covers is what it hits - so this places
   only the line, at the barrel of the gun the player can see. Public because it
   is a query about the drawn weapon, and the one thing a check of it needs is
   to be able to ask.

   `yaw`, `pitch` and `roll` are the CAMERA's, not the shot's: the gun is drawn
   in camera space, so an offset measured there is rotated into the world by the
   basis the world was drawn through. */
void weapon_tracer_origin(const Weapon* weapon, const PlayerController* player,
                          float yaw_degrees, float pitch_degrees,
                          float roll_degrees, float eye_height,
                          float view_fov_degrees,
                          float* x, float* y, float* z);

void weapon_update(Weapon* weapon, const WeaponCommand* command,
                   const PlayerController* player,
                   float aim_yaw_degrees, float aim_pitch_degrees,
                   float camera_roll_degrees,
                   float eye_height, float view_fov_degrees,
                   float delta_time);

/* Where the viewmodel sits this frame and which way it is turned, with the bob,
   the sway, the kick and the stow already in it. The renderer draws the gun
   here, the weapon starts its tracers from the barrel tip relative to here, and
   the scope pass masks to the glass relative to here, so all three agree about
   a gun that is off to the right at the hip, in against the chest at a sprint
   and dead centre through the sight. Two copies of this would drift the moment
   either changed.

   The rotation is cosmetic and must stay that way - see ViewmodelPose. To take
   a point off the drawn weapon, hand this to viewmodel_pose_transform_point
   rather than composing the angles again at the call site. */
void weapon_viewmodel_pose(const Weapon* weapon, const PlayerController* player,
                           ViewmodelPose* out);

/* How much of the sprint carry is actually in the pose, 0 to 1: the eased
   number with the precedence applied. Aiming and stowing each collapse it,
   because all three move the gun along the same axes and the blend has to
   resolve to one of them rather than to the average of two.

   One function, so the pose, the arms' shoulder placement and the bob's
   crossfade all read the same weight. Reading weapon->sprint directly is what
   would let them disagree by a frame of easing. */
float weapon_sprint_blend(const Weapon* weapon);

/* How much of the reload pose the gun is wearing, 0 to 1: the sequence's own
   swell with the precedence applied. Aiming and stowing collapse it, the same
   way and for the same reason they collapse the sprint carry.

   That aiming overrides a reload is deliberate and is not a state gate - the
   reload still runs to completion on its own clock, and the player can still
   raise the sight whenever they like. What it means is only that the gun goes
   where the player is pointing it rather than staying down in a pose they have
   asked to leave, which is the same bargain every other carry here makes. */
float weapon_reload_blend(const Weapon* weapon);

/* The gun's aim: the player's look angles with the weapon's own rotation
   composed in. The bullet leaves along these, and the reticle is drawn along
   them, which is the whole reason they exist as one function. */
void weapon_aim_angles(const Weapon* weapon, float look_yaw_degrees,
                       float look_pitch_degrees,
                       float* yaw_degrees, float* pitch_degrees);

/* Requires a fresh trigger pull before the next shot. Call when the weapon
   comes back under the player's control after something else had the mouse. */
void weapon_latch_trigger(Weapon* weapon);

/* The half-angle of the cone the next round will leave inside, in degrees.
   This is the one place aiming and the crosshair both read, which is what
   keeps them honest with each other. */
float weapon_cone_degrees(const Weapon* weapon);

/* On-screen radius of the sight's opening, as a fraction of half the
   viewport height. Derived from where the optic actually is and how wide the
   viewmodel's own projection is, so the black surround the HUD paints lands
   exactly on the rim of the bore the renderer drew. Hard-coding the HUD's
   window instead put a small metal ring inside a much larger black hole.
   Meaningless for an unmagnified sight, which has no surround. */
float weapon_sight_window_fraction(const Weapon* weapon);

/* Camera-space centre of a magnified sight's glass: +x right, +y up, -z
   forward. The renderer puts its magnified pass exactly here rather than at the
   middle of the screen, so while the sight is still swinging up the circle
   rides on the optic instead of hanging in space in front of it. */
void weapon_sight_center(const Weapon* weapon, const PlayerController* player,
                         float* x, float* y, float* z);

/* Whether this sight magnifies, and so wants a second pass of the world drawn
   inside it rather than simply zooming the whole screen. */
int weapon_sight_is_magnified(const Weapon* weapon);

#endif
