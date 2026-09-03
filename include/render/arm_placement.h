#ifndef RENDER_ARM_PLACEMENT_H
#define RENDER_ARM_PLACEMENT_H

/* Where a pair of shoulders has to sit to reach a gun's grips at a comfortable
   bend, and the geometry that decides it.

   Split out of viewmodel_arms.c, which is where these numbers are used, because
   nothing in here touches GL, a skeleton or a pose - it is two unknowns, a
   bounded scan and some trigonometry. Kept in that file it could only ever be
   checked by running the game and looking at the arms, which for a search that
   silently falls back when it fails is no check at all. Out here a probe links
   it against the real rig and prints what every pose actually solved to.

   Camera space throughout: +x right, +y up, -z forward. */

/* What a comfortable arm looks like, as a fraction of its reach. Two thirds is
   bent; past about 0.95 it is straight and stops tracking the gun. */
#define ARMS_COMFORT 0.72f
#define ARMS_COMFORT_MAX 0.94f
/* How far below the eye the shoulder line sits. The one placement number still
   stated rather than solved: it is a property of a body, not of a gun. */
#define ARMS_SHOULDER_DOWN 0.32f
/* How long the arms are, in metres from shoulder to wrist.

   Not the body rig's own length. That rig is scaled to CHARACTER_RIG_HEIGHT,
   which is 2.53m, and its arm comes out at 0.757m - a reach that belongs to the
   giant the third-person body is, not to a pair of hands holding a real-sized
   rifle a foot from the camera. */
#define ARMS_REACH 0.60f
/* The search, in the ranges a shoulder could plausibly sit and a torso could
   plausibly turn. Run once per weapon, so the step can afford to be fine. */
#define ARMS_SEARCH_Z_MIN (-0.90f)
#define ARMS_SEARCH_Z_MAX 0.60f
#define ARMS_SEARCH_Z_STEP 0.01f
#define ARMS_SEARCH_BLADE_MAX 80.0f
#define ARMS_SEARCH_BLADE_STEP 2.0f
/* How hard a solve is pulled toward the previous one in a sequence. Blade is
   divided by 60 degrees first, so the two unknowns are weighed in comparable
   units rather than one in metres against one in degrees. */
#define ARMS_CONTINUITY 0.006f
#define ARMS_CONTINUITY_BLADE 60.0f

/* The rig faces +Z at rest - character_renderer says so where it turns the body
   by its heading - and the camera looks down -Z, so the arms have to be turned
   to face the way the player is looking. Without it the rig's right arm lands on
   the screen's left and every mirrored assumption in here quietly inverts. */
#define ARMS_FACING_DEGREES 180.0f

/* The measurements the search needs off whatever rig is being posed, so this
   module never has to know what a skeleton is. Filled in once when the rig
   loads. */
typedef struct {
    /* The midpoint of the two shoulders, in the rig's own units. The rig is
       placed by putting this point at the camera-space anchor. */
    float shoulder_mid[3];
    /* Each shoulder's offset from that midpoint, in the pose actually solved -
       the grip clip moves the clavicles, so the bind pose is the wrong thing to
       calibrate against. Index 0 is the firing arm. */
    float shoulder_offset[2][3];
    /* Rig units to metres, from ARMS_REACH over the rig's own arm length. */
    float scale;
} ArmRig;

/* One answer: how far forward the shoulder line sits, and how far the stance is
   bladed. Two numbers, because one shoulder line cannot serve two grips that
   far apart - placed for the support hand the firing arm folds to a quarter of
   its length, and a quarter-folded arm throws its elbow 0.29m off the line to
   the hand, which puts a fist up through the sight picture. Yawing the rig the
   way a shooter blades their body brings the support shoulder forward and the
   firing shoulder back, and closes most of the gap. */
typedef struct {
    float z;
    float blade;
} ArmPlacement;

/* Rig space to camera space, rotation only: the facing, then the blade. */
void arm_placement_rig_to_camera(const float in[3], float blade, float out[3]);

/* The same backwards. A 180 degree yaw is its own inverse, which is why the
   facing reads the same in both directions. */
void arm_placement_rig_from_camera(const float in[3], float blade,
                                   float out[3]);

/* Where one shoulder lands in camera space for a candidate placement. */
void arm_placement_shoulder(const ArmRig* rig, int arm,
                            ArmPlacement placement, float out[3]);

/* How far one arm has to stretch, as a fraction of its reach. What the search
   is scoring, and the number worth printing when a stance looks wrong: at
   ARMS_COMFORT it is comfortably bent, near ARMS_COMFORT_MAX it is straight and
   has stopped tracking the gun, and well below it is folded and throwing its
   elbow somewhere the player can see. */
float arm_placement_reach_fraction(const ArmRig* rig, int arm,
                                   const float target[3],
                                   ArmPlacement placement);

/* Solves one pose. `targets` are the two grips in camera space for that pose,
   `half_fov_degrees` the half-angle of the projection the gun is drawn through.
   A scan rather than anything clever: two unknowns over a bounded range, run
   once per weapon, and a scan cannot walk off into a local minimum the way a
   gradient step can. Returns non-zero when something was found - and a caller
   must check, because a pose no placement reaches leaves `out` untouched. */
int arm_placement_solve(const ArmRig* rig, const float targets[2][3],
                        float half_fov_degrees, ArmPlacement* out);

/* The same, biased toward `previous` when one is given.

   For a SEQUENCE of placements this is the one to call, and the plain solve is
   a trap. Two targets a couple of centimetres apart can have two quite
   different placements that are equally comfortable - the search found z -0.48
   with the torso bladed to 80 degrees for one reload key and z -0.23 square on
   for the next - and either answer is fine on its own. Interpolating between
   them is not: the midpoint fits neither, and the arm folds to half its reach
   halfway through a movement whose endpoints both measured perfectly.

   So a small penalty on distance from the previous answer, enough to break a
   tie between comparable minima and not enough to buy a worse bend. What says
   the weight is right is the check that walks the whole track and watches the
   reach between the keys, not the value itself. */
int arm_placement_solve_near(const ArmRig* rig, const float targets[2][3],
                             float half_fov_degrees,
                             const ArmPlacement* previous, ArmPlacement* out);

/* Where the shoulder line sits in camera space for a solved placement. */
void arm_placement_anchor(ArmPlacement placement, float out[3]);

/* A camera-space point, into the rig's own units. The placement is a uniform
   scale and a translation, so it inverts without a matrix: undo the anchor,
   undo the scale, and add back the point the anchor was measured from. The
   solve then runs entirely in model space, which is where the bone lengths
   are. */
void arm_placement_to_model_space(const ArmRig* rig, const float camera[3],
                                  const float anchor[3], float blade,
                                  float out[3]);

#endif
