#ifndef TRAVERSAL_H
#define TRAVERSAL_H

typedef struct {
    float x, y, z;
    /* Outward normal of the face the ray entered through, so a hit on a box's
       top reads as up and not as whichever side happened to be nearer. */
    float normal_x, normal_y, normal_z;
    int wall_index;
} TraversalHit;

/* The world-space ray under the crosshair, from the camera's look angles. The
   view matrix is built as Rz(-roll)Rx(-pitch)Ry(-yaw), so this is that rotation
   inverted and applied to view-space forward, (0, 0, -1). Roll does not appear
   because rolling about the view axis does not move the centre of the screen.

   Everything that aims through the crosshair must come here for its direction.
   Two hand-rolled copies of this formula had the pitch term negated, which put
   shots and grapples as far below the crosshair as the player was looking
   above it. */
void traversal_aim_direction(float yaw_degrees, float pitch_degrees,
                             float* x, float* y, float* z);

/* The full camera basis in world space. `right` and `up` are the screen's axes,
   so roll matters here even though it does not for the aim: anything positioned
   relative to the screen - the gun in the player's hands, and therefore the
   point a tracer leaves from - is rolled with the view. Each output may be
   NULL. `forward` agrees with traversal_aim_direction by construction. */
void traversal_view_basis(float yaw_degrees, float pitch_degrees,
                          float roll_degrees,
                          float forward[3], float right[3], float up[3]);

/* Nearest box along the ray within `max_distance`. A ray that starts inside a
   box does not hit that box. */
int traversal_raycast_walls(float origin_x, float origin_y, float origin_z,
                            float direction_x, float direction_y, float direction_z,
                            float max_distance, TraversalHit* hit);

#endif
