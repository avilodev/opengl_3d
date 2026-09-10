#include "gameplay/arena.h"
#include "utils/math_utils.h"

/* FLOODGATE K-12 -- a decommissioned drainage works, 210m square

   A facility rather than a plan: laid out the way one grows, not mirrored
   about an axis. Nothing marks a spawn as a spawn -- the twelve slots stand on
   open ground among ordinary concrete, and no geometry or material is reserved
   for either side.

   Balanced by capability rather than by symmetry. Each half gets an interior
   to fight inside, a climb to own the air, and a lane to slide. The digesters
   answer the silos, the pump house answers the control block, and the mid
   station sits on the long axis so no spawn can see the other.

   Boxes with a base above zero are what make it a building. Two invariants are
   checked by tools/arena_export.py and hold as written: nothing standable has
   less than MOVE_PLAYER_HEIGHT of clear air over it, and every raised surface
   has a neighbour within MOVE_MANTLE_MAX_RISE of its top. */
const ArenaWall g_arena_walls[ARENA_WALL_COUNT] = {
    /* Perimeter flood walls. */
    {-105,0,2,210,0,22,ARENA_MATERIAL_NEUTRAL},
    {105,0,2,210,0,22,ARENA_MATERIAL_NEUTRAL},
    {0,-105,210,2,0,22,ARENA_MATERIAL_NEUTRAL},
    {0,105,210,2,0,22,ARENA_MATERIAL_NEUTRAL},

    /* Pump house, north-west. Two storeys: slab over the west half, windows above, stepped climb to the roof. */
    {-48,-70.5f,34,1,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-57.625f,-45.5f,14.75f,1,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-48,-45.5f,4.5f,1,3.4f,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-38.375f,-45.5f,14.75f,1,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-64.5f,-58,1,24,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-31.5f,-65.125f,1,9.75f,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-31.5f,-58,1,4.5f,3.4f,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-31.5f,-50.875f,1,9.75f,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-55,-58,18,24,6,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-62.75f,-70.5f,4.5f,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-58,-70.5f,5,1,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {-58,-70.5f,5,1,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-49,-70.5f,13,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-40,-70.5f,5,1,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {-40,-70.5f,5,1,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-34.25f,-70.5f,6.5f,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-62.75f,-45.5f,4.5f,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-58,-45.5f,5,1,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {-58,-45.5f,5,1,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-49,-45.5f,13,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-40,-45.5f,5,1,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {-40,-45.5f,5,1,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-34.25f,-45.5f,6.5f,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-64.5f,-69.25f,1,1.5f,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-64.5f,-66,1,5,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {-64.5f,-66,1,5,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-64.5f,-58,1,11,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-64.5f,-50,1,5,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {-64.5f,-50,1,5,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-64.5f,-46.75f,1,1.5f,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-31.5f,-66.25f,1,7.5f,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-31.5f,-49.75f,1,7.5f,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-48,-58,34,26,12.8f,13.4f,ARENA_MATERIAL_NEUTRAL},
    {-38,-66,2,2,0,6.4f,ARENA_MATERIAL_HAZARD},
    {-38,-50,2,2,0,6.4f,ARENA_MATERIAL_HAZARD},
    {-43.5f,-64,4.5f,4.5f,0,1.7f,ARENA_MATERIAL_NEUTRAL},
    {-43.5f,-59.5f,4.5f,4.5f,0,3.4f,ARENA_MATERIAL_NEUTRAL},
    {-43.5f,-55,4.5f,4.5f,0,5.1f,ARENA_MATERIAL_NEUTRAL},
    {-48,-74.25f,8,4.5f,0,11.4f,ARENA_MATERIAL_NEUTRAL},
    {-48,-78.75f,8,4.5f,0,9.2f,ARENA_MATERIAL_NEUTRAL},
    {-48,-83.25f,8,4.5f,0,7,ARENA_MATERIAL_NEUTRAL},
    {-48,-87.75f,8,4.5f,0,4.8f,ARENA_MATERIAL_NEUTRAL},
    {-48,-92.25f,8,4.5f,0,2.6f,ARENA_MATERIAL_NEUTRAL},

    /* Control block, south-east. The pump house's opposite number, rotated and mirrored. */
    {41.875f,44.5f,15.75f,1,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {52,44.5f,4.5f,1,3.4f,6.4f,ARENA_MATERIAL_NEUTRAL},
    {62.125f,44.5f,15.75f,1,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {52,71.5f,36,1,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {34.5f,50.375f,1,10.75f,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {34.5f,58,1,4.5f,3.4f,6.4f,ARENA_MATERIAL_NEUTRAL},
    {34.5f,65.625f,1,10.75f,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {69.5f,58,1,26,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {60,58,18,26,6,6.4f,ARENA_MATERIAL_NEUTRAL},
    {36.75f,44.5f,5.5f,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {42,44.5f,5,1,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {42,44.5f,5,1,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {52,44.5f,15,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {62,44.5f,5,1,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {62,44.5f,5,1,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {67.25f,44.5f,5.5f,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {36.75f,71.5f,5.5f,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {42,71.5f,5,1,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {42,71.5f,5,1,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {52,71.5f,15,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {62,71.5f,5,1,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {62,71.5f,5,1,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {67.25f,71.5f,5.5f,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {34.5f,49,1,8,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {34.5f,67,1,8,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {69.5f,45.25f,1,.5f,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {69.5f,48,1,5,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {69.5f,48,1,5,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {69.5f,58,1,15,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {69.5f,68,1,5,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {69.5f,68,1,5,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {69.5f,70.75f,1,.5f,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {52,58,36,28,12.8f,13.4f,ARENA_MATERIAL_NEUTRAL},
    {48.5f,50,4.5f,4.5f,0,1.7f,ARENA_MATERIAL_NEUTRAL},
    {48.5f,54.5f,4.5f,4.5f,0,3.4f,ARENA_MATERIAL_NEUTRAL},
    {48.5f,59,4.5f,4.5f,0,5.1f,ARENA_MATERIAL_NEUTRAL},
    {52,75.25f,8,4.5f,0,11.4f,ARENA_MATERIAL_NEUTRAL},
    {52,79.75f,8,4.5f,0,9.2f,ARENA_MATERIAL_NEUTRAL},
    {52,84.25f,8,4.5f,0,7,ARENA_MATERIAL_NEUTRAL},
    {52,88.75f,8,4.5f,0,4.8f,ARENA_MATERIAL_NEUTRAL},
    {52,93.25f,8,4.5f,0,2.6f,ARENA_MATERIAL_NEUTRAL},

    /* Mid station. Sits on the long axis so no spawn sees the other. */
    {-8.125f,-11.5f,11.75f,1,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {0,-11.5f,4.5f,1,3.4f,6.4f,ARENA_MATERIAL_NEUTRAL},
    {8.125f,-11.5f,11.75f,1,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-8.125f,11.5f,11.75f,1,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {0,11.5f,4.5f,1,3.4f,6.4f,ARENA_MATERIAL_NEUTRAL},
    {8.125f,11.5f,11.75f,1,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-13.5f,0,1,22,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {13.5f,0,1,22,0,6.4f,ARENA_MATERIAL_NEUTRAL},
    {0,-6,26,10,6,6.4f,ARENA_MATERIAL_NEUTRAL},
    {-9,-11.5f,10,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {9,-11.5f,10,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-12.25f,11.5f,3.5f,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-8,11.5f,5,1,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {-8,11.5f,5,1,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {0,11.5f,11,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {8,11.5f,5,1,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {8,11.5f,5,1,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {12.25f,11.5f,3.5f,1,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-13.5f,-9.75f,1,2.5f,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-13.5f,-6,1,5,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {-13.5f,-6,1,5,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-13.5f,0,1,7,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-13.5f,6,1,5,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {-13.5f,6,1,5,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {-13.5f,9.75f,1,2.5f,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {13.5f,-9.75f,1,2.5f,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {13.5f,-6,1,5,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {13.5f,-6,1,5,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {13.5f,0,1,7,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {13.5f,6,1,5,6.4f,7.6f,ARENA_MATERIAL_NEUTRAL},
    {13.5f,6,1,5,10.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {13.5f,9.75f,1,2.5f,6.4f,12.8f,ARENA_MATERIAL_NEUTRAL},
    {0,0,28,24,12.8f,13.4f,ARENA_MATERIAL_NEUTRAL},
    {6,1.5f,5,4.5f,0,5.1f,ARENA_MATERIAL_NEUTRAL},
    {6,5.5f,5,4.5f,0,3.4f,ARENA_MATERIAL_NEUTRAL},
    {6,9.5f,5,4.5f,0,1.7f,ARENA_MATERIAL_NEUTRAL},
    {17.25f,0,4.5f,8,0,11.4f,ARENA_MATERIAL_NEUTRAL},
    {21.75f,0,4.5f,8,0,9.2f,ARENA_MATERIAL_NEUTRAL},
    {26.25f,0,4.5f,8,0,7,ARENA_MATERIAL_NEUTRAL},
    {30.75f,0,4.5f,8,0,4.8f,ARENA_MATERIAL_NEUTRAL},
    {35.25f,0,4.5f,8,0,2.6f,ARENA_MATERIAL_NEUTRAL},

    /* Sheds, and the crates that reach their roofs. */
    {-14.5f,65.45f,9,.9f,0,6,ARENA_MATERIAL_NEUTRAL},
    {-8,65.45f,4,.9f,3.4f,6,ARENA_MATERIAL_NEUTRAL},
    {-1.5f,65.45f,9,.9f,0,6,ARENA_MATERIAL_NEUTRAL},
    {-8,82.55f,22,.9f,0,6,ARENA_MATERIAL_NEUTRAL},
    {-18.55f,68.95f,.9f,6.1f,0,6,ARENA_MATERIAL_NEUTRAL},
    {-18.55f,74,.9f,4,0,1.2f,ARENA_MATERIAL_NEUTRAL},
    {-18.55f,74,.9f,4,4,6,ARENA_MATERIAL_NEUTRAL},
    {-18.55f,79.05f,.9f,6.1f,0,6,ARENA_MATERIAL_NEUTRAL},
    {2.55f,74,.9f,16.2f,0,6,ARENA_MATERIAL_NEUTRAL},
    {-8,74,22,18,6,6.5f,ARENA_MATERIAL_NEUTRAL},
    {20,82.45f,8,.9f,0,6,ARENA_MATERIAL_NEUTRAL},
    {26,82.45f,4,.9f,0,1.2f,ARENA_MATERIAL_NEUTRAL},
    {26,82.45f,4,.9f,4,6,ARENA_MATERIAL_NEUTRAL},
    {32,82.45f,8,.9f,0,6,ARENA_MATERIAL_NEUTRAL},
    {26,97.55f,20,.9f,0,6,ARENA_MATERIAL_NEUTRAL},
    {16.45f,85.45f,.9f,5.1f,0,6,ARENA_MATERIAL_NEUTRAL},
    {16.45f,90,.9f,4,3.4f,6,ARENA_MATERIAL_NEUTRAL},
    {16.45f,94.55f,.9f,5.1f,0,6,ARENA_MATERIAL_NEUTRAL},
    {35.55f,90,.9f,14.2f,0,6,ARENA_MATERIAL_NEUTRAL},
    {26,90,20,16,6,6.5f,ARENA_MATERIAL_NEUTRAL},
    {6.25f,74,4.5f,6,0,4.4f,ARENA_MATERIAL_NEUTRAL},
    {11.25f,74,4.5f,6,0,2.2f,ARENA_MATERIAL_NEUTRAL},
    {26,78.75f,6,4.5f,0,4.4f,ARENA_MATERIAL_NEUTRAL},
    {26,73.75f,6,4.5f,0,2.2f,ARENA_MATERIAL_NEUTRAL},

    /* Settling basins: open to the sky, walls to ride, one scraper bridge across the top. */
    {50,-78.2f,48,1.6f,0,6,ARENA_MATERIAL_NEUTRAL},
    {50,-45.8f,48,1.6f,0,6,ARENA_MATERIAL_NEUTRAL},
    {26.8f,-72.7f,1.6f,9.4f,0,6,ARENA_MATERIAL_NEUTRAL},
    {26.8f,-51.3f,1.6f,9.4f,0,6,ARENA_MATERIAL_NEUTRAL},
    {73.2f,-62,1.6f,30.8f,0,6,ARENA_MATERIAL_NEUTRAL},
    {50,-62,38,26,0,.8f,ARENA_MATERIAL_WATER},
    {50,-62,48,2.4f,6,6.8f,ARENA_MATERIAL_HAZARD},
    {76.75f,-44.2f,9.5f,1.6f,0,5,ARENA_MATERIAL_NEUTRAL},
    {95.25f,-44.2f,9.5f,1.6f,0,5,ARENA_MATERIAL_NEUTRAL},
    {86,-15.8f,28,1.6f,0,5,ARENA_MATERIAL_NEUTRAL},
    {72.8f,-30,1.6f,26.8f,0,5,ARENA_MATERIAL_NEUTRAL},
    {99.2f,-30,1.6f,26.8f,0,5,ARENA_MATERIAL_NEUTRAL},
    {86,-30,20,22,0,.6f,ARENA_MATERIAL_WATER},

    /* Inlet screens and their channel. */
    {-4,-84,1.2f,11,0,7,ARENA_MATERIAL_HAZARD},
    {4,-84,1.2f,11,0,7,ARENA_MATERIAL_HAZARD},
    {12,-84,1.2f,11,0,7,ARENA_MATERIAL_HAZARD},
    {20,-84,1.2f,11,0,7,ARENA_MATERIAL_HAZARD},
    {8,-74,32,10,0,.5f,ARENA_MATERIAL_WATER},

    /* Digester tanks, east. Every rise in the chain is at or under MOVE_MANTLE_MAX_RISE. */
    {80,5,18,18,0,16,ARENA_MATERIAL_NEUTRAL},
    {58,22,15,15,0,11,ARENA_MATERIAL_NEUTRAL},
    {88,42,13,13,0,21,ARENA_MATERIAL_NEUTRAL},
    {58,11.75f,9,3.5f,0,8.8f,ARENA_MATERIAL_NEUTRAL},
    {58,8.25f,9,3.5f,0,6.6f,ARENA_MATERIAL_NEUTRAL},
    {58,4.75f,9,3.5f,0,4.4f,ARENA_MATERIAL_NEUTRAL},
    {58,1.25f,9,3.5f,0,2.2f,ARENA_MATERIAL_NEUTRAL},
    {68.25f,14,5.5f,3,12.6f,13,ARENA_MATERIAL_HAZARD},
    {72.5f,5,4,18,13,15,ARENA_MATERIAL_HAZARD},
    {85,24.75f,3,21.5f,15.6f,16,ARENA_MATERIAL_HAZARD},
    {85,42,4,13,16,18.2f,ARENA_MATERIAL_HAZARD},
    {89,42,4,13,18.2f,20.4f,ARENA_MATERIAL_HAZARD},

    /* Silo cluster, west. The same climb the digesters are, so neither half owns the vertical. */
    {-88,10,14,14,0,14,ARENA_MATERIAL_NEUTRAL},
    {-86,34,12,12,0,9.5f,ARENA_MATERIAL_NEUTRAL},
    {-92,-18,11,11,0,18,ARENA_MATERIAL_NEUTRAL},
    {-86,42.25f,8,3.5f,0,7.3f,ARENA_MATERIAL_NEUTRAL},
    {-86,45.75f,8,3.5f,0,5.1f,ARENA_MATERIAL_NEUTRAL},
    {-86,49.25f,8,3.5f,0,2.9f,ARENA_MATERIAL_NEUTRAL},
    {-86,22.5f,5,11,9.6f,10,ARENA_MATERIAL_HAZARD},
    {-86,12,5,10,10,12.2f,ARENA_MATERIAL_HAZARD},
    {-92,-4.75f,3,15.5f,13.6f,14,ARENA_MATERIAL_HAZARD},
    {-92,-18,4,11,14,16.2f,ARENA_MATERIAL_HAZARD},

    /* Main channel: an L-bent slide lane under a suspended sluice gate. */
    {-25,-55,1.8f,46,0,3.5f,ARENA_MATERIAL_NEUTRAL},
    {-11,-60,1.8f,36,0,3.5f,ARENA_MATERIAL_NEUTRAL},
    {-18,-54,12.2f,48,0,.2f,ARENA_MATERIAL_WATER},
    {16.5f,-37,55,1.8f,0,3.5f,ARENA_MATERIAL_NEUTRAL},
    {13,-23,62,1.8f,0,3.5f,ARENA_MATERIAL_NEUTRAL},
    {13,-30,62,12.2f,0,.2f,ARENA_MATERIAL_WATER},
    {-18,-44,12.2f,2.5f,0,1,ARENA_MATERIAL_NEUTRAL},
    {30,-30,2.5f,12.2f,0,1,ARENA_MATERIAL_NEUTRAL},
    {-18,-30,14,1.8f,3.6f,9.5f,ARENA_MATERIAL_HAZARD},
    {-25,-30,2,2,0,9.5f,ARENA_MATERIAL_HAZARD},
    {-11,-30,2,2,0,9.5f,ARENA_MATERIAL_HAZARD},

    /* Wall-jump corridors. Paired faces 6.8m apart, hung from 7 to 21, with air underneath so the ground fight carries on below. */
    {-30,10,1.2f,60,7,21,ARENA_MATERIAL_NEUTRAL},
    {-22,10,1.2f,60,7,21,ARENA_MATERIAL_NEUTRAL},
    {20,26,50,1.2f,7,21,ARENA_MATERIAL_NEUTRAL},
    {20,34,50,1.2f,7,21,ARENA_MATERIAL_NEUTRAL},
    {-74,67,1.2f,55,7,21,ARENA_MATERIAL_NEUTRAL},
    {-66,67,1.2f,55,7,21,ARENA_MATERIAL_NEUTRAL},
    {-26,-25,8,8,0,11,ARENA_MATERIAL_NEUTRAL},
    {0,22,8,8,0,11,ARENA_MATERIAL_NEUTRAL},
    {-70,35,8,8,0,11,ARENA_MATERIAL_NEUTRAL},
    {-26,-33.5f,8,5,0,8.8f,ARENA_MATERIAL_NEUTRAL},
    {-26,-38.5f,8,5,0,6.6f,ARENA_MATERIAL_NEUTRAL},
    {-26,-43.5f,8,5,0,4.4f,ARENA_MATERIAL_NEUTRAL},
    {-26,-48.5f,8,5,0,2.2f,ARENA_MATERIAL_NEUTRAL},
    {-7.5f,22,5,8,0,8.8f,ARENA_MATERIAL_NEUTRAL},
    {-12.5f,22,5,8,0,6.6f,ARENA_MATERIAL_NEUTRAL},
    {-17.5f,22,5,8,0,4.4f,ARENA_MATERIAL_NEUTRAL},
    {-22.5f,22,5,8,0,2.2f,ARENA_MATERIAL_NEUTRAL},
    {-70,27.5f,8,5,0,8.8f,ARENA_MATERIAL_NEUTRAL},
    {-70,22.5f,8,5,0,6.6f,ARENA_MATERIAL_NEUTRAL},
    {-70,17.5f,8,5,0,4.4f,ARENA_MATERIAL_NEUTRAL},
    {-70,12.5f,8,5,0,2.2f,ARENA_MATERIAL_NEUTRAL},

    /* Pipe runs and bridges, raised, run over and under. */
    {-98,58,6,70,6.5f,8,ARENA_MATERIAL_HAZARD},
    {-98,28,2.4f,2.4f,0,6.5f,ARENA_MATERIAL_NEUTRAL},
    {-98,58,2.4f,2.4f,0,6.5f,ARENA_MATERIAL_NEUTRAL},
    {-98,88,2.4f,2.4f,0,6.5f,ARENA_MATERIAL_NEUTRAL},
    {-92.5f,30,3.5f,4,0,5.8f,ARENA_MATERIAL_NEUTRAL},
    {-92.5f,34,3.5f,4,0,3.6f,ARENA_MATERIAL_NEUTRAL},
    {-92.5f,38,3.5f,4,0,1.4f,ARENA_MATERIAL_NEUTRAL},
    {-18,-16,16,3,7,8.2f,ARENA_MATERIAL_HAZARD},
    {-26,-16,2,2,0,7,ARENA_MATERIAL_NEUTRAL},
    {-10,-16,2,2,0,7,ARENA_MATERIAL_NEUTRAL},
    {44,-30,3,18,6.5f,7.7f,ARENA_MATERIAL_HAZARD},
    {48,-41,4,4,0,5.5f,ARENA_MATERIAL_NEUTRAL},
    {48,-45,4,4,0,3.3f,ARENA_MATERIAL_NEUTRAL},
    {48,-49,4,4,0,1.1f,ARENA_MATERIAL_NEUTRAL},

    /* Filter galleries: waist-high cover. */
    {-64,72,26,1.8f,0,2.2f,ARENA_MATERIAL_NEUTRAL},
    {-32,72,22,1.8f,0,2.2f,ARENA_MATERIAL_NEUTRAL},
    {-64,80,26,1.8f,0,2.2f,ARENA_MATERIAL_NEUTRAL},
    {-32,80,22,1.8f,0,2.2f,ARENA_MATERIAL_NEUTRAL},
    {-48,88,20,1.8f,0,2.2f,ARENA_MATERIAL_NEUTRAL},

    /* Outfall channel, stepped. */
    {70,84,34,1.8f,0,7,ARENA_MATERIAL_NEUTRAL},
    {70,98,34,1.8f,0,7,ARENA_MATERIAL_NEUTRAL},
    {70,91,34,12.2f,0,.25f,ARENA_MATERIAL_WATER},
    {56,91,4,12.2f,0,1.4f,ARENA_MATERIAL_NEUTRAL},
    {61,91,4,12.2f,0,.7f,ARENA_MATERIAL_NEUTRAL},

    /* Grapple masts, spaced inside MOVE_GRAPPLE_RANGE of the routes they serve. */
    {-56,-20,1.8f,1.8f,0,17,ARENA_MATERIAL_HAZARD},
    {28,-58,1.8f,1.8f,0,16,ARENA_MATERIAL_HAZARD},
    {-20,54,1.8f,1.8f,0,15,ARENA_MATERIAL_HAZARD},
    {70,-70,1.8f,1.8f,0,18,ARENA_MATERIAL_HAZARD},
    {34,4,1.8f,1.8f,0,16,ARENA_MATERIAL_HAZARD},
    {-84,74,1.8f,1.8f,0,15,ARENA_MATERIAL_HAZARD},

    /* Scattered yard clutter and sightline breakers. */
    {-40,-30,6,8,0,1.6f,ARENA_MATERIAL_NEUTRAL},
    {-6,-50,5,5,0,3,ARENA_MATERIAL_NEUTRAL},
    {22,-48,9,5,0,1.5f,ARENA_MATERIAL_NEUTRAL},
    {40,-46,6,6,0,3.6f,ARENA_MATERIAL_NEUTRAL},
    {-2,34,8,5,0,1.7f,ARENA_MATERIAL_NEUTRAL},
    {-30,58,6,9,0,3,ARENA_MATERIAL_NEUTRAL},
    {18,48,5,5,0,1.4f,ARENA_MATERIAL_NEUTRAL},
    {46,-10,8,4,0,2.1f,ARENA_MATERIAL_NEUTRAL},
    {-78,-46,6,6,0,3.8f,ARENA_MATERIAL_NEUTRAL},
    {-46,-4,5,10,0,1.6f,ARENA_MATERIAL_NEUTRAL},
    {60,74,8,8,0,2.6f,ARENA_MATERIAL_NEUTRAL},
    {-96,56,9,6,0,2.2f,ARENA_MATERIAL_NEUTRAL},
    {66,-84,8,5,0,2.4f,ARENA_MATERIAL_NEUTRAL},
    {-70,-84,6,6,0,1.9f,ARENA_MATERIAL_NEUTRAL},
    {14,84,6,5,0,1.6f,ARENA_MATERIAL_NEUTRAL},
    {-24,-72,4,7,0,2.3f,ARENA_MATERIAL_NEUTRAL},
    {88,68,7,7,0,3.1f,ARENA_MATERIAL_NEUTRAL},
    {-14,20,9,4,0,1.5f,ARENA_MATERIAL_NEUTRAL},
    {74,-46,5,8,0,2.7f,ARENA_MATERIAL_NEUTRAL},
    {-58,90,7,5,0,1.8f,ARENA_MATERIAL_NEUTRAL},
    {26,64,5,5,0,2,ARENA_MATERIAL_NEUTRAL},
    {-96,30,6,8,0,2.5f,ARENA_MATERIAL_NEUTRAL},
    {96,-8,5,6,0,2.2f,ARENA_MATERIAL_NEUTRAL},
    {2,-96,8,5,0,1.7f,ARENA_MATERIAL_NEUTRAL},
    {-40,96,6,6,0,2,ARENA_MATERIAL_NEUTRAL},
    {52,32,7,4,0,1.6f,ARENA_MATERIAL_NEUTRAL},
    {-16,-88,5,7,0,2.4f,ARENA_MATERIAL_NEUTRAL},
    {80,-96,6,5,0,1.9f,ARENA_MATERIAL_NEUTRAL},
};

_Static_assert(sizeof(g_arena_walls) / sizeof(g_arena_walls[0]) ==
               ARENA_WALL_COUNT,
               "ARENA_WALL_COUNT must match the number of walls defined here");

static const ArenaSpawn TEAM_SPAWNS[ARENA_TEAM_COUNT][ARENA_TEAM_SIZE] = {
    {
        {-10,0,-92,180}, {-6,0,-90,180}, {-2,0,-92,180},
        {  2,0,-90,180}, { 6,0,-92,180}, {10,0,-90,180},
    },
    {
        { 10,0,92,0}, { 6,0,90,0}, { 2,0,92,0},
        { -2,0,90,0}, {-6,0,92,0}, {-10,0,90,0},
    },
};

const ArenaSpawn* arena_team_spawns(ArenaTeam team, int* count) {
    if (count) *count = 0;
    if (team < 0 || team >= ARENA_TEAM_COUNT) return 0;
    if (count) *count = ARENA_TEAM_SIZE;
    return TEAM_SPAWNS[team];
}

float arena_support_height(float x, float z, float radius, float max_height) {
    float best = 0.0f;
    for (int i = 0; i < ARENA_WALL_COUNT; ++i) {
        const ArenaWall* wall = &g_arena_walls[i];
        if (wall->top > max_height || wall->top <= best) continue;
        float hw = wall->width * .5f, hd = wall->depth * .5f;
        float nx = clampf(x, wall->x - hw, wall->x + hw);
        float nz = clampf(z, wall->z - hd, wall->z + hd);
        float dx = x - nx, dz = z - nz;
        if (dx * dx + dz * dz >= radius * radius) continue;
        best = wall->top;
    }
    return best;
}

float arena_ceiling_height(float x, float z, float radius, float min_height) {
    float best = ARENA_NO_CEILING;
    for (int i = 0; i < ARENA_WALL_COUNT; ++i) {
        const ArenaWall* wall = &g_arena_walls[i];
        if (wall->base < min_height || wall->base >= best) continue;
        float hw = wall->width * .5f, hd = wall->depth * .5f;
        float nx = clampf(x, wall->x - hw, wall->x + hw);
        float nz = clampf(z, wall->z - hd, wall->z + hd);
        float dx = x - nx, dz = z - nz;
        if (dx * dx + dz * dz >= radius * radius) continue;
        best = wall->base;
    }
    return best;
}
