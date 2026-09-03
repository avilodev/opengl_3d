#include "gameplay/viewmodel_pose.h"
#include "utils/math_utils.h"

#include <math.h>

/* Roll, pitch, yaw, then the kick's yaw and pitch, applied to the point in that
   order from the inside out - so the composition reads
   Rx(kick_pitch) * Ry(kick_yaw) * Ry(yaw) * Rx(pitch) * Rz(roll) * p.

   That order is not free. It is the one the renderer names its glRotatef calls
   in, reversed, because GL applies the last transform named to the vertex
   first. Written the other way round here the drawn gun and every point taken
   off it would agree only while all but one of the angles were zero.

   The kick's yaw and the carry's are both about +y and could be one addition;
   they are kept apart because the kick's PITCH is not - it is outside the
   carry's yaw where the carry's own pitch is inside it, and at the sprint
   carry's twenty-five degrees of yaw the difference between the two slots is
   most of the kick. */
static void rotate(const ViewmodelPose* pose, const float in[3], float out[3]) {
    float roll = pose->roll * MATH_DEG_TO_RAD;
    float pitch = pose->pitch * MATH_DEG_TO_RAD;
    float yaw = pose->yaw * MATH_DEG_TO_RAD;

    /* Roll: about the barrel, which is -z, so it turns x into y. */
    float cr = cosf(roll), sr = sinf(roll);
    float x = in[0] * cr - in[1] * sr;
    float y = in[0] * sr + in[1] * cr;
    float z = in[2];

    /* Pitch: about +x, tipping the muzzle. */
    float cp = cosf(pitch), sp = sinf(pitch);
    float y2 = y * cp - z * sp;
    float z2 = y * sp + z * cp;

    /* Yaw: about +y, swinging the muzzle across the body. Taken together with
       the kick's yaw, which is about the same axis and outside it. */
    float cy = cosf(yaw + pose->kick_yaw * MATH_DEG_TO_RAD);
    float sy = sinf(yaw + pose->kick_yaw * MATH_DEG_TO_RAD);
    float x3 = x * cy + z2 * sy;
    float z3 = -x * sy + z2 * cy;

    /* And the kick's pitch, about +x and outside all of it: the recoil lifts
       the whole carried gun, cant and all. */
    float ck = cosf(pose->kick_pitch * MATH_DEG_TO_RAD);
    float sk = sinf(pose->kick_pitch * MATH_DEG_TO_RAD);
    out[0] = x3;
    out[1] = y2 * ck - z3 * sk;
    out[2] = y2 * sk + z3 * ck;
}

void viewmodel_pose_transform_point(const ViewmodelPose* pose,
                                    const float local[3], float out[3]) {
    float turned[3];
    rotate(pose, local, turned);
    out[0] = pose->x + turned[0];
    out[1] = pose->y + turned[1];
    out[2] = pose->z + turned[2];
}

void viewmodel_pose_transform_direction(const ViewmodelPose* pose,
                                        const float local[3], float out[3]) {
    float turned[3];
    rotate(pose, local, turned);
    out[0] = turned[0];
    out[1] = turned[1];
    out[2] = turned[2];
}

void viewmodel_pose_lerp(const ViewmodelPose* from, const ViewmodelPose* to,
                         float t, ViewmodelPose* out) {
    out->x = lerpf(from->x, to->x, t);
    out->y = lerpf(from->y, to->y, t);
    out->z = lerpf(from->z, to->z, t);
    /* Straight lerps, and they are safe here because no carry in this game
       turns the gun far enough for the short way round and the long way round
       to differ - the widest is the sprint cant at 25 degrees. A pose that
       wanted to pass through a half turn would want slerp and a quaternion,
       and would not be describable as three independent angles in the first
       place. */
    out->pitch = lerpf(from->pitch, to->pitch, t);
    out->yaw = lerpf(from->yaw, to->yaw, t);
    out->roll = lerpf(from->roll, to->roll, t);
    /* Carried through rather than dropped, so a blend of two rest poses cannot
       quietly discard a kick the caller had already set. Every rest pose has
       zero here, so this is normally an interpolation between nothing and
       nothing. */
    out->kick_pitch = lerpf(from->kick_pitch, to->kick_pitch, t);
    out->kick_yaw = lerpf(from->kick_yaw, to->kick_yaw, t);
}
