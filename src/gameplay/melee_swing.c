/* See include/gameplay/melee_swing.h for what this decides and why each part
   of it is there. */
#include "gameplay/melee_swing.h"

#include <math.h>
#include <string.h>

void melee_swing_begin(MeleeSwing* swing, int attack) {
    if (!swing) return;
    swing->attack = attack;
    swing->progress = 0.0f;
    /* The list, which is the whole point of a begin. A swing that started
       without clearing it would be a swing that could not hit anything the
       previous one already had. */
    swing->hit_count = 0;
}

void melee_swing_end(MeleeSwing* swing) {
    if (!swing) return;
    swing->attack = -1;
    swing->hit_count = 0;
}

static int already_hit(const MeleeSwing* swing, int index) {
    for (int i = 0; i < swing->hit_count; ++i)
        if (swing->hit[i] == index) return 1;
    return 0;
}

static void remember(MeleeSwing* swing, int index) {
    if (swing->hit_count < MELEE_SWING_MAX_HITS)
        swing->hit[swing->hit_count++] = index;
    /* A full list simply stops remembering. Every body in the set is already
       in it, so there is nothing left that a further entry could protect. */
}

/* The blade's two ends, in whatever space `m` is in. */
static void blade_at(const Mat4* m, const MeleeSpec* melee,
                     float base[3], float tip[3]) {
    mat4_transform_point(m, melee->blade_base, base);
    mat4_transform_point(m, melee->blade_tip, tip);
}

static float distance3(const float a[3], const float b[3]) {
    float d = 0.0f;
    for (int c = 0; c < 3; ++c) {
        float e = a[c] - b[c];
        d += e * e;
    }
    return sqrtf(d);
}

/* One position of the blade, traced along its own length. The blade is a
   segment, so the trace runs base to tip and the first body it meets is the
   one it cut - which is also what stops a blade reaching through one person to
   hit the one behind them. */
static int cut_along(MeleeSwing* swing, const WeaponSpec* spec,
                     const MeleeAttack* attack,
                     const float base[3], const float tip[3],
                     CombatantSet* targets) {
    float length = distance3(base, tip);
    if (length < 1e-4f) return 0;

    float direction[3];
    for (int c = 0; c < 3; ++c) direction[c] = (tip[c] - base[c]) / length;

    CombatantHit hit;
    if (!combatant_raycast(targets, base[0], base[1], base[2],
                           direction[0], direction[1], direction[2],
                           length, &hit))
        return 0;
    if (already_hit(swing, hit.index)) return 0;

    remember(swing, hit.index);
    combatant_apply_damage(targets, &hit, spec->damage * attack->damage_scale);
    return 1;
}

int melee_swing_update(MeleeSwing* swing, const WeaponSpec* spec,
                       const Mat4* prev, const Mat4* now,
                       float progress, CombatantSet* targets) {
    if (!swing || !spec || !prev || !now || !targets) return 0;
    const MeleeSpec* melee = weapon_spec_melee(spec);
    if (!melee) return 0;
    if (swing->attack < 0 || swing->attack >= melee->attack_count) return 0;

    const MeleeAttack* attack = &melee->attacks[swing->attack];
    swing->progress = progress;

    /* A window stated backwards is a block: it has no live window at all
       rather than a zero-length one at the start that a rounding error could
       open. That comparison is the whole of what makes a block not cut. */
    if (attack->active_to <= attack->active_from) return 0;
    if (progress < attack->active_from || progress > attack->active_to) return 0;

    float from_base[3], from_tip[3], to_base[3], to_tip[3];
    blade_at(prev, melee, from_base, from_tip);
    blade_at(now, melee, to_base, to_tip);

    /* How finely to step, from how far the blade actually travelled. The tip
       moves furthest, so it sets the count - stepping off the base would leave
       the tip skipping a body's width between samples on any swing that
       pivots, which is every swing. */
    float travel = distance3(from_tip, to_tip);
    int steps = (int)(travel / MELEE_SWING_CHORD) + 1;
    if (steps > MELEE_SWING_MAX_STEPS) steps = MELEE_SWING_MAX_STEPS;
    if (steps < 1) steps = 1;   /* the divisor below, never zero */

    int cuts = 0;
    for (int s = 0; s <= steps; ++s) {
        float t = (float)s / (float)steps;
        float base[3], tip[3];
        for (int c = 0; c < 3; ++c) {
            base[c] = from_base[c] + (to_base[c] - from_base[c]) * t;
            tip[c] = from_tip[c] + (to_tip[c] - from_tip[c]) * t;
        }
        cuts += cut_along(swing, spec, attack, base, tip, targets);
    }
    return cuts;
}
