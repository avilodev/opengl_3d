#include "gameplay/damage_number.h"
#include "utils/math_utils.h"

#include <math.h>
#include <string.h>

/* xorshift, for the same reason weapon.c has its own: the scatter belongs to
   this pool and must not move when something else calls rand(). */
static float random_unit(DamageNumbers* numbers) {
    unsigned int x = numbers->random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    numbers->random_state = x;
    return (float)(x >> 8) / 16777216.0f;
}

void damage_numbers_init(DamageNumbers* numbers) {
    if (!numbers) return;
    memset(numbers, 0, sizeof *numbers);
    /* Any non-zero seed; xorshift is stuck at zero. */
    numbers->random_state = 0x9E3779B9u;
}

/* The live entry this owner already has, or NULL. Only entries young enough to
   still be accumulating count: an owner shot, left alone for a second and shot
   again gets a second number, which is what says the two bursts were separate. */
static DamageNumber* mergeable(DamageNumbers* numbers, int owner) {
    if (owner == DAMAGE_NUMBER_NO_OWNER) return NULL;
    for (int i = 0; i < DAMAGE_NUMBER_MAX; ++i) {
        DamageNumber* number = &numbers->items[i];
        if (number->life <= 0.0f || number->owner != owner) continue;
        if (DAMAGE_NUMBER_LIFETIME - number->life > DAMAGE_NUMBER_MERGE_TIME)
            continue;
        return number;
    }
    return NULL;
}

void damage_numbers_add(DamageNumbers* numbers, int owner, int region,
                        float amount, float x, float y, float z) {
    if (!numbers) return;

    DamageNumber* number = mergeable(numbers, owner);
    if (number) {
        number->amount += amount;
        /* The anchor moves to the newest round rather than staying on the first
           one. A burst walks up a body and the number should walk with it. */
        number->x = x; number->y = y; number->z = z;
        /* The better of the two bands sticks: a burst that clipped a head is a
           burst that clipped a head, and the colour is what says so. */
        if (region > number->region) number->region = region;
        number->life = DAMAGE_NUMBER_LIFETIME;
        number->pop = DAMAGE_NUMBER_POP_TIME;
        return;
    }

    /* Overwrite the entry with the least life left rather than dropping the new
       one, so the hit the player just made always gets a slot. */
    int oldest = 0;
    for (int i = 1; i < DAMAGE_NUMBER_MAX; ++i)
        if (numbers->items[i].life < numbers->items[oldest].life) oldest = i;
    number = &numbers->items[oldest];

    number->x = x; number->y = y; number->z = z;
    float angle = random_unit(numbers) * 2.0f * (float)MATH_PI;
    number->drift_x = cosf(angle) * DAMAGE_NUMBER_DRIFT;
    number->drift_z = sinf(angle) * DAMAGE_NUMBER_DRIFT;
    number->amount = amount;
    number->life = DAMAGE_NUMBER_LIFETIME;
    number->pop = DAMAGE_NUMBER_POP_TIME;
    number->region = region;
    number->owner = owner;
}

void damage_numbers_update(DamageNumbers* numbers, float delta_time) {
    if (!numbers || delta_time <= 0.0f) return;
    for (int i = 0; i < DAMAGE_NUMBER_MAX; ++i) {
        DamageNumber* number = &numbers->items[i];
        if (number->life <= 0.0f) continue;
        number->life -= delta_time;
        number->pop -= delta_time;
        if (number->pop < 0.0f) number->pop = 0.0f;
        number->x += number->drift_x * delta_time;
        number->y += DAMAGE_NUMBER_RISE * delta_time;
        number->z += number->drift_z * delta_time;
    }
}

float damage_number_pop_blend(const DamageNumber* number) {
    if (!number || number->pop <= 0.0f) return 0.0f;
    /* Squared, so the swell falls away fast and the figure spends most of the
       pop at its settled size rather than most of it inflated. */
    float t = number->pop / DAMAGE_NUMBER_POP_TIME;
    return t * t;
}

int damage_numbers_latest(const DamageNumbers* numbers, float* age, int* region) {
    if (!numbers) return 0;
    const DamageNumber* newest = NULL;
    for (int i = 0; i < DAMAGE_NUMBER_MAX; ++i) {
        const DamageNumber* number = &numbers->items[i];
        if (number->life <= 0.0f) continue;
        if (!newest || number->life > newest->life) newest = number;
    }
    if (!newest) return 0;
    if (age) *age = DAMAGE_NUMBER_LIFETIME - newest->life;
    if (region) *region = newest->region;
    return 1;
}
