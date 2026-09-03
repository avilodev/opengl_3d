#ifndef VIEWMODEL_POSE_H
#define VIEWMODEL_POSE_H

/* Where a held thing sits in front of the eye, and which way it is turned.

   Camera space: +x right, +y up, -z forward. The rotations are in degrees,
   about the model's OWN origin rather than about the eye, which is what makes
   them a property of how the gun is carried instead of of where the player is
   looking.

   This is the whole of how the gun is drawn, in two kinds of rotation that are
   kept apart because they mean different things.

   The carry - pitch, yaw, roll - is COSMETIC. It moves the mesh, and it moves
   anything whose job is to sit on the mesh, and it must never reach the bullet
   or the crosshair: a gun canted in against the chest still shoots where it is
   pointed, and a sight that swung with the cant would be lying.

   The kick is not cosmetic. It is the gun's own recoil rotation, the same pair
   of numbers the round is composed from, and it is here because the gun is
   DRAWN through it: it sits between the gun's position and its carry in the
   transform the renderer builds, so any point taken off the mesh - the barrel
   tip a tracer leaves from, the glass a magnified pass is masked to, a
   magazine's release point - has to go through it too. Leaving it out was worth
   centimetres at the hip and the length of the receiver during a burst at the
   sprint carry, always in the direction the gun had just kicked.

   Where the round goes is still weapon_aim_angles and not this. The kick being
   in both is the point: one number, applied to the drawn gun here and to the
   shot there, is what stops the two disagreeing.

   A named type rather than six loose floats because a pose has to be storable:
   the sprint carry is one of these today and a field on a WeaponSpec the moment
   one gun wants its own. */
typedef struct {
    float x, y, z;
    /* Applied about the gun's origin as yaw, then pitch, then roll - so roll
       cants the gun about its own barrel whatever the other two did, which is
       the order a carry is actually described in. */
    float pitch, yaw, roll;
    /* The recoil rotation the gun is drawn through, outside the carry and
       inside the position: the muzzle rises about the gun's origin, and the
       carry rides on top of that rather than the other way about. Zero on a
       rest pose, which is why every pose built by brace initialiser still
       reads correctly. */
    float kick_pitch, kick_yaw;
} ViewmodelPose;

/* A point on the model, into camera space. THE place the pose's rotation is
   composed - every consumer that wants a point off the drawn weapon goes
   through here, so the tracer's origin, the optic's glass and the mesh itself
   cannot compose it three ways and drift apart. That drift is not theoretical:
   the barrel tip was built from the pose's position alone and left the tracer
   coming out of a muzzle the mesh had already rotated away from.

   `local` is in the model's own frame, the same one the muzzle, the rail and
   the grips are measured in. Safe to alias with `out`. */
void viewmodel_pose_transform_point(const ViewmodelPose* pose,
                                    const float local[3], float out[3]);

/* The same for a direction: the rotation and none of the translation. */
void viewmodel_pose_transform_direction(const ViewmodelPose* pose,
                                        const float local[3], float out[3]);

/* Component-wise, and the rotations too. Every blend between two carries goes
   through this rather than lerping six fields by hand at each call site. */
void viewmodel_pose_lerp(const ViewmodelPose* from, const ViewmodelPose* to,
                         float t, ViewmodelPose* out);

#endif
